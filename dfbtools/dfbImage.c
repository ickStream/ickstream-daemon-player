/*$*********************************************************************\

Name            : -

Source File     : image.c

Description     : Image widget for direct frame buffer HMI

Comments        : only needed for "hmi=DirectFB" configurations

Called by       : hmiDirectFB.c

Calls           : 

Error Messages  : -
  
Date            : 30.05.2013

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
#include <stdbool.h>
#include <pthread.h>
#include <directfb.h>
#include <signal.h>
#include <curl/curl.h>

#include "dfbWidget.h"
#include "dfbImage.h"
#include "../ickpd.h"
#include "../utils.h"


/*=========================================================================*\
    Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
    Macro and type definitions
\*=========================================================================*/
struct _dfbimage {
  volatile DfbImageState   state;
  int                      refCounter;
  double                   lastaccess;
  DfbImage                *next;               // weak
  char                    *uri;                // strong
  char                    *oAuthToken;         // strong
  IDirectFB               *dfb;                // weak
  IDirectFBDataBuffer     *dfbBuffer;          // strong
  IDirectFBImageProvider  *dfbProvider;        // strong
  char                    *bufferData;         // strong
  size_t                   bufferLen;
  CURL                    *curlHandle;
  pthread_t                thread;
  pthread_mutex_t          mutex;
  pthread_cond_t           condIsComplete;
};


/*=========================================================================*\
    Private symbols
\*=========================================================================*/
static char             *resPath;
static pthread_mutex_t   listMutex = PTHREAD_MUTEX_INITIALIZER;
static DfbImage         *imageList;


/*=========================================================================*\
    Private prototypes
\*=========================================================================*/
static void *_imageThread( void *arg );
static size_t _curlWriteCallback( void *buffer, size_t size, size_t nmemb, void *userp );
#ifdef ICK_TRACECURL
static int    _curlTraceCallback( CURL *handle, curl_infotype type, char *data, size_t size, void *userp );
#endif


/*=========================================================================*\
    Set resource path (use NULL to clean up)
\*=========================================================================*/
void dfbImageSetResourcePath( const char *path )
{
  DBGMSG( "dfbImageSetResourcePath: \"%s\"", path );

  Sfree( resPath );
  if( path )
    resPath = strdup( path );
}


/*=========================================================================*\
    Get or Create an image instance
\*=========================================================================*/
DfbImage *dfbImageGet( IDirectFB *dfb, const char *uri, bool isfile )
{
  DfbImage            *dfbi;
  pthread_mutexattr_t  attr;

  DBGMSG( "dfbImageGet: \"%s\"", uri );

/*------------------------------------------------------------------------*\
    Already in cache?
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &listMutex );
  for( dfbi=imageList; dfbi; dfbi=dfbi->next )
    if( !strcmp(uri,dfbi->uri) )
      break;
  if( dfbi ) {
    dfbi->refCounter++;
    pthread_mutex_unlock( &listMutex );
    DBGMSG( "dfbImageGet (%p): \"%s\" found in cache, now %d references",
            dfbi, uri, dfbi->refCounter );
    dfbi->lastaccess = srvtime();
    return dfbi;
  }
  pthread_mutex_unlock( &listMutex );

/*------------------------------------------------------------------------*\
    Create header
\*------------------------------------------------------------------------*/
  dfbi = calloc( 1, sizeof(DfbImage) );
  if( !dfbi ) {
    logerr( "dfbImageGet (%s): out of memory!", uri );
    return NULL;
  }
  dfbi->refCounter = 1;
  dfbi->state      = DfbImageInitialzed;
  dfbi->dfb        = dfb;

/*------------------------------------------------------------------------*\
    Create full path for non-absolute file names
\*------------------------------------------------------------------------*/
  if( isfile && resPath && *uri!='/' ) {
    dfbi->uri = malloc( strlen(resPath)+strlen(uri)+2 );
    sprintf( dfbi->uri, "%s/%s", resPath, uri );
  }
  else
    dfbi->uri = strdup( uri );

/*------------------------------------------------------------------------*\
    Files: Try to read directly
\*------------------------------------------------------------------------*/
  if( isfile ) {
    DFBResult drc = dfb->CreateImageProvider( dfb, dfbi->uri, &dfbi->dfbProvider );
    if( drc!=DFB_OK ) {
      logerr( "dfbImageGet (%s): could not create image provider (%s).",
              dfbi->uri, DirectFBErrorString(drc) );
      Sfree( dfbi->uri );
      Sfree( dfbi );
      return NULL;
    }

    dfbi->state = DfbImageComplete;
  }

/*------------------------------------------------------------------------*\
    Non-Files: Create feeder thread, this encapsulates all curl actions
\*------------------------------------------------------------------------*/
  else {
    int rc = pthread_create( &dfbi->thread, NULL, _imageThread, dfbi );
    if( rc ) {
      logerr( "dfbImageGet (%s): Unable to start feeder thread: %s",
              dfbi->uri, strerror(rc) );
      Sfree( dfbi->uri );
      Sfree( dfbi );
      return NULL;
    }
    dfbi->state = DfbImageConnecting;
  }

/*------------------------------------------------------------------------*\
    Init mutex and conditions
\*------------------------------------------------------------------------*/
  pthread_mutexattr_init( &attr );
  pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_ERRORCHECK );
  pthread_mutex_init( &dfbi->mutex, &attr );
  pthread_cond_init( &dfbi->condIsComplete, NULL );

/*------------------------------------------------------------------------*\
    Link entry to list
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &listMutex );
  if( imageList )
    imageList->next = dfbi;
  imageList = dfbi;
  pthread_mutex_unlock( &listMutex );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return dfbi;
}


/*=========================================================================*\
    Release an image instance
\*=========================================================================*/
void dfbImageRelease( DfbImage *dfbi )
{
  DBGMSG( "dfbImageRelease (%p, %s): Now %d references.", dfbi, dfbi->uri, dfbi->refCounter-1 );

  if( --dfbi->refCounter<0 )
    logerr( "dfbImageRelease (%s): Reached negative reference counter %d",
            dfbi->uri, dfbi->refCounter );

  // fixme: garbage collection
}


/*=========================================================================*\
    Release an image instance
\*=========================================================================*/
void dfbImageDelete( DfbImage *dfbi )
{
  DfbImage *walk;
  DBGMSG( "dfbImageDelete (%p, %s): %d references.", dfbi, dfbi->uri, dfbi->refCounter );

/*------------------------------------------------------------------------*\
    Check reference counter
\*------------------------------------------------------------------------*/
  if( dfbi->refCounter>0 )
    logerr( "dfbImageDelete (%s): Reference counter still positive %d",
            dfbi->uri, dfbi->refCounter );

/*------------------------------------------------------------------------*\
    Unlink from list
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &listMutex );
  if( dfbi==imageList )
    imageList = imageList->next;
  else
    for( walk=imageList; walk; walk=walk->next )
      if( walk->next==dfbi )
        walk->next = walk->next->next;
  pthread_mutex_unlock( &listMutex );

/*------------------------------------------------------------------------*\
    Delete mutex and conditions
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &dfbi->mutex );
  pthread_cond_destroy( &dfbi->condIsComplete );

/*------------------------------------------------------------------------*\
    Free all allocated elements
\*------------------------------------------------------------------------*/
  if( dfbi->dfbProvider )
    dfbi->dfbProvider->Release( dfbi->dfbProvider );
  if( dfbi->dfbBuffer )
    dfbi->dfbBuffer->Release( dfbi->dfbBuffer );
   Sfree( dfbi->bufferData );
  Sfree( dfbi->oAuthToken );
  Sfree( dfbi->uri );
  Sfree( dfbi );
}

/*=========================================================================*\
      Clear image buffer
\*=========================================================================*/
void dfbImageClearBuffer( void )
{
  DBGMSG( "dfbImages: clear buffer." );

  // fixme
}



/*=========================================================================*\
      Lock image to avoid concurrent access
\*=========================================================================*/
void dfbImageLock( DfbImage *dfbi )
{
  DBGMSG( "dfbImage (%p,%s): lock.", dfbi, dfbi->uri );
  pthread_mutex_lock( &dfbi->mutex );
}


/*=========================================================================*\
      Unlock image after access or waits
\*=========================================================================*/
void dfbImageUnlock( DfbImage *dfbi )
{
  DBGMSG( "dfbImage (%p,%s): unlock.", dfbi, dfbi->uri );
  pthread_mutex_unlock( &dfbi->mutex );
}


/*=========================================================================*\
    Lock image and wait for completion (or error)
      timeout is in ms, 0 or a negative values are treated as infinity
      returns 0 and locks feed, if condition is met
        std. errode (ETIMEDOUT in case of timeout) and no locking otherwise
\*=========================================================================*/
int dfbImageLaockWaitForComplete( DfbImage *dfbi, int timeout )
{
  struct timeval  now;
  struct timespec abstime;
  int             err = 0;

  DBGMSG( "dfbImageWaitForComplete (%p,%s): waiting for completion (timeout %dms)",
          dfbi, dfbi->uri, timeout );

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   pthread_mutex_lock( &dfbi->mutex );

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
  while( dfbi->state<DfbImageComplete ) {

    // wait for condition
    err = timeout>0 ? pthread_cond_timedwait( &dfbi->condIsComplete, &dfbi->mutex, &abstime )
                    : pthread_cond_wait( &dfbi->condIsComplete, &dfbi->mutex );

    // Break on errors
    if( err )
      break;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  DBGMSG( "dfbImageWaitForComplete (%p,%s): waiting for connection: %s",
          dfbi, dfbi->uri, err?strerror(err):"Locked" );
  return err;
}


/*=========================================================================*\
       Get image uri
\*=========================================================================*/
const char *dfbImageGetUri( DfbImage *dfbi )
{
  return dfbi->uri;
}


/*=========================================================================*\
       Get image state
\*=========================================================================*/
DfbImageState dfbImageGetState( DfbImage *dfbi )
{
  return dfbi->state;
}


/*=========================================================================*\
       Get image provider
\*=========================================================================*/
IDirectFBImageProvider *dfbImageGetProvider( const DfbImage *dfbi )
{
  return dfbi->dfbProvider;
}


/*=========================================================================*\
       A feeder thread
\*=========================================================================*/
static void *_imageThread( void *arg )
{
  DfbImage          *dfbi = arg;
  int                rc;
  struct curl_slist *addedHeaderFields = NULL;

  DBGMSG( "Image loader thread (%p,%s): starting.", dfbi, dfbi->uri );
  PTHREADSETNAME( "loadimage" );

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
  dfbi->curlHandle = curl_easy_init();
  if( !dfbi->curlHandle ) {
    logerr( "Image loader thread (%s): Unable to init cURL.", dfbi->uri );
    dfbi->state = DfbImageError;
    goto end;
  }

  // Set URI
  rc = curl_easy_setopt( dfbi->curlHandle, CURLOPT_URL, dfbi->uri );
  if( rc ) {
    logerr( "Image loader thread (%s): Unable to set URI.", dfbi->uri );
    dfbi->state = DfbImageError;
    goto end;
  }

  // Need oAuth header
  if( dfbi->oAuthToken ) {
    char *hdr = malloc( strlen(dfbi->oAuthToken)+32 );
    sprintf( hdr, "Authorization: Bearer %s", dfbi->oAuthToken );
    addedHeaderFields = curl_slist_append( addedHeaderFields, hdr );  // Performs a strdup(hdr)
    DBGMSG( "Image loader thread (%p,%s): Added request header \"%s\".", dfbi, dfbi->uri, hdr );
    Sfree( hdr );
  }

  // Add headers
  if( addedHeaderFields ) {
    rc = curl_easy_setopt( dfbi->curlHandle, CURLOPT_HTTPHEADER, addedHeaderFields );
    if( rc ) {
      logerr( "Image loader thread (%s): Unable to add HTTP headers.", dfbi->uri );
      dfbi->state = DfbImageError;
      goto end;
    }
  }

  // Set our identity
  rc = curl_easy_setopt( dfbi->curlHandle, CURLOPT_USERAGENT, HttpAgentString );
  if( rc ) {
    logerr( "Image loader thread (%s): unable to set user agent to \"%s\"", dfbi->uri, HttpAgentString );
    dfbi->state = DfbImageError;
    goto end;
  }

  // Enable HTTP redirects
  rc = curl_easy_setopt( dfbi->curlHandle, CURLOPT_FOLLOWLOCATION, 1L );
  if( rc ) {
    logerr( "audioFeedCreate (%s): unable to enable redirects", dfbi->uri );
    dfbi->state = DfbImageError;
    goto end;
  }

  // Set receiver callback
  rc = curl_easy_setopt( dfbi->curlHandle, CURLOPT_WRITEDATA, (void*)dfbi );
  if( rc ) {
    logerr( "Image loader thread (%s): Unable set callback mode.", dfbi->uri );
    dfbi->state = DfbImageError;
    goto end;
  }
  rc = curl_easy_setopt( dfbi->curlHandle, CURLOPT_WRITEFUNCTION, _curlWriteCallback );
  if( rc ) {
    logerr( "Image loader thread (%s): Unable set callback function.", dfbi->uri );
    dfbi->state = DfbImageError;
    goto end;
  }

  // Set tracing callback in debugging mode
#ifdef ICK_TRACECURL
  curl_easy_setopt( dfbi->curlHandle, CURLOPT_DEBUGFUNCTION, _curlTraceCallback );
  curl_easy_setopt( efbi->curlHandle, CURLOPT_VERBOSE, 1L);
#endif

/*------------------------------------------------------------------------*\
    Collect data, this represents the thread main loop
\*------------------------------------------------------------------------*/
  dfbi->state = DfbImageConnecting;
  rc = curl_easy_perform( dfbi->curlHandle );
  if( rc!=CURLE_OK ) {
    logerr( "Image loader thread (%p,%s): %s", dfbi, dfbi->uri, curl_easy_strerror(rc) );
    dfbi->state = DfbImageError;
  }
  DBGMSG( "Image loader thread (%p,%s): terminating with curl state \"%s\".",
          dfbi, dfbi->uri, curl_easy_strerror(rc) );

end:

/*------------------------------------------------------------------------*\
    Clean up curl
\*------------------------------------------------------------------------*/
  if( dfbi->curlHandle )
    curl_easy_cleanup( dfbi->curlHandle );
  dfbi->curlHandle = NULL;
  if( addedHeaderFields )
    curl_slist_free_all( addedHeaderFields );

/*------------------------------------------------------------------------*\
    Create direct frame buffer image provider
\*------------------------------------------------------------------------*/
  if( dfbi->state == DfbImageLoading ) {
    DFBDataBufferDescription ddsc;
    ddsc.flags         = DBDESC_MEMORY;
    ddsc.memory.data   = dfbi->bufferData;
    ddsc.memory.length = dfbi->bufferLen;
    DFBResult drc =  dfbi->dfb->CreateDataBuffer( dfbi->dfb, &ddsc, &dfbi->dfbBuffer);
    if( drc!=DFB_OK ) {
      logerr( "Image loader thread (p,%s): could not create data buffer (%s).",
              dfbi, dfbi->uri, DirectFBErrorString(drc) );
      dfbi->state = DfbImageError;
     }
  }
  if( dfbi->state == DfbImageLoading ) {
    DFBResult drc = dfbi->dfbBuffer->CreateImageProvider( dfbi->dfbBuffer, &dfbi->dfbProvider );
    dfbi->state = DfbImageComplete;
    if( drc!=DFB_OK ) {
      logerr( "Image loader thread (p,%s): could not create image provider (%s).",
              dfbi, dfbi->uri, DirectFBErrorString(drc) );
      dfbi->state = DfbImageError;
    }
    pthread_cond_signal( &dfbi->condIsComplete );
  }

/*------------------------------------------------------------------------*\
    That's all ...
\*------------------------------------------------------------------------*/
  DBGMSG( "Image loader thread (%p,%s): terminated.", dfbi, dfbi->uri );
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
  DfbImage  *dfbi   = userp;

  DBGMSG( "Image loader thread (%s): receiving %ld bytes", dfbi->uri, (long)size );
  //DBGMEM( "Raw feed", buffer, size );

/*------------------------------------------------------------------------*\
    Feed termination requested?
\*------------------------------------------------------------------------*/
  if( dfbi->state>DfbImageLoading )
    return errVal;
  dfbi->state = DfbImageLoading;

/*------------------------------------------------------------------------*\
    Try to create or expand buffer
\*------------------------------------------------------------------------*/
  if( dfbi->bufferData )
    dfbi->bufferData = realloc( dfbi->bufferData, dfbi->bufferLen+size );
  else
    dfbi->bufferData = malloc( size );

  if( !dfbi->bufferData ) {
    logerr( "Image loader thread (%s): Out of memory!", dfbi->uri );
    return errVal;
  }

/*------------------------------------------------------------------------*\
    Append data to buffer
\*------------------------------------------------------------------------*/
  memcpy( dfbi->bufferData+dfbi->bufferLen, buffer, size );
  dfbi->bufferLen += size;

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
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




