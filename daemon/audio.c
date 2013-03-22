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
#include <ctype.h>
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
  loginfo( "Shutting down audio module...");
  
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
      Get backend and device name from combined string (<backendname>:<device>)
        returns NULL if backend is not found.
        The device itself is not tested!
\*=========================================================================*/
const AudioBackend *audioBackendByDeviceString( const char *str, const char **device )
{
  AudioBackend *backend;
  DBGMSG( "Get Backend for device string: %s", str );

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
      Get string description for audio format
        Format: "<chan>x<rate>x<bitwidth><S|U|F>
        returns buffer or - if buffer is NULL - a pointer to a local buffer, 
        this (buffer==NULL) is a NON reentrant usage!
\*=========================================================================*/
const char *audioFormatStr( char *buffer, const AudioFormat *format )
{
  static char *locBuffer;
         char  enc=0;
	
/*------------------------------------------------------------------------*\
    Need loacal buffer?
\*------------------------------------------------------------------------*/
  if( !buffer && !locBuffer)
    locBuffer = malloc( 64 );
  if( !buffer )
    buffer = locBuffer;

/*------------------------------------------------------------------------*\
    Get encoding style
\*------------------------------------------------------------------------*/
  if( format->bitWidth<=0 )
    enc = 0;
  else if( format->isSigned && !format->isFloat )
    enc = 'S';
  else if( !format->isSigned && !format->isFloat )
    enc = 'U';
  else if( format->isSigned && format->isFloat )
    enc = 'F';
  else if( !format->isSigned && format->isFloat ) {
    logerr( "Corrupt audio format: unsigned float." );
    enc = '?';
  }

/*------------------------------------------------------------------------*\
    Construct and return result
\*------------------------------------------------------------------------*/
  sprintf( buffer, "%dx%dx%d%c", format->channels, format->sampleRate, 
                                  format->bitWidth, enc );
  return buffer;  	
}


/*=========================================================================*\
      Get format from string
        returns -1 on syntax error
\*=========================================================================*/
int audioStrFormat( AudioFormat *format, const char *str )
{
  int  n, ch, sr, bw;
  char enc = 0;

/*------------------------------------------------------------------------*\
    Parse string
\*------------------------------------------------------------------------*/
  n = sscanf( str, "%dx%dx%d%c", &ch, &sr, &bw, &enc ); 
  if( n<3 ) {
    logwarn( "syntax error in audio format: \"%s\" (to few fields)", str );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Need encoding spec if bitwidth is defined
\*------------------------------------------------------------------------*/
  if( bw>0 ) {
    if( !strchr("sSuUfF",enc) ) {
      logwarn( "syntax error in audio format: \"%s\" (missing S|U|F)", str );
      return -1;
    }
    switch( toupper(enc) ) {
      case 'S': format->isSigned=true;  format->isFloat=false; break; 
      case 'U': format->isSigned=false; format->isFloat=false; break; 
      case 'F': format->isSigned=true;  format->isFloat=true;  break; 
    }
  }
  
/*------------------------------------------------------------------------*\
    Get channels, samplerates and bitwidth
\*------------------------------------------------------------------------*/
  format->channels   = ch>0 ? ch : 0;
  format->sampleRate = sr>0 ? sr : 0;
  format->bitWidth   = bw>0 ? bw : 0;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;  	
}


/*=========================================================================*\
      Compare two audio formats
        returns 0 for equal formats
\*=========================================================================*/
int audioFormatCompare( const AudioFormat *format1, const AudioFormat *format2 )
{
#ifdef DEBUG
  char buf1[64], buf2[64];
  DBGMSG( "compare formats: %s ?= %s", 
          audioFormatStr(buf1,format1), audioFormatStr(buf2,format2) );
#endif

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
      Check if audio format is complete
\*=========================================================================*/
bool audioFormatIsComplete( const AudioFormat *format)
{
  DBGMSG( "Format complete?: %s", audioFormatStr(NULL,format) );

  // Number of channels defined?
  if( format->channels<=0 )
    return false;

  // Sample rate defined?
  if( format->sampleRate<=0 )
    return false;

  // bit witdh defined?
  if( format->bitWidth<=0 )
    return false;

  // Is complete	
  return true;

}


/*=========================================================================*\
      Complete an audio format destFormat with data from refFormat
        returns -1 on error (destFormat still incomplete)
                 1 if destFormat was already complete
                 0 if destFormat was actually completed
\*=========================================================================*/
int audioFormatComplete( AudioFormat *destFormat, const AudioFormat *refFormat )
{
  int rc = 1;

#ifdef DEBUG
  char buf1[64], buf2[64];
  DBGMSG( "complete format %s with %s", 
          audioFormatStr(buf1,destFormat), audioFormatStr(buf2,refFormat) );
#endif

  // Number of channels defined?
  if( destFormat->channels<=0 ) {
    destFormat->channels = refFormat->channels;
    rc = 0;
  }

  // Sample rate defined?
  if( destFormat->sampleRate<=0 ){
    destFormat->sampleRate = refFormat->sampleRate;
    rc = 0;
  }

  // Bit witdh defined?
  if( destFormat->bitWidth<=0 ){
    destFormat->bitWidth = refFormat->bitWidth;
    destFormat->isSigned = refFormat->isSigned;
    destFormat->isFloat  = refFormat->isFloat;
    rc = 0;
  }

  // Resulting format still incomplete?
  if( !audioFormatIsComplete(destFormat) )
    rc = -1;

  // return result code
  return rc;
}


/*=========================================================================*\
      Append a format to format list.
        format is copied.
\*=========================================================================*/
int audioAddAudioFormat( AudioFormatList *list, const AudioFormat *format )
{
  DBGMSG( "Adding audio format to list %p: %s", list,
          format?audioFormatStr(NULL,format):"<none>" );

/*------------------------------------------------------------------------*\
    Create new list element
\*------------------------------------------------------------------------*/
  AudioFormatElement *newElement = calloc( 1, sizeof(AudioFormatElement) );
  if( !newElement ) {
     logerr( "playerAddDefaultAudioFormat: out of memory" );
    return -1;
  }
  memcpy( &newElement->format, format, sizeof(AudioFormat) );

/*------------------------------------------------------------------------*\
    Append format to list
\*------------------------------------------------------------------------*/
  if( !*list ) {
    *list = newElement;
  }
  else {
    AudioFormatElement *element = *list;
    while( element->next )
      element = element->next;
    element->next = newElement;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}

/*=========================================================================*\
     Free a list of audio formats.
\*=========================================================================*/
void audioFreeAudioFormatList( AudioFormatList *list )
{
  DBGMSG( "Deleting audio format list %p.", list ); 

/*------------------------------------------------------------------------*\
    Delete list elements
\*------------------------------------------------------------------------*/
  AudioFormatElement *element = *list;
  while( element ) {
    AudioFormatElement *next = element->next;
    Sfree( element );
    element = next;
  }

/*------------------------------------------------------------------------*\
    Reset root
\*------------------------------------------------------------------------*/
  *list = NULL;
}

/*=========================================================================*\
      Get List of available devices for an backend
        returns length of device list or -1 on error
        deviceListPtr is returned as NULL terminated array of device names
\*=========================================================================*/
int audioGetDeviceList( const AudioBackend *backend, char ***deviceListPtr, char ***descrListPtr )
{
  DBGMSG( "Get device list for audio backend %s", backend->name );
  return backend->getDevices( deviceListPtr, descrListPtr );	
}


/*=========================================================================*\
      Free list of strings as received from audioGetDeviceList
\*=========================================================================*/
void audioFreeStringList( char **stringList )
{
  char **ptr;
  DBGMSG( "free list of strings (%p)", stringList );

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
  DBGMSG( "Check for audio device: %s", deviceName );

/*------------------------------------------------------------------------*\
    Get backend name 
\*------------------------------------------------------------------------*/
  backend = audioBackendByDeviceString( deviceName, &dev );
  if( !backend ) {
  	loginfo( "Device not available: \"%s\"", deviceName );
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
    loginfo( "Device not available: \"%s\"", deviceName );
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
    logerr( "audioIfNew: out of memeory!" );
    return NULL;
  }
  aif->state   = AudioIfInitialized;
  aif->backend = backend;
  aif->devName = strdup( device );
    
/*------------------------------------------------------------------------*\
    Initialize instance 
\*------------------------------------------------------------------------*/
  if( backend->newIf(aif,device) ) {	
    logerr( "%s: Unable to init audio interface: %s", backend->name, device );
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
  int rc;

  DBGMSG( "Deleting instance of audio backend %s: %p (mode %d)", 
           aif->backend->name, aif, mode );

  rc = aif->backend->deleteIf( aif, mode );

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
  DBGMSG( "Start playback on audio instance %s (%p): %s", 
           aif->backend->name, aif, audioFormatStr(NULL,format) );
    
  // Reset or create fifo
  if( aif->fifoIn )
    fifoReset( aif->fifoIn );
  else
    aif->fifoIn = fifoCreate( aif->devName, AudioFifoDefaultSize );
  if( !aif->fifoIn ) {
    logerr( "audioIfPlay: could not create fifo" );
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
  DBGMSG( "Stop playback on audio instance %s (%p): mode %d", 
           aif->backend->name, aif, mode );

  return aif->backend->stop( aif, mode );	
}


/*=========================================================================*\
      Set pause mode 
\*=========================================================================*/
int audioIfSetPause( AudioIf *aif, bool pause )
{
  DBGMSG( "Set pause state on audio instance %s (%p): %s", 
           aif->backend->name, aif, pause?"On":"Off" );
	
  // Function not supported?
  if( !aif->backend->pause ) {
  	logwarn( "audioIfSetPause: Backend %s does not support pausing.",
                         aif->backend->name );
  	return -1;
  } 	
  if( !aif->canPause ) {
  	logwarn( "audioIfSetPause: Device %s does not support pausing.",
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
  loginfo( "Registering audio backend %s", backend->name );
  
  // Call optional init method
  if( backend->init && backend->init() ) {
    logerr( "Could not init audio backend: %s", backend->name );
    return -1;
  }
  
  // Link to list
  backend->next = backendList;
  backendList   = backend;	
  
  // That's all
  return 0;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/



