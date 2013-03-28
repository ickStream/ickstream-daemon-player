/*$*********************************************************************\

Name            : -

Source File     : audioNull.c

Description     : interface to null audio backand 

Comments        : -

Called by       : audio module 

Calls           : 

Error Messages  : -
  
Date            : 22.03.2013

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

#undef ICK_DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>

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
static int    _ifNew( AudioIf *aif ); 
static int    _ifDelete( AudioIf *aif, AudioTermMode mode ); 
static int    _ifPlay( AudioIf *aif, AudioFormat *format );
static int    _ifStop( AudioIf *aif, AudioTermMode mode );

static void  *_ifThread( void *arg );


/*=========================================================================*\
      Return descriptor for this backend 
\*=========================================================================*/
AudioBackend *audioNullDescriptor( void )
{
  static AudioBackend backend;
  
  // Set name	
  backend.next           = NULL;
  backend.name           = "null";
  backend.init           = NULL;     // optional
  backend.shutdown       = NULL;     // optional
  backend.getDevices     = &_backendGetDeviceList;
  backend.newIf          = &_ifNew; 
  backend.deleteIf       = &_ifDelete;
  backend.play           = &_ifPlay;
  backend.stop           = &_ifStop;
  backend.pause          = NULL;

  return &backend;	
}


/*=========================================================================*\
      Get all backend devices
        descrListPtr might be NULL;
\*=========================================================================*/
static int _backendGetDeviceList( char ***deviceListPtr, char ***descrListPtr )
{
  
/*------------------------------------------------------------------------*\
    only one device
\*------------------------------------------------------------------------*/
  *deviceListPtr = calloc( 2, sizeof(void*) );
  **deviceListPtr = NULL;
  if( descrListPtr ) {
    *descrListPtr  = calloc( 2, sizeof(void*) );
    **descrListPtr  = strdup("Audio null device (fast motion)");
  }
  
/*------------------------------------------------------------------------*\
    return number of devices found with name
\*------------------------------------------------------------------------*/
  return 1;
}


/*=========================================================================*\
    Open device and start thread
\*=========================================================================*/
static int _ifNew( AudioIf *aif )
{
  DBGMSG( "Audio Null (%s): new interface", aif->devName ); 

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
  DBGMSG( "Audio Null (%s): deleting interface", aif->devName ); 

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Attach a queue to device and start playing
\*=========================================================================*/
static int _ifPlay( AudioIf *aif, AudioFormat *format )
{
  DBGMSG( "Audio Null (%s): start playback", aif->devName ); 

/*------------------------------------------------------------------------*\
    Stop current playback
\*------------------------------------------------------------------------*/
  if( aif->state==AudioIfRunning ) {
  	if( _ifStop(aif,AudioDrop) ) {
      logerr( "Audio Null (%s): Could not stop running playback.", aif->devName );
      return -1;
    }
  }	

/*------------------------------------------------------------------------*\
    Check state
\*------------------------------------------------------------------------*/
  if( aif->state!=AudioIfInitialized ) {
    logerr( "Audio Null (%s): Cannot start playback, wrong state: %d", aif->devName, aif->state );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Fire up working thread
\*------------------------------------------------------------------------*/
  int rc = pthread_create( &aif->thread, NULL, _ifThread, aif );
  if( rc ) {
    logerr( "Audio Null (%s): Unable to start audio backend thread: %s", aif->devName, strerror(rc) );
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
  DBGMSG( "Audio Null (%s): %d", aif->devName, mode ); 

/*------------------------------------------------------------------------*\
    reset state to initialized, that's it
\*------------------------------------------------------------------------*/
  aif->state = AudioIfInitialized;
  return 0;
}


/*=========================================================================*\
       An audio backend thread 
\*=========================================================================*/
static void *_ifThread( void *arg )
{
  AudioIf *aif     = (AudioIf*)arg; 

  DBGMSG( "Audio Null thread: starting." ); 
  
/*------------------------------------------------------------------------*\
    Thread main loop  
\*------------------------------------------------------------------------*/
  aif->state = AudioIfRunning;
  while( aif->state==AudioIfRunning ) {
    int    rc;
        
    // Wait for data in fifo
    rc = fifoLockWaitReadable( aif->fifoIn, 500 );
    if( rc==ETIMEDOUT ) {
      DBGMSG( "Audio Null thread: timout while waiting for fifo data." );	
  	  continue;
  	}
    if( rc ) {
      DBGMSG( "Audio Null thread: wait for fifo error, terminating: %s", strerror(rc) );
  	  aif->state = AudioIfTerminatedError;
  	  break; 	
    }
    
    // How much data can be delivered
    size_t bytes_readable = fifoGetSize( aif->fifoIn, FifoNextReadable );

    // Do transfer the data
    DBGMSG( "Audio Null thread: consuming %ld frames", (long)bytes_readable );

    // Check out accepted data from fifo
    fifoUnlockAfterRead( aif->fifoIn,bytes_readable ); 
  	
  }  // End of: Thread main loop
   
/*------------------------------------------------------------------------*\
    Fulfilled external termination request without error...  
\*------------------------------------------------------------------------*/
  DBGMSG( "Audio Null thread: terminating due to state %d", aif->state );
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

