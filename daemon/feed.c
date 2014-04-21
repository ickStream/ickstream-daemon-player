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
// #define ICK_TRACECURL

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/select.h>
#include <curl/curl.h>

#include "ickpd.h"
#include "ickutils.h"
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
  volatile AudioFeedState  state;
  int                      flags;
  char                    *uri;
  char                    *oAuthToken;
  AudioFeedCallback        callback;
  void                    *usrData;
  char                    *type;
  AudioFormat              format;
  int                      icyInterval;
  int                      pipefd[2];
  char                    *header;
  size_t                   headerLen;
  CURL                    *curlHandle;
  pthread_t                thread;
  pthread_mutex_t          mutex;
  pthread_cond_t           condIsConnected;
};


/*=========================================================================*\
    Private prototypes
\*=========================================================================*/
static void *_feederThread( void *arg );
static size_t _curlWriteCallback( void *contents, size_t size, size_t nmemb, void *userp );
#ifdef ICK_TRACECURL
static int    _curlTraceCallback( CURL *handle, curl_infotype type, char *data, size_t size, void *userp );
#endif


/*=========================================================================*\
    Create and start an audio data feed
      We use curl so we basically can use all sorts of sources and auth schemes
      uri        - is the source locator
      oAuthToken - supply token if needed (NULL otherwise)
      flags      - controls the behavior (see header)
      callback   - is a function that signals state changes
      Use the file descriptor obtained by audioFeedGetFd() to access data
      Return NULL on error.
\*=========================================================================*/
AudioFeed *audioFeedCreate( const char *uri, const char *oAuthToken, int flags, AudioFeedCallback callback, void *usrData )
{
  AudioFeed           *feed;
  int                  rc;

  DBGMSG( "audioFeedCreate: \"%s\", flags=%d, callback=%p", uri, flags, callback );

/*------------------------------------------------------------------------*\
    Create header 
\*------------------------------------------------------------------------*/
  feed = calloc( 1, sizeof(AudioFeed) );
  if( !feed ) {
    logerr( "audioCreateFeed (%s): out of memory!", uri );
    return NULL;
  }
  feed->state             = FeedInitialized;
  feed->header            = NULL;
  memset( &feed->format, 0, sizeof(AudioFormat) );

/*------------------------------------------------------------------------*\
    Setup output pipe
\*------------------------------------------------------------------------*/
  if( pipe(feed->pipefd) ) {
    logerr( "audioFeedCreate (%s): could not create pipe (%s).",
            uri, strerror(errno) );
    Sfree( feed );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Init mutex and conditions
\*------------------------------------------------------------------------*/
  ickMutexInit( &feed->mutex );
  pthread_cond_init( &feed->condIsConnected, NULL );

/*------------------------------------------------------------------------*\
    Copy parameters
\*------------------------------------------------------------------------*/
  feed->flags      = flags;
  feed->uri        = strdup( uri );
  feed->oAuthToken = oAuthToken ? strdup(oAuthToken) : NULL;
  feed->callback   = callback;
  feed->usrData    = usrData;

/*------------------------------------------------------------------------*\
    Create feeder thread, this encapsulates all curl actions
\*------------------------------------------------------------------------*/
  rc = pthread_create( &feed->thread, NULL, _feederThread, feed );
  if( rc ) {
    logerr( "audioFeedCreate (%s): Unable to start feeder thread: %s", strerror(rc) );
    audioFeedDelete( feed, false );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return feed;
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
  if( feed->state!=FeedInitialized && wait )
     pthread_join( feed->thread, NULL ); 

/*------------------------------------------------------------------------*\
    Close writing end of pipe
\*------------------------------------------------------------------------*/
  close( feed->pipefd[1] );

/*------------------------------------------------------------------------*\
    Delete mutex and conditions
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &feed->mutex );
  pthread_cond_destroy( &feed->condIsConnected );

/*------------------------------------------------------------------------*\
    Free buffers and header
\*------------------------------------------------------------------------*/
  Sfree( feed->uri );
  Sfree( feed->oAuthToken );
  Sfree( feed->type );
  Sfree( feed->header );
  Sfree( feed );

/*------------------------------------------------------------------------*\
    That's all  
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Lock feed to avoid concurrent modifications
\*=========================================================================*/
void audioFeedLock( AudioFeed *feed )
{
  int perr;

  DBGMSG( "Audiofeed (%p,%s): lock.", feed, feed->uri );
  perr = pthread_mutex_lock( &feed->mutex );
  if( perr )
    logerr( "audioFeedLock: %s", strerror(perr) );

}


/*=========================================================================*\
      Unlock feed after modifications or waits
\*=========================================================================*/
void audioFeedUnlock( AudioFeed *feed )
{
  int perr;
  DBGMSG( "Audiofeed (%p,%s): unlock.", feed, feed->uri );
  perr = pthread_mutex_unlock( &feed->mutex );
  if( perr )
    logerr( "audioFeedUnlock: %s", strerror(perr) );
}


/*=========================================================================*\
    Lock feed and wait for connection
      timeout is in ms, 0 or a negative values are treated as infinity
      returns 0 and locks feed, if condition is met
        std. errode (ETIMEDOUT in case of timeout) and no locking otherwise
\*=========================================================================*/
int audioFeedLockWaitForConnection( AudioFeed *feed, int timeout )
{
  struct timeval  now;
  struct timespec abstime;
  int             err = 0;
  int             perr;

  DBGMSG( "Audiofeed (%p,%s): waiting for connection (timeout %dms)",
          feed, feed->uri, timeout );

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   perr = pthread_mutex_lock( &feed->mutex );
   if( perr )
     logerr( "audioFeedLockWaitForConnection: locking feed mutex: %s", strerror(perr) );


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
  while( feed->state<FeedConnected ) {

    // wait for condition
    err = timeout>0 ? pthread_cond_timedwait( &feed->condIsConnected, &feed->mutex, &abstime )
                    : pthread_cond_wait( &feed->condIsConnected, &feed->mutex );

    // Break on errors
    if( err )
      break;
  }

/*------------------------------------------------------------------------*\
    In case of error: unlock mutex
\*------------------------------------------------------------------------*/
  if( err ) {
    perr = pthread_mutex_unlock( &feed->mutex );
    if( perr )
      logerr( "audioFeedLockWaitForConnection: unlocking feed mutex: %s", strerror(perr) );
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  DBGMSG( "Audiofeed (%p,%s): waiting for connection: %s",
          feed, feed->uri, err?strerror(err):"Locked" );
  return err;
}


/*=========================================================================*\
    Get URI of feed
\*=========================================================================*/
const char  *audioFeedGetURI( AudioFeed *feed )
{
  return feed->uri;
}


/*=========================================================================*\
    Get feed flags
\*=========================================================================*/
int audioFeedGetFlags( AudioFeed *feed )
{
  return feed->flags;
}


/*=========================================================================*\
    Get feed state
\*=========================================================================*/
AudioFeedState audioFeedGetState( AudioFeed *feed )
{
  return feed->state;
}


/*=========================================================================*\
    Get output file handle
\*=========================================================================*/
int audioFeedGetFd( AudioFeed *feed )
{
  return feed->pipefd[0];
}


/*=========================================================================*\
    Get audio format (might only be set when headers are enabled)
\*=========================================================================*/
AudioFormat *audioFeedGetFormat( AudioFeed *feed )
{
  return &feed->format;
}


/*=========================================================================*\
    Get type (might only be set when headers are enabled)
\*=========================================================================*/
const char *audioFeedGetType( AudioFeed *feed )
{
  return feed->type;
}


/*=========================================================================*\
    Get ICY interval (might only be set when headers are enabled)
\*=========================================================================*/
long audioFeedGetIcyInterval( AudioFeed *feed )
{
  return feed->icyInterval;
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
       if a field is contained more than once the _last_ instance is used
       if field is NULL the first (response) line is sent.
       the result is allocated and must be freed!
       return NULL if not found
\*=========================================================================*/
char *audioFeedGetResponseHeaderField( AudioFeed *feed, const char *fieldName )
{
  const char *ptr     = feed->header;
  char       *retval  = NULL;
  size_t      nameLen = fieldName ? strlen(fieldName) : 0;

  DBGMSG( "audioFeedGetResponseHeader(%s): \"%s\":",
           feed->uri, fieldName?fieldName:"(null)" );

  // Loop over all lines
  while( ptr && *ptr ) {
    size_t len;

    // Check for field name (or first/response line)
    if( !fieldName || (!strncasecmp(ptr,fieldName,nameLen) && ptr[nameLen]==':') ) {

      // Skip to value, ignore leading spaces
      if( fieldName )
        ptr += nameLen+1;
      while( *ptr==' ' || *ptr=='\t' )
        ptr++;

      // Get end of line
      char *endptr = strpbrk( ptr, "\n\r" );
      if( !endptr )
        len = strlen( ptr );
      else
        len = endptr - ptr;

      // Copy value
      Sfree( retval );
      retval = strndup( ptr, len );
      DBGMSG( "audioFeedGetResponseHeader(%s): Found field \"%s\": \"%s\"",
               feed->uri, fieldName?fieldName:"(null)", retval );

      // That's it (in case we're looking for the first instance)
      if( !fieldName )
        return retval;
    }

    // Skip content of non-matching line
    ptr = strpbrk( ptr, "\n\r" );
    if( !ptr )
      break;

    // Skip line breaks (be tolerant to actual format)
    while( *ptr=='\r' || *ptr=='\n' )
      ptr++;
  }

  // Return result
  return retval;
}


/*=========================================================================*\
       A feeder thread 
\*=========================================================================*/
static void *_feederThread( void *arg )
{
  AudioFeed         *feed = (AudioFeed*)arg;
  int                rc;
  struct curl_slist *addedHeaderFields = NULL;

  DBGMSG( "Feeder thread (%p,%s): starting.", feed, feed->uri );
  PTHREADSETNAME( "feed" );

/*------------------------------------------------------------------------*\
    Block broken pipe signals from this thread
\*------------------------------------------------------------------------*/
  sigset_t sigSet;
  sigemptyset( &sigSet );
  sigaddset( &sigSet, SIGPIPE );
  pthread_sigmask( SIG_BLOCK, NULL, &sigSet );

/*------------------------------------------------------------------------*\
    Setup cURL
\*------------------------------------------------------------------------*/
  feed->curlHandle = curl_easy_init();
  if( !feed->curlHandle ) {
    logerr( "audioFeedCreate (%s): Unable to init cURL.", feed->uri );
    feed->state = FeedTerminatedError;
    goto end;
  }

  // Set URI
  rc = curl_easy_setopt( feed->curlHandle, CURLOPT_URL, feed->uri );
  if( rc ) {
    logerr( "audioFeedCreate (%s): Unable to set URI.", feed->uri );
    feed->state = FeedTerminatedError;
    goto end;
  }

  // ICY protocol enabled?
  if( feed->flags&FeedIcy ) {
    DBGMSG( "Feeder thread (%p,%s): Requesting ICY data.", feed, feed->uri );

    // Add header ICY header field
    addedHeaderFields = curl_slist_append( addedHeaderFields , "Icy-MetaData: 1" );
    DBGMSG( "Feeder thread (%p,%s): Added request header \"%s\".", feed, feed->uri, "Icy-MetaData: 1" );
  }

  // Need oAuth header
  if( feed->oAuthToken ) {
    char *hdr = malloc( strlen(feed->oAuthToken)+32 );
    sprintf( hdr, "Authorization: Bearer %s", feed->oAuthToken );
    addedHeaderFields = curl_slist_append( addedHeaderFields, hdr );  // Performs a strdup(hdr)
    DBGMSG( "Feeder thread (%p,%s): Added request header \"%s\".", feed, feed->uri, hdr );
    Sfree( hdr );
  }

  // Add headers
  if( addedHeaderFields ) {
    rc = curl_easy_setopt( feed->curlHandle, CURLOPT_HTTPHEADER, addedHeaderFields );
    if( rc ) {
      logerr( "audioFeedCreate (%s): Unable to add ICY HTTP header.", feed->uri );
      feed->state = FeedTerminatedError;
      goto end;
    }
  }

  // We are interested in the HTTP response header
  rc = curl_easy_setopt( feed->curlHandle, CURLOPT_HEADER, 1 );
  if( rc ) {
    logerr( "audioFeedCreate (%s): Unable to set header mode.", feed->uri );
    feed->state = FeedTerminatedError;
    goto end;
  }

  // Set our identity
  rc = curl_easy_setopt( feed->curlHandle, CURLOPT_USERAGENT, HttpAgentString );
  if( rc ) {
    logerr( "audioFeedCreate (%s): unable to set user agent to \"%s\"", feed->uri, HttpAgentString );
    feed->state = FeedTerminatedError;
    goto end;
  }

  // Enable HTTP redirects
  rc = curl_easy_setopt(feed->curlHandle, CURLOPT_FOLLOWLOCATION, 1L );
  if( rc ) {
    logerr( "audioFeedCreate (%s): unable to enable redirects", feed->uri );
    feed->state = FeedTerminatedError;
    goto end;
  }

  // Set receiver callback
  rc = curl_easy_setopt( feed->curlHandle, CURLOPT_WRITEDATA, (void*)feed );
  if( rc ) {
    logerr( "audioFeedCreate (%s): Unable set callback mode.", feed->uri );
    feed->state = FeedTerminatedError;
    goto end;
  }
  rc = curl_easy_setopt( feed->curlHandle, CURLOPT_WRITEFUNCTION, _curlWriteCallback );
  if( rc ) {
    logerr( "audioFeedCreate (%s): Unable set callback function.", feed->uri );
    feed->state = FeedTerminatedError;
    goto end;
  }

  // Set tracing callback in debugging mode
#ifdef ICK_TRACECURL
  curl_easy_setopt( feed->curlHandle, CURLOPT_DEBUGFUNCTION, _curlTraceCallback );
  curl_easy_setopt( feed->curlHandle, CURLOPT_VERBOSE, 1L);
#endif

/*------------------------------------------------------------------------*\
    Collect data, this represents the thread main loop  
\*------------------------------------------------------------------------*/
  feed->state = FeedConnecting;
  rc = curl_easy_perform( feed->curlHandle );
  if( rc==CURLE_OK || feed->state==FeedTerminating )
    feed->state = FeedTerminatedOk;
  else {
    logerr( "Feeder thread (%p,%s): %s", feed, feed->uri, curl_easy_strerror(rc) );
    feed->state = FeedTerminatedError;
  }
  DBGMSG( "Feeder thread (%p,%s): terminating with curl state \"%s\".",
          feed, feed->uri, curl_easy_strerror(rc) );

end:

/*------------------------------------------------------------------------*\
    Execute callback
\*------------------------------------------------------------------------*/
  if( feed->callback ) {
    rc = feed->callback( feed, feed->usrData );
    if( rc )
      logerr( "Feeder thread (%p,%s): callback returned error %d.",
              feed, feed->uri, rc );
  }

/*------------------------------------------------------------------------*\
    Close pipe
\*------------------------------------------------------------------------*/
  close( feed->pipefd[1]);

/*------------------------------------------------------------------------*\
    Clean up curl
\*------------------------------------------------------------------------*/
  if(feed->curlHandle )
    curl_easy_cleanup( feed->curlHandle );
  feed->curlHandle = NULL;
  if( addedHeaderFields )
    curl_slist_free_all( addedHeaderFields );

/*------------------------------------------------------------------------*\
    That's all ...
\*------------------------------------------------------------------------*/
  DBGMSG( "Feeder thread (%p,%s): terminated.", feed, feed->uri );
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

  DBGMSG( "Feeder thread (%s): receiving %ld bytes", feed->uri, (long)size );
  //DBGMEM( "Raw feed", buffer, size );

/*------------------------------------------------------------------------*\
    Feed termination requested?
\*------------------------------------------------------------------------*/
  if( feed->state>FeedConnected )
    return errVal;

/*------------------------------------------------------------------------*\
    Process header
\*------------------------------------------------------------------------*/
  if( feed->state==FeedConnecting ) {
    long headerSize = 0;

    // Get header size up to now
    curl_easy_getinfo( feed->curlHandle, CURLINFO_HEADER_SIZE, &headerSize );
    //DBGMSG( "Feeder thread(%s): header part: curlHeaderSize=%ld, headerBufLen=%ld size=%ld",
    //          feed->uri, headerSize, feed->headerLen, size );

    // First packet
    if( !feed->header ) {
      feed->header    = malloc( size );
      feed->headerLen = size;
      memcpy( feed->header, buffer, size );
      size = 0;
    }

    // Complete? (we can only detect this one call late ;-()
    else if( headerSize<feed->headerLen ) {
      size_t diff = feed->headerLen - headerSize;

      // Copy Binary part to buffer
      newbuf = malloc( diff+size );
      memcpy( newbuf, feed->header+headerSize, diff );
      memcpy( newbuf+diff, buffer, size );
      buffer = newbuf;
      size  += diff;

      // Terminate and shrink header string
      feed->header[headerSize] = 0;
      feed->header = realloc( feed->header, headerSize+1 );
      DBGMSG( "Feeder thread (%s): header complete: \"%s\"",
              feed->uri, feed->header );

      // Process icy headers
      if( feed->flags&FeedIcy ) {
        char *str = audioFeedGetResponseHeaderField( feed, "icy-metaint" );
        if( !str ) {
          logerr( "Feeder thread (%s): header field \"icy-metaint\" not found.",
                                feed->uri );
          Sfree( newbuf );
          return errVal;
        }
        feed->icyInterval = atol( str );
        Sfree( str );
        DBGMSG( "Feeder thread (%s): icy interval is %d",
              feed->uri, feed->icyInterval );
      }

      // Get content type
      feed->type = audioFeedGetResponseHeaderField( feed, "Content-Type" );

      // Set new state and inform delegates
      feed->state = FeedConnected;
      pthread_cond_signal( &feed->condIsConnected );
      if( feed->callback ) {
        int rc = feed->callback( feed, feed->usrData );
        if( rc ) {
          logerr( "Feeder thread (%s): callback returned error %d - terminating.",
              feed->uri, rc );
          Sfree( newbuf );
          return errVal;
        }
      }
    }

    // Extend existing header
    else {
      size_t oldLen    = feed->headerLen;
      feed->headerLen += size;
      feed->header     = realloc( feed->header, feed->headerLen );
      memcpy( feed->header+oldLen, buffer, size );
      size = 0;
    }

    // DBGMSG( "Feeder thread(%s): after header: curlHeaderSize=%ld, headerBufLen=%ld size=%ld",
    //          feed->uri, headerSize, feed->headerLen, size );
  }

/*------------------------------------------------------------------------*\
    Write data to pipe
\*------------------------------------------------------------------------*/
  //DBGMEM( "Binary feed", buffer, size );
  while( size ) {
    fd_set         wfds;
    struct timeval tv;
    int            retval;
    ssize_t        bytes;

    // Terminate feed?
    if( feed->state>FeedConnected ) {
      retVal = errVal;
      break;
    }

    // wait max. 500ms for pipe to be writable
    FD_ZERO( &wfds );
    FD_SET( feed->pipefd[1], &wfds);
    tv.tv_sec  = 0;
    tv.tv_usec = 500*1000;
    retval     = select( feed->pipefd[1]+1, NULL, &wfds, NULL, &tv);
    if( retval<0 ) {
      logerr( "Feeder thread (%s): select returned %s",
               feed->uri, strerror(errno) );
      retVal = errVal;
      break;
    }
    else if( !retval ) {
      DBGMSG( "Feeder thread(%s): waiting for pipe to be writable...",
               feed->uri );
      continue;
    }

              /* FD_ISSET(0, &rfds) will be true. */


    // Forward data to pipe (blocking mode)
    bytes = write( feed->pipefd[1], buffer, size );
    if( bytes<0 ) {
      DBGMSG( "Feeder thread(%s): could not write to pipe: %s",
               feed->uri, strerror(errno) );
      if( errno!=EPIPE && errno!=ECONNRESET )
        logerr( "Feeder thread (%s): could not write to pipe: %s",
                 feed->uri, strerror(errno) );
      retVal = errVal;
      break;
    }
    DBGMSG( "Feeder thread(%s): wrote %ld/%ld bytes to pipe",
             feed->uri, (long)bytes, (long)size );

    // Calculate leftover 
    buffer  = (char*)buffer + bytes;
    size   -= bytes;

    // Terminate feed? 
    if( feed->state>FeedConnected ) {
      retVal = errVal;
      break;
    }

    // sleep
    if( size ) {
      loginfo( "Feeder thread(%s): could not process whole chunk (%ld of %ld), sleeping...", 
               feed->uri, (long)bytes, (long)(size+bytes) );
      sleep( 1 );  // Fixme.
    }
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  Sfree( newbuf );
  return retVal;
}

/*=========================================================================*\
      cURL debug callback
      (stolen from http://curl.haxx.se/libcurl/c/debug.html)
\*=========================================================================*/
#ifdef ICK_TRACECURL
static int _curlTraceCallback( CURL *handle, curl_infotype type, char *data, size_t size, void *userp )
{
  const char *text;

/*------------------------------------------------------------------------*\
    Switch on trace message type
\*------------------------------------------------------------------------*/
  switch (type) {
    case CURLINFO_TEXT:
      DBGMSG( "== Info: %s", data );
      // no break

    default: /* in case a new one is introduced to shock us */
      return 0;

    case CURLINFO_HEADER_OUT:
      text = "=> Send header";
      break;
    case CURLINFO_DATA_OUT:
      text = "=> Send data";
      break;
    case CURLINFO_SSL_DATA_OUT:
      text = "=> Send SSL data";
      break;
    case CURLINFO_HEADER_IN:
      text = "<= Recv header";
      break;
    case CURLINFO_DATA_IN:
      text = "<= Recv data";
      break;
    case CURLINFO_SSL_DATA_IN:
      text = "<= Recv SSL data";
      break;
  }

/*------------------------------------------------------------------------*\
    Dump data and return
\*------------------------------------------------------------------------*/
  DBGMEM( text, data, size );
  return 0;
}
#endif

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/


