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

#undef DEBUG 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include "utils.h"
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
  srvmsg( LOG_INFO, "Registering codec %s", codec->name );
  
  // Call optional init method
  if( codec->init && codec->init() ) {
    srvmsg( LOG_INFO, "Could not init codec %s", codec->name );
    return -1;
  }
  
  // Link to list
  codec->next = codecList;
  codecList   = codec;	
  
  // That's all
  return 0;
}


/*=========================================================================*\
      Shutdown all codecs 
\*=========================================================================*/
void codecShutdown( bool force )
{
  Codec *codec; 
  DBGMSG( "Shuting down codecs" );

/*------------------------------------------------------------------------*\
    Loop over all codecs 
\*------------------------------------------------------------------------*/
  while( codecList ) {
  	
  	// Unlink from list
  	codec = codecList;
  	codecList = codecList->next;

    // call optional shutdown method
    if( codec->shutdown )
      codec->shutdown( force );
  }
  
/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
      Find a codec for type an parameters
        for initial call codec shall be NULL
        supply codec with previous result to get next match
        Returns a matching codec or NULL if none
\*=========================================================================*/
Codec *codecFind( const char *type, const AudioFormat *format, Codec *codec )
{

/*------------------------------------------------------------------------*\
    First call? 
\*------------------------------------------------------------------------*/
  if( !codec )
    codec = codecList;

/*------------------------------------------------------------------------*\
    Loop over all remaining list elements 
\*------------------------------------------------------------------------*/
  while( codec ) {
    if( codec->checkType(type,format) )
      break;	
    codec = codec->next;  
  }	
  
/*------------------------------------------------------------------------*\
    Return result 
\*------------------------------------------------------------------------*/
  DBGMSG( "Using codec for type %s: %s", type, 
                     codec?codec->name:"<none found>" );
  return codec;
}


/*=========================================================================*\
      Create a new instance of codec
\*=========================================================================*/
CodecInstance *codecNewInstance( Codec *codec, Fifo *fifo, AudioFormat *format )
{
  CodecInstance *instance;  
  DBGMSG( "Creating new instance of codec %s", codec->name );

/*------------------------------------------------------------------------*\
    Create header 
\*------------------------------------------------------------------------*/
  instance = calloc( 1, sizeof(CodecInstance) );
  if( !instance ) {
    srvmsg( LOG_ERR, "codecNewInstance: out of memeory!" );
    return NULL;
  }
  instance->state   = CodecInitialized;
  instance->codec   = codec;
  instance->fifoOut = fifo;
  memcpy( &instance->format, format, sizeof(AudioFormat) );
  
/*------------------------------------------------------------------------*\
    Call Codec instance initializer 
\*------------------------------------------------------------------------*/
  if( codec->newInstance(instance) ) {
  	srvmsg( LOG_ERR, "codecNewInstance: could not get instance of codec %s", 
  	                 codec->name );
    Sfree( instance );
  	return NULL;
  }

/*------------------------------------------------------------------------*\
    Init mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_init( &instance->mutex, NULL );

/*------------------------------------------------------------------------*\
    Create codec thread
\*------------------------------------------------------------------------*/
  int rc = pthread_create( &instance->thread, NULL, _codecThread, instance );
  if( rc ) {
    srvmsg( LOG_ERR, "codecNewInstance: Unable to start thread: %s", strerror(rc) );
    codec->deleteInstance( instance );
    Sfree( instance );
    return NULL;
  }
  
/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
  return instance;
}


/*=========================================================================*\
      Delete a codec instance
\*=========================================================================*/
int codecDeleteInstance(CodecInstance *instance, bool wait )
{
#ifdef DEBUG    
  Codec *codec = instance->codec;
#endif
  DBGMSG( "Deleting codec instance \"%s\"", codec->name );	

/*------------------------------------------------------------------------*\
    Stop thread and optionally wait for termination   
\*------------------------------------------------------------------------*/
  instance->state = CodecTerminating;
  if( instance->thread && wait )  //fixme
     pthread_join( instance->thread, NULL ); 

/*------------------------------------------------------------------------*\
    Free header  
\*------------------------------------------------------------------------*/
  Sfree( instance );

/*------------------------------------------------------------------------*\
    That's it  
\*------------------------------------------------------------------------*/
  return 0;	
}


/*=========================================================================*\
      Feed input to a codec instance
\*=========================================================================*/
int codecFeedInput( CodecInstance *instance, void *data, size_t size, size_t *accepted )
{
  Codec *codec = instance->codec;
  
  DBGMSG( "codec %s input: %ld bytes", codec->name, (long)size );
  
  // Call codec function
  return codec->acceptInput( instance, data, size, accepted );
}


/*=========================================================================*\
      Set volume in codec
\*=========================================================================*/
int codecSetVolume( CodecInstance *instance, double volume )
{
  Codec *codec = instance->codec;
  
  DBGMSG( "codec %s volume: %.2lf%%", codec->name, volume*100 );
  
  // Not supported?
  if( !codec->setVolume ) {
  	srvmsg( LOG_WARNING, "Codec %s: volume setting not supported.",
                         codec->name );
  	return -1;
  } 	
    
  // Call codec function
  return codec->setVolume( instance, volume );
}


/*=========================================================================*\
      Get seek position (time)
\*=========================================================================*/
int codecGetSeekTime( CodecInstance *instance, double *pos )
{
  Codec *codec = instance->codec;
  
  // Not supported?
  if( !codec->getSeekTime ) {
  	srvmsg( LOG_WARNING, "Codec %s: seek position not supported.",
                         codec->name );
  	return -1;
  } 	
    
  // Call codec function
  int rc = codec->getSeekTime( instance, pos );
  DBGMSG( "codec %s seek: %.2lf s", codec->name, *pos );
  return rc;
}


/*=========================================================================*\
       A decoder thread 
\*=========================================================================*/
static void *_codecThread( void *arg )
{
  CodecInstance *instance = (CodecInstance*)arg; 
  Codec         *codec    = instance->codec;

/*------------------------------------------------------------------------*\
    Thread main loop  
\*------------------------------------------------------------------------*/
  instance->state = CodecRunning;
  while( instance->state==CodecRunning) {
    int    rc;
    size_t size = 0;
    
  	// Wait max. 500 ms for free space in output fifo 
  	rc = fifoLockWaitWritable( instance->fifoOut, 500 );
  	if( rc==ETIMEDOUT ) {
  	  continue;
    }   
    if( rc ) {
      srvmsg( LOG_ERR, "Codec thread: wait error, terminating: %s", strerror(rc) );
  	  instance->state = CodecTerminatedError;
  	  break; 	
    }
    
  	// Transfer data from codec to fifo
    size_t space = fifoGetSize( instance->fifoOut, FifoNextWritable );
  	rc = codec->deliverOutput( instance, instance->fifoOut->writep, space, &size );
  	
  	// Unlock fifo
  	fifoUnlockAfterWrite( instance->fifoOut, size ); 
    
    // Be verbose
    DBGMSG( "Codec thread: %ld bytes written to output (space=%ld, rc=%d)",
                        (long)size, (long)space, rc );
    
    // Error while providing data?
  	if( rc<0 ) {                  
      srvmsg( LOG_ERR, "Codec thread: output error, terminating" );
  	  instance->state = CodecTerminatedError;
  	  break;
  	}
  	
  	// Normal end of input?
  	if( rc>0 ) {                  
      DBGMSG( "Codec thread: end of track, terminating" );
  	  instance->state = CodecTerminatedOk;
  	  break;
  	}
  	
  }  // End of: Thread main loop
 
/*------------------------------------------------------------------------*\
    Terminate decoder  
\*------------------------------------------------------------------------*/
  if( codec->deleteInstance(instance) ) {
  	srvmsg( LOG_ERR, "Codec thread: could not delete instance of codec %s", 
  	                 codec->name );
  	instance->state = CodecTerminatedError;
  	return NULL;
  }
  
/*------------------------------------------------------------------------*\
    Fulfilled external termination request without error?  
\*------------------------------------------------------------------------*/
  if( instance->state==CodecTerminating )
    instance->state = CodecTerminatedOk;   

/*------------------------------------------------------------------------*\
    Delete mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &instance->mutex );

/*------------------------------------------------------------------------*\
    That's it ...  
\*------------------------------------------------------------------------*/
  return NULL;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
