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
#include <pthread.h>
#include <sys/time.h>


#include "ickutils.h"
#include "codec.h"
#include "fifo.h"
#include "audio.h"

// Audio Backends
#include "audioNull.h"
#ifdef ICK_ALSA
#include "audioAlsa.h"
#endif
#ifdef ICK_PULSEAUDIO
#include "audioPulse.h"
#endif

// Codecs
#ifdef ICK_MPG123
#include "codecMpg123.h"
#endif
#ifdef ICK_FLAC
#include "codecFlac.h"
#endif
#ifdef ICK_PCM
#include "codecPCM.h"
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
#ifdef ICK_PULSEAUDIO
  _audioRegister( audioPulseDescriptor() );
#endif
#ifdef ICK_ALSA
  _audioRegister( audioAlsaDescriptor() );
#endif
_audioRegister( audioNullDescriptor() );

/*------------------------------------------------------------------------*\
    Register available codecs
\*------------------------------------------------------------------------*/
#ifdef ICK_MPG123
  codecRegister( mpg123Descriptor() );
#endif
  
#ifdef ICK_FLAC
  codecRegister( flacDescriptor() );
#endif

#ifdef ICK_PCM
  codecRegister( pcmDescriptor() );
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
  if( !backend || (str[strlen(backend->name)]!=':' && str[strlen(backend->name)]!=0) )
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
    logwarn( "Syntax error in audio format: \"%s\" (to few fields)", str );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Need encoding spec if bitwidth is defined
\*------------------------------------------------------------------------*/
  if( bw>0 ) {
    if( !strchr("sSuUfF",enc) ) {
      logwarn( "Syntax error in audio format: \"%s\" (missing S|U|F)", str );
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
#ifdef ICK_DEBUG
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

#ifdef ICK_DEBUG
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
  DBGMSG( "Free list of strings (%p)", stringList );

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
      Use a device - A convenience wrapper for selecting backend and
                     setting up an audio interface
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
AudioIf *audioIfNew( const AudioBackend *backend, const char *device, size_t fifoSize )
{
  AudioIf *aif;  

/*------------------------------------------------------------------------*\
    Create and initialize header
\*------------------------------------------------------------------------*/
  aif = calloc( 1, sizeof(AudioIf) );
  if( !aif ) {
    logerr( "audioIfNew (%s): out of memory!", backend->name );
    return NULL;
  }
  aif->state     = AudioIfInitialized;
  aif->backend   = backend;
  aif->devName   = strdup( device );
  aif->canPause  = false;
  aif->hasVolume = false;
  aif->volume    = 0.0;
  aif->muted     = false;
  DBGMSG( "Audio instance %s (%p): created", aif->backend->name, aif );

/*------------------------------------------------------------------------*\
    Create input fifo
\*------------------------------------------------------------------------*/
  aif->fifoIn = fifoCreate( aif->devName, fifoSize );
  if( !aif->fifoIn ) {
    logerr( "audioIfNew (%s): Unable to create fifo (%ld bytes)",
            backend->name, (long)fifoSize );
    Sfree( aif->devName );
    Sfree( aif );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Init mutex and conditions
\*------------------------------------------------------------------------*/
  ickMutexInit( &aif->mutex );
  pthread_cond_init( &aif->condIsReady, NULL );

/*------------------------------------------------------------------------*\
    Initialize instance 
\*------------------------------------------------------------------------*/
  if( backend->newIf(aif) ) {	
    logerr( "audioIfNew (%s): Unable to init audio interface \"%s\"",
            backend->name, device );
    fifoDelete( aif->fifoIn );
    pthread_mutex_destroy( &aif->mutex );
    pthread_cond_destroy( &aif->condIsReady );
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

  DBGMSG( "Audio instance (%p,%s): Deleting (mode %d).",
           aif, aif->backend->name, mode );

/*------------------------------------------------------------------------*\
    Call instance destructor
\*------------------------------------------------------------------------*/
  rc = aif->backend->deleteIf( aif, mode );

/*------------------------------------------------------------------------*\
    Delete fifo
\*------------------------------------------------------------------------*/
  if( aif->fifoIn )
    fifoDelete( aif->fifoIn );

/*------------------------------------------------------------------------*\
    Delete mutex and conditions
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &aif->mutex );
  pthread_cond_destroy( &aif->condIsReady );

/*------------------------------------------------------------------------*\
    Free header data and return
\*------------------------------------------------------------------------*/
  Sfree( aif->devName );
  Sfree( aif );
  return rc;
}


/*=========================================================================*\
    Wait till an audio device is initialized, i.e ready to play
      timeout is in ms, 0 or a negative values are treated as infinity
      blocks and returns 0 if device is ready
                 or std. errcode (ETIMEDOUT in case of timeout) otherwise
\*=========================================================================*/
int audioIfWaitForInit( AudioIf *aif, int timeout )
{
  struct timeval  now;
  struct timespec abstime;
  int             err = 0;

  DBGMSG( "Audio instance (%p,%s): Wait for init (timeout %dms).",
           aif, aif->backend->name, timeout );

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &aif->mutex );

/*------------------------------------------------------------------------*\
    Get absolute timestamp for timeout
\*------------------------------------------------------------------------*/
  if( timeout>0 ) {
    gettimeofday( &now, NULL );
    abstime.tv_sec  = now.tv_sec + timeout/1000;
    abstime.tv_nsec = now.tv_usec*1000UL +(timeout%1000)*1000UL*1000UL;
    if( abstime.tv_nsec>1000UL*1000UL*1000UL ) {
      abstime.tv_nsec -= 1000UL*1000UL*1000UL;
      abstime.tv_sec++;
    }
  }

/*------------------------------------------------------------------------*\
    Loop while condition is not met (cope with "spurious  wakeups")
\*------------------------------------------------------------------------*/
  while( aif->state<AudioIfRunning ) {

    // wait for condition
    err = timeout>0 ? pthread_cond_timedwait( &aif->condIsReady, &aif->mutex, &abstime )
                    : pthread_cond_wait( &aif->condIsReady, &aif->mutex );

    // Break on errors
    if( err )
      break;
  }

/*------------------------------------------------------------------------*\
    Unlock mutex
\*------------------------------------------------------------------------*/
  if( !err)
    pthread_mutex_unlock( &aif->mutex );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  DBGMSG( "Audio instance (%p,%s): Waited for init (%s).",
           aif, aif->backend->name, strerror(err) );
  return err;
}


/*=========================================================================*\
      Start output with a defined format on an interface
        the interface is (re)initialized,
        playing or paused data in fifo is dropped
        returns the input fifo or NULL on error
\*=========================================================================*/
int audioIfPlay( AudioIf *aif, AudioFormat *format, AudioTermMode mode )
{
  DBGMSG( "Audio instance (%p,%s): Start playback (format %s)",
           aif, aif->backend->name, audioFormatStr(NULL,format) );

  // Output is already active
  if( aif->state==AudioIfRunning ) {

    // Do nothing if draining and no format change
    if( mode==AudioDrain && !audioFormatCompare(format,&aif->format) )
      return 0;

    // Else stop current playback
    if( audioIfStop(aif,mode) )
      return -1;
  }

  // Call backend to set format and start output
  if( aif->backend->play(aif,format) ) 
    return -1;

  // Store new format; that's all
  memcpy( &aif->format, format, sizeof(AudioFormat) ); 
  return 0;
}


/*=========================================================================*\
      Stop playback on an interface, deteach queue 
\*=========================================================================*/
int audioIfStop( AudioIf *aif, AudioTermMode mode )
{
  int rc;
  DBGMSG( "audio instance (%p,%s): Stop playback (mode %d).",
          aif, aif->backend->name, mode );

  // In draining mode wait for fifo to fall dry
  if( mode==AudioDrain ) {
    rc = fifoLockWaitDrained( aif->fifoIn, 10000 );
    if( rc )
      return -1;
    fifoUnlock( aif->fifoIn );
  }

  // Otherwise simply reset fifo
  else
    fifoReset( aif->fifoIn );

  // Call backend function
  rc = aif->backend->stop( aif, mode );

  // That's all
  return rc;
}


/*=========================================================================*\
      Set pause mode 
\*=========================================================================*/
int audioIfSetPause( AudioIf *aif, bool pause )
{
  DBGMSG( "Audio instance (%p,%s): Set pause %s.",
           aif, aif->backend->name, pause?"On":"Off" );

  // Function not supported?
  if( !aif->backend->pause ) {
    logwarn( "audioIfSetPause (%s): Backend does not support pausing.",
              aif->backend->name );
    return -1;
  }
  if( !audioIfSupportsPause(aif) ) {
    logwarn( "audioIfSetPause (%s): Device %s does not support pausing.",
              aif->backend->name, aif->devName );
    return -1;
  }

  // dispatch to backend function
  return aif->backend->pause( aif, pause );	
}


/*=========================================================================*\
      Set volume
\*=========================================================================*/
int audioIfSetVolume( AudioIf *aif, double volume, bool muted )
{
  DBGMSG( "Audio instance (%p,%s): Set volume %f %s.",
          aif, aif->backend->name, volume, muted?"(muted)":"(unmuted)" );

  // Function not supported?
  if( !aif->backend->setVolume ) {
    logwarn( "audioIfSetVolume (%s): Backend does not support volume control.",
             aif->backend->name );
    return -1;
  }
  if( !audioIfSupportsVolume(aif) ) {
    logwarn( "audioIfSetPause (%s): Device %s does not support volume control.",
             aif->backend->name, aif->devName );
    return -1;
  }

  // dispatch to backend function
  return aif->backend->setVolume( aif, volume, muted );	
}


/*=========================================================================*\
      Register an audio backend
\*=========================================================================*/
static int _audioRegister( AudioBackend *backend )
{
  loginfo( "Registering audio backend %s", backend->name );
  
  // Call optional init method
  if( backend->init && backend->init() ) {
    logerr( "Audio backend (%s): Could not init.", backend->name );
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



