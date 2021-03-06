/*$*********************************************************************\

Name            : -

Source File     : codec.c

Description     : manage audio codecs

Comments        : -

Called by       : audio and feeder module 

Calls           : 

Error Messages  : -
  
Date            : 02.03.2013

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

// #undef ICK_DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>


#include "ickutils.h"
#include "audio.h"
#include "codec.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static Codec *codecList;


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static void *_codecThread( void *arg );


/*=========================================================================*\
      Register and init an codec 
\*=========================================================================*/
int codecRegister( Codec *codec )
{

  // Call optional init method
  if( codec->init && codec->init(codec) ) {
    loginfo( "codecRegister (%s): Could not init codec.", codec->name );
    return -1;
  }

  // Link to list
  codec->next = codecList;
  codecList   = codec;	

  // That's all
  loginfo( "codecRegister (%s): Ok.", codec->name );
  return 0;
}


/*=========================================================================*\
      Shutdown all codecs 
\*=========================================================================*/
void codecShutdown( bool force )
{
  Codec *codec; 
  DBGMSG( "codecShutdown: %s.", force?"force":"wait" );

/*------------------------------------------------------------------------*\
    Loop over all codecs 
\*------------------------------------------------------------------------*/
  while( codecList ) {

    // Unlink from list
    codec = codecList;
    codecList = codecList->next;

    // call optional shutdown method
    if( codec->shutdown )
      codec->shutdown( codec, force );
  }

/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
      Find a codec for type and format
        for initial call codec shall be NULL
        supply codec with previous result to get next match
      Returns a matching codec
        or NULL if none was found
      For an incomplete format only the defined components are checked.
\*=========================================================================*/
Codec *codecFind( const char *type, AudioFormat *format, Codec *codec )
{
  DBGMSG( "codecFind: %s, %s.", type, audioFormatStr(NULL,format) );

/*------------------------------------------------------------------------*\
    First call? 
\*------------------------------------------------------------------------*/
  if( !codec )
    codec = codecList;

/*------------------------------------------------------------------------*\
    Loop over all remaining list elements 
\*------------------------------------------------------------------------*/
  for( ; codec; codec = codec->next ) {

    // Check codec (format might be incomplete here!)
    DBGMSG( "codecFind: Checking codec %s for type %s, format %s.",
            codec->name, type, audioFormatStr(NULL,format) );
    if( codec->checkType(type,format) )
      break;

  } // for( ; codec; codec = codec->next )

/*------------------------------------------------------------------------*\
    Return result 
\*------------------------------------------------------------------------*/
  DBGMSG( "codecFind (%s,%s): using %s",
           type, audioFormatStr(NULL,format), codec?codec->name:"<none found>" );
  return codec;
}


/*=========================================================================*\
    Create a new instance of a codec
       fifo        - the output link to the audio backend (PCM format)
       type        - the data type (MIME)
       format      - the preferred output format
       fd          - file descriptor for input data (close on EOF!)
    Returns NULL on error
\*=========================================================================*/
CodecInstance *codecNewInstance( const Codec *codec, const char *type, const AudioFormat *format, int fd, Fifo *fifo )
{
  CodecInstance       *instance;

  DBGMSG( "codecNewInstance (%s): creating new instance for type \"%s\".", codec->name, type );

/*------------------------------------------------------------------------*\
    Create header 
\*------------------------------------------------------------------------*/
  instance = calloc( 1, sizeof(CodecInstance) );
  if( !instance ) {
    logerr( "codecNewInstance (%s): Out of memory!", codec->name );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Initialize with parameters
\*------------------------------------------------------------------------*/
  instance->state       = CodecInitialized;
  instance->codec       = codec;
  instance->fifoOut     = fifo;
  instance->fdIn        = fd;
  memcpy( &instance->format, format, sizeof(AudioFormat) );
  instance->type        = strdup( type );
  if( !instance->type ) {
    Sfree( instance );
    logerr( "codecNewInstance (%s): Out of memory!", codec->name );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Init mutex and conditions
\*------------------------------------------------------------------------*/
  ickMutexInit( &instance->mutex_access );
  ickMutexInit( &instance->mutex_state );
  pthread_cond_init( &instance->condIsReady, NULL );
  pthread_cond_init( &instance->condEndOfTrack, NULL );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return instance;
}


/*=========================================================================*\
    Start the execution of a codec instance
    Returns -1 on error
\*=========================================================================*/
int codecStartInstance( CodecInstance *instance )
{
  int              rc;
  int              perr;
  struct timeval   now;
  struct timespec  abstime;

  DBGMSG( "codecStartInstance (%p,%s): starting instance.",
          instance, instance->codec->name );

/*------------------------------------------------------------------------*\
    Check state
\*------------------------------------------------------------------------*/
  if( instance->state!=CodecInitialized ) {
    logerr( "codecStartInstance (%s): Instance was already started.",
            instance->codec->name );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Create codec thread
\*------------------------------------------------------------------------*/
  rc = pthread_create( &instance->thread, NULL, _codecThread, instance );
  if( rc ) {
    instance->state = CodecTerminatedError;
    logerr( "codecStartInstance (%s): Unable to start thread (%s).",
            instance->codec->name, strerror(rc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Wait for max. 5 seconds till thread is up and running
\*------------------------------------------------------------------------*/
  DBGMSG( "codecStartInstance (%p,%s): waiting for codec thread to become ready.",
          instance, instance->codec->name );
  perr = pthread_mutex_lock( &instance->mutex_state );
  if( perr )
    logerr( "codecStartInstance: locking state mutex: %s", strerror(perr) );
  gettimeofday( &now, NULL );
  abstime.tv_sec  = now.tv_sec + 5;
  abstime.tv_nsec = now.tv_usec*1000UL;
  while( instance->state==CodecInitialized  ) {
    rc = pthread_cond_timedwait( &instance->condIsReady, &instance->mutex_state, &abstime );
    if( rc )
      break;
  }
  perr = pthread_mutex_unlock( &instance->mutex_state );
  if( perr )
    logerr( "codecStartInstance: unlocking state mutex: %s", strerror(perr) );

  // Something went wrong.
  if( rc ) {
    logerr( "codecStartInstance (%p,%s): codec thread did not become become ready (%s)",
            instance, instance->codec->name, strerror(rc) );
    instance->state = CodecTerminatedError;
    return -1;
  }

  // Signal codec initialization errors to caller
  if( instance->state != CodecRunning )
    return -1;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      To be called from codec thread when codec is ready
\*=========================================================================*/
void codecInstanceIsInitialized( CodecInstance *instance, CodecInstanceState state )
{
  DBGMSG( "codecInstanceIsReady (%s,%p): state %d -> %d",
          instance->codec->name, instance, instance->state, state );

/*------------------------------------------------------------------------*\
    Set new state
\*------------------------------------------------------------------------*/
  instance->state = state;

/*------------------------------------------------------------------------*\
    Signal this to start up function
\*------------------------------------------------------------------------*/
  pthread_cond_signal( &instance->condIsReady );

}


/*=========================================================================*\
      Delete a codec instance
\*=========================================================================*/
int codecDeleteInstance( CodecInstance *instance, bool wait )
{
  DBGMSG( "codecDeleteInstance (%s,%p): Deleting instance (%s).",
          instance->codec->name, instance, wait?"wait":"nowait" );

/*------------------------------------------------------------------------*\
    Stop thread and optionally wait for termination   
\*------------------------------------------------------------------------*/
  instance->state = CodecTerminating;
  if( instance->state!=CodecInitialized && wait ) {
     pthread_join( instance->thread, NULL ); 
      DBGMSG( "codecDeleteInstance (%s,%p): Instance has terminated.",
            instance->codec->name, instance );
  }

/*------------------------------------------------------------------------*\
    Delete mutex and conditions
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &instance->mutex_access );
  pthread_mutex_destroy( &instance->mutex_state );
  pthread_cond_destroy( &instance->condEndOfTrack );
  
/*------------------------------------------------------------------------*\
    Free header  
\*------------------------------------------------------------------------*/
  Sfree( instance->type );
  Sfree( instance );

/*------------------------------------------------------------------------*\
    That's it  
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Set icy interval (0 is disabled)
\*=========================================================================*/
void codecSetIcyInterval( CodecInstance *instance, long icyInterval )
{
  DBGMSG( "codecSetIcyInterval (%s,%p): %ld.",
          instance->codec->name, instance, icyInterval );

  instance->icyInterval = icyInterval;
}



/*=========================================================================*\
      Set callback for format detection / changes
\*=========================================================================*/
void codecSetFormatCallback( CodecInstance *instance, CodecFormatCallback callback, void *userData )
{
  DBGMSG( "codecSetFormatCallback (%s,%p): %p, userData %p.",
          instance->codec->name, instance, callback, userData );

  instance->formatCallback = callback;
  instance->formatCallbackUserData = userData;
}


/*=========================================================================*\
      Set callback for meta data detection
\*=========================================================================*/
void codecSetMetaCallback( CodecInstance *instance, CodecMetaCallback callback, void *userData )
{
  DBGMSG( "codecSetMetaCallback (%s,%p): %p, userData %p.",
          instance->codec->name, instance, callback, userData );

  instance->metaCallback = callback;
  instance->metaCallbackUserData = userData;
}


/*=========================================================================*\
      Wait for end of codec output
        timeout is in ms, 0 or a negative values are treated as infinity
        blocks and returns 0 if codec has ended
               or std. errcode (ETIMEDOUT in case of timeout) otherwise
\*=========================================================================*/
int codecWaitForEnd( CodecInstance *instance, int timeout )
{
  struct timeval  now;
  struct timespec abstime;
  int             err = 0;
  int             perr;

  DBGMSG( "codecWaitForEnd (%s,%p): Waiting for end (timeout %dms).",
          instance->codec->name, instance, timeout );

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  perr = pthread_mutex_lock( &instance->mutex_state );
  if( perr )
    logerr( "codecWaitForEnd: locking state mutex: %s", strerror(perr) );

/*------------------------------------------------------------------------*\
    Get absolut timestamp for timeout
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
  while( instance->state!=CodecTerminatedOk && instance->state!=CodecTerminatedError ) {

    // wait for condition
    err = timeout>0 ? pthread_cond_timedwait( &instance->condEndOfTrack, &instance->mutex_state, &abstime )
                    : pthread_cond_wait( &instance->condEndOfTrack, &instance->mutex_state );
    
    // Break on errors
    if( err )
      break; 
  }

/*------------------------------------------------------------------------*\
    Unlock mutex
\*------------------------------------------------------------------------*/
  perr = pthread_mutex_unlock( &instance->mutex_state );
  if( perr )
    logerr( "codecWaitForEnd: unlocking state mutex: %s", strerror(perr) );

  DBGMSG( "codecWaitForEnd (%s,%p): Waited for end (%s)",
          instance->codec->name, instance, strerror(err) );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return err;
}


/*=========================================================================*\
      Set volume in codec
\*=========================================================================*/
int codecSetVolume( CodecInstance *instance, double volume, bool muted )
{
  const Codec *codec = instance->codec;

  DBGMSG( "codecSetVolume (%s,%p): Set volume to %f (%s).",
           codec->name, instance, volume, muted?"muted":"unmuted" );

  // Not supported?
  if( !codec->setVolume ) {
    logwarn( "codecSetVolume (%s): Volume setting not supported.", codec->name );
    return -1;
  }

  // Call codec function
  return codec->setVolume( instance, volume, muted );
}


/*=========================================================================*\
      Get seek position (time)
\*=========================================================================*/
int codecGetSeekTime( CodecInstance *instance, double *pos )
{
  int          rc    = 0;
  const Codec *codec = instance->codec;

/*------------------------------------------------------------------------*\
    Use function supplied by codec (if any)
\*------------------------------------------------------------------------*/
  if( codec->getSeekTime )
    rc = codec->getSeekTime( instance, pos );

/*------------------------------------------------------------------------*\
    Calculate position from bytes delivered
\*------------------------------------------------------------------------*/
  else
    *pos = instance->bytesDelivered/(instance->format.channels*(instance->format.bitWidth/8))
           / (double)instance->format.sampleRate;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  DBGMSG( "codecGetSeekTime (%s,%p): %.2lfs", codec->name, instance, *pos );
  return rc;
}


/*=========================================================================*\
      Get current audio format
\*=========================================================================*/
const AudioFormat *codecGetAudioFormat( CodecInstance *instance )
{
  return &instance->format;
}


/*=========================================================================*\
       A decoder thread 
\*=========================================================================*/
static void *_codecThread( void *arg )
{
  CodecInstance *instance = (CodecInstance*)arg; 
  const Codec   *codec    = instance->codec;

  DBGMSG( "Codec thread (%s): starting.", codec->name );
  PTHREADSETNAME( codec->name );

/*------------------------------------------------------------------------*\
    Call Codec instance initializer
\*------------------------------------------------------------------------*/
  if( instance->codec->newInstance(instance) ) {
    logerr( "Codec thread (%s): Could not init instance.", codec->name );
    instance->state = CodecTerminatedError;
    pthread_cond_signal( &instance->condIsReady );
//    pthread_cond_signal( &instance->condEndOfTrack );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Thread main loop, used if deliverOutput is defined, else the codec
    is blocking in the initializer till end of input
\*------------------------------------------------------------------------*/
  while( codec->deliverOutput && instance->state==CodecRunning ) {
    int    rc;
    size_t size = 0;
    
    // Wait max. 500 ms for any free space in output fifo
    rc = fifoLockWaitWritable( instance->fifoOut, 500, 0 );
    if( rc==ETIMEDOUT ) {
      continue;
    }   
    if( rc ) {
      logerr( "Codec thread (%s): Error while waiting for fifo (%s), terminating.",
               codec->name, strerror(rc) );
      instance->state = CodecTerminatedError;
      break;
    }
    
    // Transfer data from codec to fifo
    size_t space = fifoGetSize( instance->fifoOut, FifoNextWritable );
    rc = codec->deliverOutput( instance, fifoGetWritePtr(instance->fifoOut), space, &size );

    // Unlock fifo
    fifoUnlockAfterWrite( instance->fifoOut, size );
    instance->bytesDelivered += size;

    // Be verbose
    DBGMSG( "Codec thread (%s,%p): %ld bytes written to output (space=%ld, rc=%d)",
            codec->name, instance, (long)size, (long)space, rc );
    
    // Error while providing data?
    if( rc<0 ) {
      logerr( "Codec thread (%s): Output error, terminating.", codec->name );
      instance->state = CodecTerminatedError;
      break;
    }

  }  // End of: Thread main loop
 
/*------------------------------------------------------------------------*\
    Terminate decoder  
\*------------------------------------------------------------------------*/
  if( codec->deleteInstance(instance) ) {
    logerr( "Codec thread (%s): Could not delete instance.", codec->name );
    instance->state = CodecTerminatedError;
    return NULL;
  }
  DBGMSG( "Codec thread (%s,%p): Terminated due to state %d.",
          codec->name, instance, instance->state );

/*------------------------------------------------------------------------*\
    Fulfilled external termination request without error?  
\*------------------------------------------------------------------------*/
  if( instance->state==CodecTerminating || instance->state==CodecEndOfTrack  )
    instance->state = CodecTerminatedOk;   

/*------------------------------------------------------------------------*\
    That's it ...  
\*------------------------------------------------------------------------*/
  return NULL;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/



