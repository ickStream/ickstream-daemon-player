/*$*********************************************************************\

Name            : -

Source File     : feed.c

Description     : implement audio feeds 

Comments        : -

Called by       : ickstream player and audio module 

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

// #undef DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include "utils.h"
#include "codec.h"
#include "fifo.h"
#include "feed.h"

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
static void *_feederThread( void *arg );
static size_t _curlWriteCallback( void *contents, size_t size, size_t nmemb, void *userp );


/*=========================================================================*\
      Create and prepare an audio feed 
\*=========================================================================*/
AudioFeed *audioFeedCreate( const char *uri, const char *type, AudioFormat *format )
{
  Codec      *codec;
  AudioFeed  *feed;
  
  DBGMSG( "Trying to create feed for \"%s\" (%s) with %dfps %dch", 
                      uri, type, format->sampleRate, format->channels  );	

/*------------------------------------------------------------------------*\
    Get first codec matching type and mode 
\*------------------------------------------------------------------------*/
  codec = codecFind( type, format, NULL );
  if( !codec ) {
    DBGMSG( "No codec found for (%s) with %dfps %dch", 
                        type, format->sampleRate, format->channels  );	
  	return NULL;
  }
  
/*------------------------------------------------------------------------*\
    Create header 
\*------------------------------------------------------------------------*/
  feed = calloc( 1, sizeof(AudioFeed) );
  if( !feed ) {
    srvmsg( LOG_ERR, "audioCreateFeed: out of memeory!" );
    return NULL;
  }
  
/*------------------------------------------------------------------------*\
    Copy parameters 
\*------------------------------------------------------------------------*/
  feed->uri        = strdup( uri );
  feed->type       = strdup( type );
  memcpy( &feed->format, format, sizeof(AudioFormat) );
  feed->state      = FeedInitialized;
  
/*------------------------------------------------------------------------*\
    Setup cURL  
\*------------------------------------------------------------------------*/
  feed->curlHandle = curl_easy_init();
  if( !feed->curlHandle ) {
  	srvmsg( LOG_ERR, "audioFeedCreate: unable to init cURL." );
  	feed->state = FeedTerminatedError;
  	return NULL;
  }
  
  // Set URI
  int rc = curl_easy_setopt( feed->curlHandle, CURLOPT_URL, feed->uri );
  if( rc ) {
  	srvmsg( LOG_ERR, "audioFeedCreate: unable to set URI: %s", feed->uri );
  	feed->state = FeedTerminatedError;
    curl_easy_cleanup( feed->curlHandle );
  } 
  
  // Set receiver callback
  size_t chunkSize = codec->feedChunkSize;
  if( chunkSize )
    curl_easy_setopt( feed->curlHandle, CURLOPT_BUFFERSIZE, chunkSize );
  curl_easy_setopt( feed->curlHandle, CURLOPT_WRITEDATA, (void*)feed );
  curl_easy_setopt( feed->curlHandle, CURLOPT_WRITEFUNCTION, _curlWriteCallback );
 
  // Set miscellaneous options
  curl_easy_setopt(feed->curlHandle, CURLOPT_USERAGENT, FeederAgentString );
  curl_easy_setopt(feed->curlHandle, CURLOPT_FOLLOWLOCATION, 1L );  // Follow redirects

/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
  return feed;                   
}

/*=========================================================================*\
      Start feeding to codec instance
\*=========================================================================*/
int audioFeedStart(  AudioFeed *feed, CodecInstance *instance )
{

/*------------------------------------------------------------------------*\
    Set codec instance
\*------------------------------------------------------------------------*/
  feed->codecInstance = instance;
   
/*------------------------------------------------------------------------*\
    Create feeder thread
\*------------------------------------------------------------------------*/
  int rc = pthread_create( &feed->thread, NULL, _feederThread, feed );
  if( rc ) {
    srvmsg( LOG_ERR, "Unable to start feeder thread: %s", strerror(rc) );
    audioFeedDelete( feed, false );
    return -1;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Delete an audio feed 
\*=========================================================================*/
int audioFeedDelete( AudioFeed *feed, bool wait )
{
  DBGMSG( "Deleting audio feed \"%s\"", feed->uri );

/*------------------------------------------------------------------------*\
    Stop thread and optionally wait for termination   
\*------------------------------------------------------------------------*/
  feed->state = FeedTerminating;
  if( feed->thread && wait )  //fixme
     pthread_join( feed->thread, NULL ); 
     
/*------------------------------------------------------------------------*\
    Free buffer and header  
\*------------------------------------------------------------------------*/
  Sfree( feed->uri );
  Sfree( feed->type );
  Sfree( feed );
  
/*------------------------------------------------------------------------*\
    That's all  
\*------------------------------------------------------------------------*/
  return 0;	
}


/*=========================================================================*\
       Get reference to output fifo 
         might be null, if not (yet) available
\*=========================================================================*/
Fifo *audioFeedFifo( AudioFeed *feed )
{
  CodecInstance *instance = feed->codecInstance;
  if( !instance ) 
    return NULL;
  return instance->fifoOut;
}


/*=========================================================================*\
       A feeder thread 
\*=========================================================================*/
static void *_feederThread( void *arg )
{
  AudioFeed *feed = (AudioFeed*)arg;
  int        rc;
      
/*------------------------------------------------------------------------*\
    Collect data, this represents the thread main loop  
\*------------------------------------------------------------------------*/
  feed->state = FeedRunning;
  rc = curl_easy_perform( feed->curlHandle );
  if( rc!=CURLE_OK) {
  	srvmsg( LOG_ERR, "Feeder thread error (%s): %s", feed->uri, curl_easy_strerror(rc) );
  	feed->state = FeedTerminatedError;
  }
  
/*------------------------------------------------------------------------*\
    Terminate curl  
\*------------------------------------------------------------------------*/
  curl_easy_cleanup( feed->curlHandle );

/*------------------------------------------------------------------------*\
    Signal end of input to codec
\*------------------------------------------------------------------------*/
  if( feed->codecInstance )
    codecSetEndOfInput( feed->codecInstance );

/*------------------------------------------------------------------------*\
    That's it ...  
\*------------------------------------------------------------------------*/
  feed->state = FeedTerminatedOk;
  return NULL;
}


/*=========================================================================*\
      cURL write callback
\*=========================================================================*/
static size_t _curlWriteCallback( void *buffer, size_t size, size_t nmemb, void *userp )
{
  size             *= nmemb;       // get real size in bytes
  size_t     okVal  = size;
  size_t     errVal = size+1;      // Signal error by size mismatch  
  AudioFeed *feed   = userp;

  DBGMSG( "Feeder thread(%s): receiving %ld bytes", 
                      feed->uri, (long)size );	

/*------------------------------------------------------------------------*\
    Terminate feed? 
\*------------------------------------------------------------------------*/
  if( feed->state!=FeedRunning )
    return errVal;

/*------------------------------------------------------------------------*\
    Process all data (possibly in chunks)
\*------------------------------------------------------------------------*/
  while( size ) {
    size_t chunk = 0;

    // Forward data to codec (this potentially blocks)
    if( codecFeedInput(feed->codecInstance,buffer,size,&chunk) )
      return errVal;
  
    // be defensive ...
    if( chunk>size ) {
      srvmsg( LOG_ERR, "Feeder thread(%s): codec consumed more than offered (%ld>%ld)", 
                      feed->uri, (long)chunk, (long)size );	
      return errVal;                    
    }
    
    // Calculate leftover 
    buffer  = (char*)buffer + chunk;
    size   -= chunk;

    // Terminate feed? 
    if( feed->state!=FeedRunning )
      return errVal;
    
    // sleep
    if( size ) {
      srvmsg( LOG_INFO, "Feeder thread(%s): could not process whole chunk (%ld of %ld), sleeping...", 
                         feed->uri, (long)chunk, (long)(size+chunk) );	
      sleep( 1 );
    }
  }
  
/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return okVal;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
