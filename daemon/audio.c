/*$*********************************************************************\

Name            : -

Source File     : audio.c

Description     : audio control 

Comments        : -

Called by       : ickstream protocol 

Calls           : 

Error Messages  : -
  
Date            : 20.02.2013

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "utils.h"
#include "codec.h"
#include "fifo.h"
#include "audio.h"

// Audio Backends
#ifdef ALSA
#include "alsa.h"
#endif

// Codecs
#ifdef MPG123
#include "codecMpg123.h"
#endif


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static AudioBackend *backendList;


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static int          _audioRegister( AudioBackend *backend );


/*=========================================================================*\
      Init audio module 
\*=========================================================================*/
int audioInit( const char *deviceName )
{

/*------------------------------------------------------------------------*\
    Register available audio backends
\*------------------------------------------------------------------------*/
#ifdef ALSA
  _audioRegister( alsaDescriptor() );
#endif

/*------------------------------------------------------------------------*\
    Register available codecs
\*------------------------------------------------------------------------*/
#ifdef MPG123
  codecRegister( mpg123Descriptor() );
#endif
  
/*------------------------------------------------------------------------*\
    That's all if no device name is given
\*------------------------------------------------------------------------*/
  if( !deviceName )
    return 0;
    
/*------------------------------------------------------------------------*\
    Else check existence of audio interface
\*------------------------------------------------------------------------*/
  return audioCheckDevice( deviceName );
}


/*=========================================================================*\
      Get linked list of audio backends.
        Do not modify or free!
\*=========================================================================*/
const AudioBackend *audioBackendsRoot( void )
{
  return backendList;	
}


/*=========================================================================*\
      Shutdown the audio module
\*=========================================================================*/
void audioShutdown( AudioTermMode mode )
{
  srvmsg( LOG_INFO, "Shutting down audio module...");
  
/*------------------------------------------------------------------------*\
    Shutdown all backends 
\*------------------------------------------------------------------------*/
  while( backendList ) {
  	AudioBackend *backend;
  	
  	// Unlink from list
  	backend = backendList;
  	backendList = backendList->next;

    // call optional shutdown method
    if( backend->shutdown )
      backend->shutdown( mode );
  }
  
/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
}

/*=========================================================================*\
      Get string description for audio format
\*=========================================================================*/
const char *audioFormatStr( AudioFormat *format )
{
  static char *buffer;
  	
  if( !buffer )
    buffer = malloc( 1024 );
    
  sprintf( buffer, "%d Hz, %d Ch", format->sampleRate, format->channels);
  
  return buffer;  	
}

/*=========================================================================*\
      Compare two audio formats
        returns 0 for equal formats
\*=========================================================================*/
int audioFormatCompare( AudioFormat *format1, AudioFormat *format2 )
{

  // Compare number of channels	
  if( format1->channels!=format2->channels )
    return 1;

  // Compare sample rate	
  if( format1->sampleRate!=format2->sampleRate)
    return 2;
    
  // Equal!	
  return 0;
}



/*=========================================================================*\
      Get List of available devices for an backend
        returns length of device list or -1 on error
        deviceListPtr is returned as NULL terminated array of device names
\*=========================================================================*/
int audioGetDeviceList( const AudioBackend *backend, char ***deviceListPtr, char ***descrListPtr )
{
  return backend->getDevices( deviceListPtr, descrListPtr );	
}


/*=========================================================================*\
      Free list of strings as received from audioGetDeviceList
\*=========================================================================*/
void audioFreeStringList( char **stringList )
{
  char **ptr;

/*------------------------------------------------------------------------*\
    Loop over elements 
\*------------------------------------------------------------------------*/
  for( ptr=stringList; ptr && *ptr; ptr++ )
    Sfree( *ptr );  		
    
/*------------------------------------------------------------------------*\
    Free the list itself 
\*------------------------------------------------------------------------*/
  Sfree( stringList );  
}


/*=========================================================================*\
      Use a device - A convenience wrapper for selecting backend and setting up an audio interface
\*=========================================================================*/
int audioCheckDevice( const char *deviceName )
{
  const AudioBackend  *backend;
  const char          *dev = NULL;
  char               **deviceList;
  char               **ptr;
  int                  retval;

/*------------------------------------------------------------------------*\
    Get backend name 
\*------------------------------------------------------------------------*/
  backend = audioBackendByDeviceString( deviceName, &dev );
  if( !backend ) {
  	srvmsg( LOG_INFO, "Device not available: \"%s\"", deviceName );
  	return -1;
  }
  
/*------------------------------------------------------------------------*\
    Check if device is actually available 
\*------------------------------------------------------------------------*/
  retval = audioGetDeviceList( backend, &deviceList, NULL );
  if( retval<0 )
    return -1;
  for( ptr=deviceList; ptr && *ptr; ptr++ )
    if( !strcmp(*ptr,deviceName) )
      break;
  audioFreeStringList( deviceList );   
  if( !ptr ) {   // dangling, but not dereferenced...
    srvmsg( LOG_INFO, "Device not available: \"%s\"", deviceName );
    return -1;
  }    
  
/*------------------------------------------------------------------------*\
    That's all: Device found 
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Create a new output instance (called interface) for backend  
\*=========================================================================*/
AudioIf *audioIfNew( const AudioBackend *backend, const char *device )
{
  AudioIf *aif;  
  DBGMSG( "Creating new instance of audio backend %s", backend->name );

/*------------------------------------------------------------------------*\
    Create header 
\*------------------------------------------------------------------------*/
  aif = calloc( 1, sizeof(AudioIf) );
  if( !aif ) {
    srvmsg( LOG_ERR, "audioIfNew: out of memeory!" );
    return NULL;
  }
  aif->state   = AudioIfInitialized;
  aif->backend = backend;
  aif->devName = strdup( device );
    
/*------------------------------------------------------------------------*\
    Initialize instance 
\*------------------------------------------------------------------------*/
  if( backend->newIf(aif,device) ) {	
    srvmsg( LOG_ERR, "%s: Unable to init audio interface: %s", backend->name, device );
    Sfree( aif->devName );
    Sfree( aif );
    return NULL;
  }
  
/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
  return aif;
}


/*=========================================================================*\
      Shut down an output instance (called interface) for backend
\*=========================================================================*/
int audioIfDelete( AudioIf *aif, AudioTermMode mode )
{
  int rc = aif->backend->deleteIf( aif, mode );

  if( aif->fifoIn )
    fifoDelete( aif->fifoIn );
  Sfree( aif->devName );
  Sfree( aif );
  return rc;
}


/*=========================================================================*\
      Play a fifo on an interface
        the interface is (re)initialized, playing or paused data is dropped
        returns the input fifo or NULL on error
\*=========================================================================*/
Fifo *audioIfPlay( AudioIf *aif, AudioFormat *format )
{
    
  // Reset or create fifo
  if( aif->fifoIn )
    fifoReset( aif->fifoIn );
  else
    aif->fifoIn = fifoCreate( aif->devName, AudioFifoDefaultSize );
  if( !aif->fifoIn ) {
    srvmsg( LOG_ERR, "audioIfPlay: could not create fifo" );
    return NULL;
  }   
  
  // Already playing and no format change?
  if( aif->state==AudioIfRunning && !audioFormatCompare(format,&aif->format) )
    return aif->fifoIn;

  // Call backend
  if( aif->backend->play(aif,format) ) 
    return NULL;
    
  // Store new format return pointer to fifo
  memcpy( &aif->format, format, sizeof(AudioFormat) ); 
  return aif->fifoIn;
}


/*=========================================================================*\
      Stop playback on an interface, deteach queue 
\*=========================================================================*/
int audioIfStop( AudioIf *aif, AudioTermMode mode )
{
  return aif->backend->stop( aif, mode );	
}


/*=========================================================================*\
      Set pause mode 
\*=========================================================================*/
int audioIfSetPause( AudioIf *aif, bool pause )
{
	
  // Function not supported?
  if( !aif->backend->pause ) {
  	srvmsg( LOG_WARNING, "audioIfSetPause: Backend %s does not support pausing.",
                         aif->backend->name );
  	return -1;
  } 	
  if( !aif->canPause ) {
  	srvmsg( LOG_WARNING, "audioIfSetPause: Device %s does not support pausing.",
                         aif->devName );
  	return -1;
  } 	
	
  // dispatch to backend function
  return aif->backend->pause( aif, pause );	
}


/*=========================================================================*\
      Register an audio backend
\*=========================================================================*/
static int _audioRegister( AudioBackend *backend )
{
  srvmsg( LOG_INFO, "Registering audio backend %s", backend->name );
  
  // Call optional init method
  if( backend->init && backend->init() ) {
    srvmsg( LOG_ERR, "Could not init audio backend: %s", backend->name );
    return -1;
  }
  
  // Link to list
  backend->next = backendList;
  backendList   = backend;	
  
  // That's all
  return 0;
}


/*=========================================================================*\
      Get backend and device name from combined string (<backendname>:<device>)
        returns NULL if backend is not found.
        The device itself is not tested!
\*=========================================================================*/
const AudioBackend *audioBackendByDeviceString( const char *str, const char **device )
{
  AudioBackend *backend;
  
/*------------------------------------------------------------------------*\
    Loop over all backends
\*------------------------------------------------------------------------*/
  for( backend=backendList; backend; backend=backend->next )
  	if( !strncmp(backend->name,str,strlen(backend->name)) )
  	  break;
    
/*------------------------------------------------------------------------*\
    Check for terminating ':'
\*------------------------------------------------------------------------*/
  if( !backend || str[strlen(backend->name)]!=':' )
    backend = NULL;

/*------------------------------------------------------------------------*\
    Set pointer to device
\*------------------------------------------------------------------------*/
  if( backend && device )
    *device = str + strlen(backend->name) +1;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return backend;	
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
