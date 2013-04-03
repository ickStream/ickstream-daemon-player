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

// #undef ICK_DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <curl/curl.h>


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
struct _audioFeed {
  AudioFeedState     state;
  char              *uri;
  char              *type;
  int                flags;
  AudioFormat        format;
  struct curl_slist *addedHeaderFields;
  char              *header;
  size_t             headerLen;
  long               icyInterval;
  pthread_t          thread;

  // Private to feeder thread
  CURL            *curlHandle;
  CodecInstance   *codecInstance;
};


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static void *_feederThread( void *arg );
static size_t _curlWriteCallback( void *contents, size_t size, size_t nmemb, void *userp );


/*=========================================================================*\
      Create and prepare an audio feed 
\*=========================================================================*/
AudioFeed *audioFeedCreate( const char *uri, const char *type, AudioFormat *format, int flags )
{
  Codec      *codec;
  AudioFeed  *feed;
  
  DBGMSG( "Trying to create feed for \"%s\" (%s) with %s, flags=%d",
                      uri, type, audioFormatStr(NULL,format), flags );

/*------------------------------------------------------------------------*\
    Get first codec matching type and mode 
\*------------------------------------------------------------------------*/
  codec = codecFind( type, format, NULL );
  if( !codec ) {
    DBGMSG( "No codec found for (%s) with %s",
                        type, audioFormatStr(NULL,format) );
  	return NULL;
  }
  
/*------------------------------------------------------------------------*\
    Create header 
\*------------------------------------------------------------------------*/
  feed = calloc( 1, sizeof(AudioFeed) );
  if( !feed ) {
    logerr( "audioCreateFeed: out of memory!" );
    return NULL;
  }
  feed->addedHeaderFields = NULL;
  feed->header            = NULL;
  
/*------------------------------------------------------------------------*\
    Copy parameters 
\*------------------------------------------------------------------------*/
  feed->flags      = flags;
  feed->uri        = strdup( uri );
  feed->type       = strdup( type );
  memcpy( &feed->format, format, sizeof(AudioFormat) );
  feed->state      = FeedInitialized;
  
/*------------------------------------------------------------------------*\
    Setup cURL  
\*------------------------------------------------------------------------*/
  feed->curlHandle = curl_easy_init();
  if( !feed->curlHandle ) {
  	logerr( "audioFeedCreate: unable to init cURL." );
  	feed->state = FeedTerminatedError;
  	return NULL;
  }
  
  // Set URI
  int rc = curl_easy_setopt( feed->curlHandle, CURLOPT_URL, feed->uri );
  if( rc ) {
  	logerr( "audioFeedCreate: unable to set URI: %s", feed->uri );
  	feed->state = FeedTerminatedError;
    curl_easy_cleanup( feed->curlHandle );
  } 
  
  // Set additional header elements
  if( flags&FeedIcy ) {
    feed->addedHeaderFields = curl_slist_append( feed->addedHeaderFields , "Icy-MetaData:1");
    curl_easy_setopt( feed->curlHandle, CURLOPT_HTTPHEADER, feed->addedHeaderFields );
  }

  // Are we interested in the connect header?
  curl_easy_setopt( feed->curlHandle, CURLOPT_HEADER, (feed->flags&FeedIgnoreHeader)?0:1 );

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
    logerr( "Unable to start feeder thread: %s", strerror(rc) );
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
  if( feed->addedHeaderFields )
    curl_slist_free_all( feed->addedHeaderFields );
  Sfree( feed->uri );
  Sfree( feed->type );
  Sfree( feed->header );
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
       Get URI of feed
\*=========================================================================*/
const char  *audioFeedGetURI( AudioFeed *feed )
{
  return feed->uri;
}


/*=========================================================================*\
       Get feed format
\*=========================================================================*/
AudioFormat *audioFeedGetFormat( AudioFeed *feed )
{
  return &feed->format;
}


/*=========================================================================*\
       Get Response Header
\*=========================================================================*/
const char *audioFeedGetResponseHeader( AudioFeed *feed )
{
  return feed->header;
}


/*=========================================================================*\
       Get Response Header field
         result is allocated and must be freed!
\*=========================================================================*/
char *audioFeedGetResponseHeaderField( AudioFeed *feed, const char *fieldName )
{
  const char *ptr = feed->header;
  size_t      len = strlen(fieldName );

  DBGMSG( "audioFeedGetResponseHeader(%s): \"%s\":",
           feed->uri, fieldName );

  // No headers available
  if( !ptr )
    return NULL;

  // Loop over all lines
  while( *ptr ) {

    // Check for field name
    if( !strncasecmp(ptr,fieldName,len) && ptr[len]==':' ) {

      // Skip to value, ignore leading spaces
      ptr += len+1;
      while( *ptr==' ' || *ptr=='\t' )
        ptr++;

      // Get end of line
      char *endptr = strpbrk( ptr, "\n\r" );
      if( !endptr )
        len = strlen(ptr);
      else
        len = endptr - ptr;

      // Copy value
      endptr = calloc( len+1, 1 );
      strncpy( endptr, ptr, len );
      DBGMSG( "audioFeedGetResponseHeader(%s): found field \"%s\": \"%s\"",
               feed->uri, fieldName, endptr );

      // That's it
      return endptr;
    }

    // Skip content of non-matching line
    ptr = strpbrk( ptr, "\n\r" );
    if( !ptr )
      break;

    // Skip line breaks (be tolerant to actual format)
    while( *ptr=='\r' || *ptr=='\n' )
      ptr++;
  }

  // Not found
  return NULL;
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
  	logerr( "Feeder thread error (%s): %s", feed->uri, curl_easy_strerror(rc) );
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
  size_t     retVal = size;        // Ok
  size_t     errVal = size+1;      // Signal error by size mismatch  
  AudioFeed *feed   = userp;
  void      *newbuf = NULL;
  long headerSize = 0;

  // Get header size up to now
  curl_easy_getinfo( feed->curlHandle, CURLINFO_HEADER_SIZE, &headerSize );

  DBGMSG( "Feeder thread(%s): receiving %ld bytes, headersize: %ld",
                      feed->uri, (long)size, headerSize );

/*------------------------------------------------------------------------*\
    Terminate feed? 
\*------------------------------------------------------------------------*/
  if( feed->state!=FeedRunning )
    return errVal;

/*------------------------------------------------------------------------*\
    Process header
\*------------------------------------------------------------------------*/
  if( !(feed->flags&FeedIgnoreHeader) && !(feed->flags&FeedHeaderComplete) ) {

    DBGMSG( "Feeder thread(%s): header part: curlHeaderSize=%ld, headerBufLen=%ld size=%ld",
              feed->uri, headerSize, feed->headerLen, size );

    // First packet
    if( !feed->header ) {
      feed->header = malloc( size+1 );
      strncpy( feed->header, buffer, size );
      feed->header[size] = 0;
      size = 0;
    }

    // Complete?
    else if( headerSize<feed->headerLen ) {
      size_t diff = feed->headerLen - headerSize;
      feed->flags |= FeedHeaderComplete;
      newbuf = malloc( diff+size );
      memcpy( newbuf, feed->header+headerSize, diff );
      memcpy( newbuf+headerSize, buffer, size );
      buffer = newbuf;
      size  += diff;
      feed->header[headerSize] = 0;
      DBGMSG( "Feeder thread(%s): header complete: \"%s\"",
              feed->uri, feed->header );

      // Process icy headers
      if( feed->flags&FeedIcy ) {
        char *str = audioFeedGetResponseHeaderField( feed, "icy-metaint" );
        if( !str ) {
          logerr( "Feeder thread(%s): header field \"icy-metaint\" not found.",
                                feed->uri );
          Sfree( newbuf );
          return retVal;
        }
        feed->icyInterval = atol( str );
        Sfree( str );
        DBGMSG( "Feeder thread(%s): icy interval is %d",
              feed->uri, feed->icyInterval );

      }


    }

    // Extend existing header
    else {
      size_t      len = strlen(feed->header) + size;
      feed->header    = realloc( feed->header, len+1 );
      feed->headerLen = len;
      strncat( feed->header, buffer, size );
      feed->header[len] = 0;
      size = 0;
    }

  }

/*------------------------------------------------------------------------*\
    Process all data (possibly in chunks)
\*------------------------------------------------------------------------*/
  while( size ) {
    size_t chunk = 0;

    // Start codec output
    if( feed->codecInstance->state==CodecInitialized &&
        codecStartInstanceOutput(feed->codecInstance,feed->icyInterval) ) {
      logerr( "Feeder thread(%s): could not start codec output thread.",
                      feed->uri );
      retVal = errVal;
      break;
    }

    // Forward data to codec (this potentially blocks)
    if( codecFeedInput(feed->codecInstance,buffer,size,&chunk) ) {
      retVal = errVal;
      break;
    }

    // be defensive ...
    if( chunk>size ) {
      logerr( "Feeder thread(%s): codec consumed more than offered (%ld>%ld)", 
                      feed->uri, (long)chunk, (long)size );	
      retVal = errVal;
      break;
    }
    
    // Calculate leftover 
    buffer  = (char*)buffer + chunk;
    size   -= chunk;

    // Terminate feed? 
    if( feed->state!=FeedRunning ) {
      retVal = errVal;
      break;
    }
    
    // sleep
    if( size ) {
      loginfo( "Feeder thread(%s): could not process whole chunk (%ld of %ld), sleeping...", 
                         feed->uri, (long)chunk, (long)(size+chunk) );	
      sleep( 1 );
    }
  }
  
/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  Sfree( newbuf );
  return retVal;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/


