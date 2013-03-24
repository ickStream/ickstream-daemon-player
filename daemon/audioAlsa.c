/*$*********************************************************************\

Name            : -

Source File     : audioAlsa.c

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

#undef DEBUG

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
typedef struct {
  snd_pcm_t        *pcm;        // PCM output
  snd_mixer_elem_t *mixerElem;  // Volume control
} AlsaData; 


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static int    _backendGetDeviceList( char ***deviceListPtr, char ***descrListPtr );
static int    _ifNew( AudioIf *aif );
static int    _ifGetMixer( AudioIf *aif );
static int    _ifDelete( AudioIf *aif, AudioTermMode mode ); 
static int    _ifPlay( AudioIf *aif, AudioFormat *format );
static int    _ifStop( AudioIf *aif, AudioTermMode mode );
static int    _ifSetPause( AudioIf *aif, bool pause );
static int    _ifSetVolume( AudioIf *aif, double volume, bool muted ); 

static int               _ifSetParameters( AudioIf *aif, AudioFormat *format );
static snd_pcm_format_t  _getAlsaFormat( const AudioFormat *format );
static void             *_ifThread( void *arg );


/*=========================================================================*\
      Return descriptor for this backend 
\*=========================================================================*/
AudioBackend *audioAlsaDescriptor( void )
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
  backend.setVolume      = &_ifSetVolume;

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
    logerr( "ALSA: Error searching for pcm devices." ); 
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
       logerr( "ALSA: Found device without name." ); 
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
static int _ifNew( AudioIf *aif )
{
  AlsaData  *ifData;
  int        rc;
  
  DBGMSG( "Alsa (%s): open new interface", aif->devName ); 

/*------------------------------------------------------------------------*\
    Create auxiliary data
\*------------------------------------------------------------------------*/
  ifData = calloc( 1, sizeof(AlsaData) );
  if( !ifData ) {
    logerr( "Out of memory." );
    return -1;
  }
  aif->ifData = ifData;

/*------------------------------------------------------------------------*\
    Open sound device 
\*------------------------------------------------------------------------*/
  rc = snd_pcm_open( &ifData->pcm, aif->devName, SND_PCM_STREAM_PLAYBACK, 0 );
  if( rc<0 ) {
    logerr( "Alsa (%s): Error opening pcm device: %s", aif->devName, snd_strerror(rc) );
    Sfree( aif->ifData );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Is there a mixer availabale 
\*------------------------------------------------------------------------*/
  if( !_ifGetMixer(aif) )
    aif->hasVolume = true;

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Get mixer for interface
\*=========================================================================*/
static int _ifGetMixer( AudioIf *aif )
{
  AlsaData         *ifData = (AlsaData*)aif->ifData;
  snd_pcm_info_t   *pcmInfo;
  char              cardName[32];
  snd_mixer_t      *handle;
  snd_mixer_elem_t *elem;
  int               rc; 

/*------------------------------------------------------------------------*\
    Get info for this pcm channel 
\*------------------------------------------------------------------------*/
  snd_pcm_info_alloca( &pcmInfo );
  rc = snd_pcm_info( ifData->pcm, pcmInfo );
  if( rc<0 ) {
    logerr( "Alsa (%s): Could not get info for pcm device: %s", 
             aif->devName, snd_strerror(rc) );
    snd_pcm_close( ifData->pcm );
    Sfree( aif->ifData );
    return -1;
  }
  sprintf( cardName, "hw:%d", snd_pcm_info_get_card(pcmInfo) );

/*------------------------------------------------------------------------*\
    Get mixer 
\*------------------------------------------------------------------------*/
  rc = snd_mixer_open( &handle, 0 );
  if( rc<0 ) {
    logerr( "Alsa (%s): Could not open mixer: %s", 
            aif->devName, snd_strerror(rc) );
    return -1;
  }
  rc = snd_mixer_attach( handle, cardName );
  if( rc<0 ) {
    logerr( "Alsa (%s): Could not attach mixer: %s", 
            aif->devName, snd_strerror(rc) );
    return -1;
  }
  rc = snd_mixer_selem_register( handle, NULL, NULL );
  if( rc<0 ) {
    logerr( "Alsa (%s): Could not register mixer: %s", 
            aif->devName, snd_strerror(rc) );
    return -1;
  }
  rc = snd_mixer_load( handle );
  if( rc<0 ) {
    logerr( "Alsa (%s): Could not load mixer: %s", 
            aif->devName, snd_strerror(rc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Find channel for playback volume control 
\*------------------------------------------------------------------------*/
  for( elem=snd_mixer_first_elem(handle); elem; elem=snd_mixer_elem_next(elem) ) {
    snd_mixer_selem_id_t *sid;
	snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_get_id( elem, sid );
	DBGMSG( "Alsa (%s): Simple mixer control \"%s\",%i", aif->devName, 
            snd_mixer_selem_id_get_name(sid), snd_mixer_selem_id_get_index(sid) );

    // Not active?
	if( !snd_mixer_selem_is_active(elem) )
      continue;

    // No Playback volume?
    if( !snd_mixer_selem_has_playback_volume(elem) )
      continue;

    // Capture volume?
    if( snd_mixer_selem_has_capture_volume(elem) )
      continue;

    // Success...
    DBGMSG( "Alsa (%s): found mixer for card %s: %s", aif->devName, 
                cardName, snd_mixer_selem_id_get_name(sid) ); 
    break;
  }
  if( !elem )
    DBGMSG( "Alsa (%s): found no mixer for card %s.", aif->devName, cardName ); 

  // snd_mixer_close(handle);

/*------------------------------------------------------------------------*\
    That's it 
\*------------------------------------------------------------------------*/
  ifData->mixerElem = elem;
  return 0;
}


/*=========================================================================*\
    Close device
\*=========================================================================*/
static int _ifDelete( AudioIf *aif, AudioTermMode mode )
{
  AlsaData *ifData = (AlsaData*)aif->ifData;
  int       rc     = 0;
  
  DBGMSG( "Alsa: deleting interface" ); 

/*------------------------------------------------------------------------*\
    Stop current playback
\*------------------------------------------------------------------------*/
  if( aif->state==AudioIfRunning ) {
  	if( _ifStop(aif,mode) ) {
      logerr( "_ifDelete: Could not stop running playback." );
      rc = -1;
    }
  }

/*------------------------------------------------------------------------*\
    Close alsa pcm device
\*------------------------------------------------------------------------*/
  if( ifData->pcm )
    snd_pcm_close( ifData->pcm );

/*------------------------------------------------------------------------*\
    Get rid of auxiliary data
\*------------------------------------------------------------------------*/
  Sfree( aif->ifData );

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
      logerr( "_ifPlay: Could not stop running playback." );
      return -1;
    }
  }	

/*------------------------------------------------------------------------*\
    Check state
\*------------------------------------------------------------------------*/
  if( aif->state!=AudioIfInitialized ) {
    logerr( "_ifPlay: Cannot start playback, wrong state: %d", aif->state );
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
    logerr( "Unable to start audio backend thread: %s", strerror(rc) );
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
  AlsaData *ifData = (AlsaData*)aif->ifData;

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
    snd_pcm_drain( ifData->pcm );
  else
    snd_pcm_drop( ifData->pcm );
  
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
  AlsaData *ifData = (AlsaData*)aif->ifData;

  DBGMSG( "Alsa pause: %s", pause?"On":"Off" ); 

/*------------------------------------------------------------------------*\
    Set flag
\*------------------------------------------------------------------------*/
  int rc = snd_pcm_pause( ifData->pcm, pause?1:0 ); 
  if( rc<0 ) {
  	logerr( "Unable to pause alsa device: %s", aif->devName );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Set volume
\*=========================================================================*/
static int _ifSetVolume( AudioIf *aif, double volume, bool muted )
{
  AlsaData *ifData = (AlsaData*)aif->ifData;
  long      min, max;
  int       rc = 0;
  DBGMSG( "Alsa (%s): set volume to %.2lf%% %s", aif->devName, 
           volume*100, muted?"(muted)":"(unmuted)" ); 

/*------------------------------------------------------------------------*\
    Set muting
\*------------------------------------------------------------------------*/
  if( snd_mixer_selem_has_playback_switch(ifData->mixerElem) ) {
    DBGMSG( "Alsa (%s): using mixer switch for muting", aif->devName ); 
    rc = snd_mixer_selem_set_playback_switch_all( ifData->mixerElem, muted?0:1 );
    if( rc<0 ) {
      logerr( "Alsa (%s): Warning setting playback mixer switch: %s", 
              aif->devName, snd_strerror(rc) );
      if( muted )
        volume = 0;
    }
  }
  else if( muted )
    volume = 0;

/*------------------------------------------------------------------------*\
    Set volume
\*------------------------------------------------------------------------*/
  snd_mixer_selem_get_playback_volume_range( ifData->mixerElem, &min, &max);
  DBGMSG( "Alsa (%s): volume range [%ld,%ld], set: %ld", 
           aif->devName, min, max, min+(long)(volume*max)); 
  rc = snd_mixer_selem_set_playback_volume_all( ifData->mixerElem, min+(long)(volume*max) );
  if( rc<0 ) {
    logerr( "Alsa (%s): Error setting volume %ld [%ld,%ld]: %s", 
            aif->devName, min, max, min+(long)(volume*max), snd_strerror(rc) );
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
  AlsaData            *ifData = (AlsaData*)aif->ifData;
  snd_pcm_format_t     alsaFormat;
  snd_pcm_hw_params_t *hwParams;
  unsigned int         realRate;
  int                  rc;
  
  DBGMSG( "Alsa: setting format to %s", audioFormatStr(NULL,format) ); 

/*------------------------------------------------------------------------*\
    Do we know this format? 
\*------------------------------------------------------------------------*/
  alsaFormat = _getAlsaFormat( format );
  if( alsaFormat==SND_PCM_FORMAT_UNKNOWN ) {
  	logerr( "Unable get alsa format for: %s", audioFormatStr(NULL,format) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Collect hardware parameters on stack 
\*------------------------------------------------------------------------*/
  snd_pcm_hw_params_alloca( &hwParams );
  snd_pcm_hw_params_any( ifData->pcm, hwParams );

  // Get pause support
  aif->canPause = snd_pcm_hw_params_can_pause( hwParams );
  DBGMSG( "Alsa: pausing %ssupported", aif->canPause?"":"not " );

  // Multi channel is interleaved
  rc = snd_pcm_hw_params_set_channels( ifData->pcm, hwParams, format->channels );
  if( rc<0 ) {
  	logerr( "Unable to set alsa pcm hw parameter: channels (%d)",
                     format->channels );
    return -1;
  }
  if( format->channels>1 )
    rc = snd_pcm_hw_params_set_access( ifData->pcm, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED );
  if( rc<0 ) {
  	logerr( "Unable to set alsa pcm hw parameter: SND_PCM_ACCESS_RW_INTERLEAVED" );
    return -1;
  }

  // Set data format and calculate frame size (sample size*channels)
  rc = snd_pcm_hw_params_set_format( ifData->pcm, hwParams, alsaFormat );
  if( rc<0 ) {
  	logerr( "Unable to set alsa pcm hw format to: %s", audioFormatStr(NULL,format) );
    return -1;
  }
  aif->framesize = format->bitWidth/8*format->channels;

  // Sample rate 
  realRate = format->sampleRate;
  rc = snd_pcm_hw_params_set_rate_near( ifData->pcm, hwParams, &realRate, NULL);
  if( rc<0 ) {
  	logerr( "Unable to set alsa pcm hw parameter: rate (%d)", format->sampleRate );
    return -1;
  }
  if( realRate!=format->sampleRate ) {
    logerr( "alsa: device \"%s\" does not support %dfps (would be %dfps)",
                      aif->devName, format->sampleRate, realRate );
    return -1;
  }

  // Set number of periods to two
  rc = snd_pcm_hw_params_set_periods( ifData->pcm, hwParams, 2, 0 );
  if( rc<0 ) {
  	logerr( "Unable to set alsa pcm hw parameter: periods 2, 0" );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Apply the hardware parameters 
\*------------------------------------------------------------------------*/
  rc = snd_pcm_hw_params( ifData->pcm, hwParams );
  if( rc<0 ) {
    logerr( "Unable to set alsa pcm hw parameters: %s", snd_strerror(rc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Set software parameters 
\*------------------------------------------------------------------------*/  
  // buffer size 
  // fixme: snd_pcm_sw_params_set_avail_min(pcm_handle, sw_params, period_size);
  
/*------------------------------------------------------------------------*\
    Prepare interface 
\*------------------------------------------------------------------------*/
  rc = snd_pcm_prepare( ifData->pcm );
  if( rc<0 ) {
    logerr( "Unable to prepare alsa interface: %s", snd_strerror(rc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
       Translate audio format to ALSA standard 
\*=========================================================================*/
static snd_pcm_format_t _getAlsaFormat( const AudioFormat *format )
{
  DBGMSG( "_getAlsaFormat: %s", audioFormatStr(NULL,format) ); 
  bool le = true;

/*------------------------------------------------------------------------*\
    Relevant float formats  
\*------------------------------------------------------------------------*/
  if( format->isFloat ) {
    if( format->bitWidth==32 )
      return le ? SND_PCM_FORMAT_FLOAT_LE : SND_PCM_FORMAT_FLOAT_BE;
    if( format->bitWidth==64 )
      return le? SND_PCM_FORMAT_FLOAT64_LE : SND_PCM_FORMAT_FLOAT64_BE;
    return SND_PCM_FORMAT_UNKNOWN;
  }

/*------------------------------------------------------------------------*\
    Relevant signed formats  
\*------------------------------------------------------------------------*/
  if( format->isSigned ) {
    if( format->bitWidth==8 )
      return SND_PCM_FORMAT_S8;
    if( format->bitWidth==16 )
      return le ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_S16_BE;
    if( format->bitWidth==24 )
      return le ? SND_PCM_FORMAT_S24_LE : SND_PCM_FORMAT_S24_BE;
    if( format->bitWidth==32 )
      return le ? SND_PCM_FORMAT_S32_LE : SND_PCM_FORMAT_S32_BE;
    return SND_PCM_FORMAT_UNKNOWN;
  }

/*------------------------------------------------------------------------*\
    Relevant unsigned formats  
\*------------------------------------------------------------------------*/
  if( format->bitWidth==8 )
    return SND_PCM_FORMAT_U8;
  if( format->bitWidth==16 )
    return le ? SND_PCM_FORMAT_U16_LE : SND_PCM_FORMAT_U16_BE;
  if( format->bitWidth==24 )
    return le ? SND_PCM_FORMAT_U24_LE : SND_PCM_FORMAT_U24_BE;
  if( format->bitWidth==32 )
    return le ? SND_PCM_FORMAT_U32_LE : SND_PCM_FORMAT_U32_BE;

/*------------------------------------------------------------------------*\
    Not known  
\*------------------------------------------------------------------------*/
  return SND_PCM_FORMAT_UNKNOWN;
}


/*=========================================================================*\
       An audio backend thread 
\*=========================================================================*/
static void *_ifThread( void *arg )
{
  AudioIf  *aif    = (AudioIf*)arg; 
  AlsaData *ifData = (AlsaData*)aif->ifData;
  DBGMSG( "Alsa thread: starting." ); 
  
/*------------------------------------------------------------------------*\
    Thread main loop  
\*------------------------------------------------------------------------*/
  aif->state = AudioIfRunning;
  while( aif->state==AudioIfRunning ) {
    int    rc;
    
    // Wait (max 500ms) till data is actually needed
    //0: timedout  1: ready  <0: xrun, suspend or error  
    rc =  snd_pcm_wait( ifData->pcm, 500 );
    if( !rc ) {
      DBGMSG( "Alsa thread: timout while waiting for free buffer size." ); 
      continue;
    }
    if( rc==-EPIPE || rc==-ESTRPIPE ) {
      rc = snd_pcm_recover( ifData->pcm, rc, 0 ); 
      if( rc ) {
        logerr( "Alsa thread (after wait): Unable to recover alsa interface: %s", strerror(rc) );
        aif->state = AudioIfTerminatedError;
        break;
      } 
    }
    else if( rc<0 ) {
      logerr( "Alsa thread: Error while waiting for alsa interface: %s", strerror(rc) );
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
    snd_pcm_sframes_t frames_writable = snd_pcm_avail_update( ifData->pcm );
    snd_pcm_sframes_t frames_readable = fifoGetSize( aif->fifoIn, FifoNextReadable )/aif->framesize;
    snd_pcm_sframes_t frames = MIN( frames_writable, frames_readable );

    // Do transfer the data
    DBGMSG( "Alsa thread: writing %ld frames", (long)frames );
    rc = snd_pcm_writei( ifData->pcm, aif->fifoIn->readp, frames );		
    if( rc==-EPIPE || rc==-ESTRPIPE ){
      rc = snd_pcm_recover( ifData->pcm, rc, 0 ); 
      if( rc ) {
        logerr( "Alsa thread (after write): Unable to recover alsa interface: %s", strerror(rc) );
        aif->state = AudioIfTerminatedError;
        break;
      } 
    }
    else if( rc<0 ) {
      logerr( "Alsa thread: Error while writing data to alsa interface: %s", strerror(rc) );
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

