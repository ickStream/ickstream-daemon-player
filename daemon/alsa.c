/*$*********************************************************************\

Name            : -

Source File     : alsa.c

Description     : interface to alsa API 

Comments        : -

Called by       : audio module 

Calls           : 

Error Messages  : -
  
Date            : 26.02.2013

Updates         : -
                  
Author          : //MAF 

Remarks         : -

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright 
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright 
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of ickStream nor the names of its contributors 
 *     may be used to endorse or promote products derived from this software 
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\************************************************************************/

// #undef DEBUG

#include <stdio.h>
#include <strings.h>
#include <stdbool.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

#include "utils.h"
#include "audio.h"
#include "fifo.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
	Private symbols
\*=========================================================================*/
// none  


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static int    _backendGetDeviceList( char ***deviceListPtr, char ***descrListPtr );
static int    _ifNew( AudioIf *aif, const char *device ); 
static int    _ifDelete( AudioIf *aif, AudioTermMode mode ); 
static int    _ifPlay( AudioIf *aif, AudioFormat *format );
static int    _ifStop( AudioIf *aif, AudioTermMode mode );
static int    _ifSetPause( AudioIf *aif, bool pause );

static int    _ifSetParameters( AudioIf *aif, AudioFormat *format );
static void  *_ifThread( void *arg );


/*=========================================================================*\
      Return descriptor for this backend 
\*=========================================================================*/
AudioBackend *alsaDescriptor( void )
{
  static AudioBackend backend;
  
  // Set name	
  backend.next           = NULL;
  backend.name           = "alsa";
  backend.init           = NULL;     // optional
  backend.shutdown       = NULL;     // optional
  backend.getDevices     = &_backendGetDeviceList;
  backend.newIf          = &_ifNew; 
  backend.deleteIf       = &_ifDelete;
  backend.play           = &_ifPlay;
  backend.stop           = &_ifStop;
  backend.pause          = &_ifSetPause;

  return &backend;	
}


/*=========================================================================*\
      Get all ALSA pcm devices 
        descrListPtr might be NULL;
\*=========================================================================*/
static int _backendGetDeviceList( char ***deviceListPtr, char ***descrListPtr )
{
  void **hints; 
  void **hintPtr;
  int    retval;
  
/*------------------------------------------------------------------------*\
    Reset results 
\*------------------------------------------------------------------------*/
  *deviceListPtr = NULL;
  if( descrListPtr )
    *descrListPtr = NULL;
    
/*------------------------------------------------------------------------*\
    Try to get hints 
\*------------------------------------------------------------------------*/
  if( snd_device_name_hint(-1,"pcm",&hints)<0 ) {
    srvmsg( LOG_ERR, "ALSA: Error searching for pcm devices." ); 
    return -1; 
  }	

/*------------------------------------------------------------------------*\
    Count hints and allocate list storage
\*------------------------------------------------------------------------*/
  for( hintPtr=hints,retval=0; *hintPtr; hintPtr++ )
  	retval++;
  *deviceListPtr = calloc( retval+1, sizeof(*deviceListPtr) );
  if( descrListPtr )
    *descrListPtr = calloc( retval+1, sizeof(*descrListPtr) );
    
/*------------------------------------------------------------------------*\
    Loop over all hints 
\*------------------------------------------------------------------------*/
  for( hintPtr=hints,retval=0; *hintPtr; hintPtr++ ) {
    char  *str;
    
    // Get and check name
    str = snd_device_name_get_hint( *hintPtr, "NAME" );
    if( !str ) {
       srvmsg( LOG_ERR, "ALSA: Found device without name." ); 
       continue;
    }
  	(*deviceListPtr)[retval] = str;
  	
  	// Get and check description text
  	if( descrListPtr ) {
  	  str = snd_device_name_get_hint( *hintPtr, "DESC" );
  	  if( !str )
  	    str = strdup( "" );  
      (*descrListPtr)[retval] = str;
  	}
  	
  	retval++;    
  }
  
/*------------------------------------------------------------------------*\
    return number of devices found with name
\*------------------------------------------------------------------------*/
  return retval;
}


/*=========================================================================*\
    Open device and start thread
\*=========================================================================*/
static int _ifNew( AudioIf *aif, const char *device )
{
  snd_pcm_t *pcm;
  int        rc;
  
    DBGMSG( "Alsa: new interface" ); 

/*------------------------------------------------------------------------*\
    Open sound device 
\*------------------------------------------------------------------------*/
  rc = snd_pcm_open( &pcm, device, SND_PCM_STREAM_PLAYBACK, 0 );
  if( rc<0 ) {
    srvmsg( LOG_ERR, "Error opening alsa pcm device \"%s\": %s", 
                     device, snd_strerror(rc) );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Store handle 
\*------------------------------------------------------------------------*/
  aif->ifData = (void*)pcm;

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Close device
\*=========================================================================*/
static int _ifDelete( AudioIf *aif, AudioTermMode mode )
{
  snd_pcm_t *pcm = (snd_pcm_t*)aif->ifData;
  int        rc  = 0;
  
  DBGMSG( "Alsa: deleting interface" ); 

/*------------------------------------------------------------------------*\
    Stop current playback
\*------------------------------------------------------------------------*/
  if( aif->state==AudioIfRunning ) {
  	if( _ifStop(aif,mode) ) {
      srvmsg( LOG_ERR, "_ifDelete: Could not stop running playback." );
      rc = -1;
    }
  }

/*------------------------------------------------------------------------*\
    Close alsa device
\*------------------------------------------------------------------------*/
  if( pcm )
    snd_pcm_close( pcm );
  aif->ifData = NULL;

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return rc;
}


/*=========================================================================*\
    Attach a queue to device and start playing
\*=========================================================================*/
static int _ifPlay( AudioIf *aif, AudioFormat *format )
{
  DBGMSG( "Alsa: start playback" ); 
	  
/*------------------------------------------------------------------------*\
    Stop current playback
\*------------------------------------------------------------------------*/
  if( aif->state==AudioIfRunning ) {
  	if( _ifStop(aif,AudioDrop) ) {
      srvmsg( LOG_ERR, "_ifPlay: Could not stop running playback." );
      return -1;
    }
  }	

/*------------------------------------------------------------------------*\
    Check state
\*------------------------------------------------------------------------*/
  if( aif->state!=AudioIfInitialized ) {
    srvmsg( LOG_ERR, "_ifPlay: Cannot start playback, wrong state: %d", aif->state );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Set interface parameters
\*------------------------------------------------------------------------*/
  if( _ifSetParameters(aif,format) )
    return -1;
  
/*------------------------------------------------------------------------*\
    Fire up working thread
\*------------------------------------------------------------------------*/
  int rc = pthread_create( &aif->thread, NULL, _ifThread, aif );
  if( rc ) {
    srvmsg( LOG_ERR, "Unable to start audio backend thread: %s", strerror(rc) );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Stop playback
\*=========================================================================*/
static int _ifStop( AudioIf *aif, AudioTermMode mode )
{
  snd_pcm_t *pcm = (snd_pcm_t*)aif->ifData;

  DBGMSG( "Alsa stop: %d", mode ); 

/*------------------------------------------------------------------------*\
    Stop thread and optionally wait for termination   
\*------------------------------------------------------------------------*/
  aif->state = AudioIfTerminating;
  if( aif->thread && mode!=AudioForce )
     pthread_join( aif->thread, NULL ); 
  
/*------------------------------------------------------------------------*\
    How to deal with data in buffer?
\*------------------------------------------------------------------------*/
  if( mode==AudioDrain )
    snd_pcm_drain( pcm );
  else
    snd_pcm_drop( pcm );
  
/*------------------------------------------------------------------------*\
    Error?
\*------------------------------------------------------------------------*/
  if( aif->state!=AudioIfTerminatedOk )
    return -1;
           
/*------------------------------------------------------------------------*\
    reset state to initialized, that's it
\*------------------------------------------------------------------------*/
  aif->state = AudioIfInitialized;
  return 0;
}


/*=========================================================================*\
    Set pausemode
\*=========================================================================*/
static int _ifSetPause( AudioIf *aif, bool pause )
{
  snd_pcm_t *pcm = (snd_pcm_t*)aif->ifData;

  DBGMSG( "Alsa pause: %s", pause?"On":"Off" ); 

/*------------------------------------------------------------------------*\
    Set flag
\*------------------------------------------------------------------------*/
  int rc = snd_pcm_pause( pcm, pause?1:0 ); 
  if( rc<0 ) {
  	srvmsg( LOG_ERR, "Unable to pause alsa device: %s", aif->devName );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    (re)set stream parameters
\*=========================================================================*/
static int _ifSetParameters( AudioIf *aif, AudioFormat *format )
{
  snd_pcm_t           *pcm = (snd_pcm_t*)aif->ifData;
  snd_pcm_hw_params_t *hwParams;
  unsigned int         realRate;
  int                  rc;
  
  DBGMSG( "Alsa: setting format to %s", audioFormatStr(format) ); 

/*------------------------------------------------------------------------*\
    Collect hardware parameters on stack 
\*------------------------------------------------------------------------*/
  snd_pcm_hw_params_alloca( &hwParams );
  snd_pcm_hw_params_any( pcm, hwParams );

  // Get pause support
  aif->canPause = snd_pcm_hw_params_can_pause( hwParams );

  // Multi channel is interleaved
  rc = snd_pcm_hw_params_set_channels( pcm, hwParams, format->channels );
  if( rc<0 ) {
  	srvmsg( LOG_ERR, "Unable to set alsa pcm hw parameter: channels (%d)",
                     format->channels );
    return -1;
  }
  if( format->channels>1 )
    rc = snd_pcm_hw_params_set_access( pcm, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED );
  if( rc<0 ) {
  	srvmsg( LOG_ERR, "Unable to set alsa pcm hw parameter: SND_PCM_ACCESS_RW_INTERLEAVED" );
    return -1;
  }

  // Fixme: set data format
  rc = snd_pcm_hw_params_set_format( pcm, hwParams, SND_PCM_FORMAT_S16_LE );
  if( rc<0 ) {
  	srvmsg( LOG_ERR, "Unable to set alsa pcm hw parameter: SND_PCM_FORMAT_S16_LE" );
    return -1;
  }

  aif->framesize = 2*format->channels;

  // Sample rate 
  realRate = format->sampleRate;
  rc = snd_pcm_hw_params_set_rate_near( pcm, hwParams, &realRate, NULL);
  if( rc<0 ) {
  	srvmsg( LOG_ERR, "Unable to set alsa pcm hw parameter: rate (%d)", format->sampleRate );
    return -1;
  }
  if( realRate!=format->sampleRate ) {
    srvmsg( LOG_ERR, "alsa: device \"%s\" does not support %dfps (would be %dfps)",
                      aif->devName, format->sampleRate, realRate );
    return -1;
  }

  // Set number of periods
  rc = snd_pcm_hw_params_set_periods( pcm, hwParams, 2, 0 );
  if( rc<0 ) {
  	srvmsg( LOG_ERR, "Unable to set alsa pcm hw parameter: periods 2, 0" );
    return -1;
  }
  
  
  
/*------------------------------------------------------------------------*\
    Apply the hardware parameters 
\*------------------------------------------------------------------------*/
  rc = snd_pcm_hw_params( pcm, hwParams );
  if( rc<0 ) {
    srvmsg( LOG_ERR, "Unable to set alsa pcm hw parameters: %s",
                     snd_strerror(rc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Set software parameters 
\*------------------------------------------------------------------------*/  
  // buffer size
  // snd_pcm_sw_params_set_avail_min(pcm_handle, sw_params, period_size);
  
/*------------------------------------------------------------------------*\
    Prepare interface 
\*------------------------------------------------------------------------*/
  rc = snd_pcm_prepare( pcm );
  if( rc<0 ) {
    srvmsg( LOG_ERR, "Unable to prepare alsa interface: %s",
                     snd_strerror(rc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
       An audio backend thread 
\*=========================================================================*/
static void *_ifThread( void *arg )
{
  AudioIf            *aif     = (AudioIf*)arg; 
  snd_pcm_t          *pcm     = (snd_pcm_t*)aif->ifData;

  DBGMSG( "Alsa thread: starting." ); 
  
/*------------------------------------------------------------------------*\
    Thread main loop  
\*------------------------------------------------------------------------*/
  aif->state = AudioIfRunning;
  while( aif->state==AudioIfRunning ) {
    int    rc;
    
    // Wait (max 500ms) till data is actually needed
    //0: timedout  1: ready  <0: xrun, suspend or error  
    rc =  snd_pcm_wait( pcm, 500 );
    if( !rc ) {
      DBGMSG( "Alsa thread: timout while waiting for free buffer size." ); 
      continue;
    }
    if( rc==-EPIPE || rc==-ESTRPIPE ) {
      rc = snd_pcm_recover( pcm, rc, 0 ); 
      if( rc ) {
        srvmsg( LOG_ERR, "Alsa thread (after wait): Unable to recover alsa interface: %s", strerror(rc) );
        aif->state = AudioIfTerminatedError;
        break;
      } 
    }
    else if( rc<0 ) {
      srvmsg( LOG_ERR, "Alsa thread: Error while waiting for alsa interface: %s", strerror(rc) );
      aif->state = AudioIfTerminatedError;
      break;
    } 
    
    // Wait for data in fifo
    rc = fifoLockWaitReadable( aif->fifoIn, 500 );
    if( rc==ETIMEDOUT ) {
      DBGMSG( "Alsa thread: timout while waiting for fifo data." );	
  	  continue;
  	}
    if( rc ) {
      DBGMSG( "Alsa thread: wait for fifo error, terminating: %s", strerror(rc) );
  	  aif->state = AudioIfTerminatedError;
  	  break; 	
    }
    
    // How much data can be delivered
    snd_pcm_sframes_t frames_writable = snd_pcm_avail_update( pcm );
    snd_pcm_sframes_t frames_readable = fifoGetSize( aif->fifoIn, FifoNextReadable )/aif->framesize;
    snd_pcm_sframes_t frames = MIN( frames_writable, frames_readable );

    // Do transfer the data
    DBGMSG( "Alsa thread: writing %d frames", frames );
    rc = snd_pcm_writei( pcm, aif->fifoIn->readp, frames );		
    if( rc==-EPIPE || rc==-ESTRPIPE ){
      rc = snd_pcm_recover( pcm, rc, 0 ); 
      if( rc ) {
        srvmsg( LOG_ERR, "Alsa thread (after write): Unable to recover alsa interface: %s", strerror(rc) );
        aif->state = AudioIfTerminatedError;
        break;
      } 
    }
    else if( rc<0 ) {
      srvmsg( LOG_ERR, "Alsa thread: Error while writing data to alsa interface: %s", strerror(rc) );
      aif->state = AudioIfTerminatedError;
      break;
    } 

    // Check out accepted data from fifo
    fifoUnlockAfterRead( aif->fifoIn, rc*aif->framesize ); 
  	
  }  // End of: Thread main loop
   
/*------------------------------------------------------------------------*\
    Fulfilled external termination request without error...  
\*------------------------------------------------------------------------*/
  DBGMSG( "Alsa thread: terminating due to state %d", aif->state );
  if( aif->state==AudioIfTerminating )
    aif->state = AudioIfTerminatedOk;   

/*------------------------------------------------------------------------*\
    That's it ...  
\*------------------------------------------------------------------------*/
  return NULL;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
