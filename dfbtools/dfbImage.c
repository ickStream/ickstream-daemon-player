/*$*********************************************************************\

Name            : -

Source File     : dfbImage.c

Description     : Image widget of dfb toolkit

Comments        : implements a buffer for images and allows primitive
                  asynchronous loading

Called by       :

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
#include <sys/time.h>
#include <curl/curl.h>

#include "dfbTools.h"
#include "dfbToolsInternal.h"
#include "ickpd.h"
#include "ickutils.h"


/*=========================================================================*\
    Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
    Macro and type definitions
\*=========================================================================*/

// Cache item
typedef struct _imageCacheItem {
  volatile DfbtImageState  state;
  int                      refCounter;
  double                   lastaccess;
  struct _imageCacheItem  *next;               // weak
  char                    *uri;                // strong
  char                    *oAuthToken;         // strong
  IDirectFBDataBuffer     *dfbBuffer;          // strong
  IDirectFBImageProvider  *dfbProvider;        // strong
  char                    *bufferData;         // strong
  size_t                   bufferLen;
  CURL                    *curlHandle;
  pthread_t                thread;
  pthread_mutex_t          mutex;
  pthread_cond_t           condIsComplete;
} ImageCacheItem;

// Extra widget data
typedef struct {
  DfbtWidget              *nextImage;          // linked lists of images
  ImageCacheItem          *cacheItem;          // strong
} DfbtImageData;

#define NEXTIMAGE(widget) (((DfbtImageData*)widget->data)->nextImage)


/*=========================================================================*\
    Private symbols
\*=========================================================================*/
static pthread_mutex_t   cacheMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t   imageMutex = PTHREAD_MUTEX_INITIALIZER;
static ImageCacheItem   *cacheList;
static DfbtWidget       *imageList;


/*=========================================================================*\
    Private prototypes
\*=========================================================================*/
static ImageCacheItem *_cacheGetImage( const char *theuri, bool isfile );
void _cacheItemRelease( ImageCacheItem *dfbi );


static void *_imageLoaderThread( void *arg );
static size_t _curlWriteCallback( void *buffer, size_t size, size_t nmemb, void *userp );
#ifdef ICK_TRACECURL
static int    _curlTraceCallback( CURL *handle, curl_infotype type, char *data, size_t size, void *userp );
#endif


/*=========================================================================*\
    Create an image widget
\*=========================================================================*/
DfbtWidget *dfbtImage( int width, int height, const char *uri, bool isFile )
{
  ImageCacheItem          *cacheItem;
  DfbtWidget              *widget;
  DfbtImageData           *imageData;
  DFBSurfaceRenderOptions  ropts;

  DBGMSG( "dfbtImage: \"%s\"", uri );

/*------------------------------------------------------------------------*\
    Request image from cache
\*------------------------------------------------------------------------*/
  cacheItem  = _cacheGetImage( uri, isFile );
  if( !cacheItem )
    return NULL;

  // fixme: get size and surface setting from image

/*------------------------------------------------------------------------*\
    Create widget
\*------------------------------------------------------------------------*/
  widget = _dfbtNewWidget( DfbtImage, width, height );
  if( !widget )
    return NULL;

/*------------------------------------------------------------------------*\
    Set render options for scaling
\*------------------------------------------------------------------------*/
  ropts = DSRO_SMOOTH_UPSCALE | DSRO_SMOOTH_DOWNSCALE;
  _dfbtSurfaceLock( widget->surface );
  widget->surface->SetRenderOptions( widget->surface, ropts );
  _dfbtSurfaceUnlock( widget->surface );

/*------------------------------------------------------------------------*\
    Allocate and init additional data
\*------------------------------------------------------------------------*/
  imageData = calloc( 1, sizeof(DfbtImageData) );
  if( !imageData ) {
    logerr( "dfbtImage: out of memory!" );
    _dfbtWidgetDestruct( widget );
    return NULL;
  }
  imageData->cacheItem  = cacheItem;
  widget->data          = imageData;

/*------------------------------------------------------------------------*\
    Default name
\*------------------------------------------------------------------------*/
  dfbtSetName( widget, uri );

/*------------------------------------------------------------------------*\
    Link to list of images
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &imageMutex );
  imageData->nextImage = imageList;
  imageList            = widget;
  pthread_mutex_unlock( &imageMutex );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return widget;
}


/*=========================================================================*\
       Get image uri
\*=========================================================================*/
const char *dfbtImageGetUri( DfbtWidget *widget )
{
  DfbtImageData  *imageData = widget->data;
  ImageCacheItem *cacheItem = imageData->cacheItem;
  return cacheItem->uri;
}


/*=========================================================================*\
       Get image state
\*=========================================================================*/
DfbtImageState dfbtImageGetState( DfbtWidget *widget )
{
  DfbtImageData *imageData = widget->data;
  ImageCacheItem *cacheItem = imageData->cacheItem;
  return cacheItem->state;
}


/*=========================================================================*\
    Lock image and wait for completion (or error)
      timeout is in ms, 0 or a negative values are treated as infinity
      returns 0 if condition is met
        std. errode (ETIMEDOUT in case of timeout) and no locking otherwise
        image is unlocked in any case
\*=========================================================================*/
int dfbtImageWaitForComplete( DfbtWidget *widget, int timeout )
{
  DfbtImageData   *imageData = widget->data;
  ImageCacheItem  *cacheItem = imageData->cacheItem;
  struct timeval   now;
  struct timespec  abstime;
  int              err = 0;

  DBGMSG( "dfbImageWaitForComplete (%p,%s): waiting for completion (timeout %dms)",
          cacheItem, cacheItem->uri, timeout );

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   pthread_mutex_lock( &cacheItem->mutex );

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
  while( cacheItem->state<DfbtImageComplete ) {

    // wait for condition
    err = timeout>0 ? pthread_cond_timedwait( &cacheItem->condIsComplete, &cacheItem->mutex, &abstime )
                    : pthread_cond_wait( &cacheItem->condIsComplete, &cacheItem->mutex );

    // Break on errors
    if( err )
      break;
  }

  if( !err )
    pthread_mutex_unlock( &cacheItem->mutex );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  DBGMSG( "dfbImageWaitForComplete (%p,%s): %s",
          cacheItem, cacheItem->uri, strerror(err) );
  return err;
}


/*=========================================================================*\
    Destruct type specific part of a image widget
\*=========================================================================*/
void _dfbtImageDestruct( DfbtWidget *widget )
{
  DfbtImageData *imageData = widget->data;
  DfbtWidget    *walk, *last;

  DBGMSG( "_dfbImageDestruct (%p): \"%s\"", widget, imageData->cacheItem->uri );

/*------------------------------------------------------------------------*\
    Unlink from list of images
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &imageMutex );
  for( walk=imageList,last=NULL; walk; last=walk,walk=NEXTIMAGE(walk) ) {
    if( walk==widget ) {
      if( !last )
        imageList = NEXTIMAGE(walk);
      else
        NEXTIMAGE(last) = NEXTIMAGE(walk);
      break;
    }
  }
  if( !walk )
    logerr( "_dfbtImageDestruct (%s): not in image list.", widget->name );
  pthread_mutex_unlock( &imageMutex );

/*------------------------------------------------------------------------*\
    Release cache item and free widget specific part
\*------------------------------------------------------------------------*/
  _cacheItemRelease( imageData->cacheItem );
  Sfree( widget->data );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
    Draw image widget
\*=========================================================================*/
void _dfbtImageDraw( DfbtWidget *widget )
{
  IDirectFBSurface  *surf      = widget->surface;
  DfbtImageData     *imageData = widget->data;
  ImageCacheItem    *cacheItem = imageData->cacheItem;
  DFBResult          drc;

  DBGMSG( "_dfbtImageDraw (%p): \"%s\" ", widget, cacheItem->uri );

/*------------------------------------------------------------------------*\
    We can do nothing if cache item in in error condition.
\*------------------------------------------------------------------------*/
  if( cacheItem->state==DfbtImageError ) {
    DBGMSG( "_dfbtImageDraw (%p,%s): drawing suspended, cache item in error state",
            widget, cacheItem->uri );
    return;
  }

/*------------------------------------------------------------------------*\
    We can do nothing if item is not complete.
\*------------------------------------------------------------------------*/
  if( cacheItem->state!=DfbtImageComplete ) {
    DBGMSG( "_dfbtImageDraw (%p,%s): drawing suspended, cache item not complete",
            widget, cacheItem->uri );
    return;
  }

/*------------------------------------------------------------------------*\
    Lazily create direct frame buffer and image provider
\*------------------------------------------------------------------------*/
  if( !cacheItem->dfbProvider ) {
    if( !cacheItem->dfbBuffer ) {
      IDirectFB *dfb = dfbtGetDdb();
      DFBDataBufferDescription ddsc;
      ddsc.flags         = DBDESC_MEMORY;
      ddsc.memory.data   = cacheItem->bufferData;
      ddsc.memory.length = cacheItem->bufferLen;
      drc =  dfb->CreateDataBuffer( dfb, &ddsc, &cacheItem->dfbBuffer);
      if( drc!=DFB_OK ) {
        logerr( "_dfbtImageDraw (p,%s): could not create data buffer (%s).",
                cacheItem, cacheItem->uri, DirectFBErrorString(drc) );
        cacheItem->state = DfbtImageError;
        return;
       }
    }
    drc = cacheItem->dfbBuffer->CreateImageProvider( cacheItem->dfbBuffer, &cacheItem->dfbProvider );
    cacheItem->state = DfbtImageComplete;
    if( drc!=DFB_OK ) {
      logerr( "_dfbtImageDraw (p,%s): could not create image provider (%s).",
              cacheItem, cacheItem->uri, DirectFBErrorString(drc) );
      cacheItem->state = DfbtImageError;
      return;
    }
  }

/*------------------------------------------------------------------------*\
    Draw image to surface
\*------------------------------------------------------------------------*/
  drc = cacheItem->dfbProvider->RenderTo( cacheItem->dfbProvider, surf, NULL );
  if( drc!=DFB_OK ) {
    logerr( "_dfbtImageDraw: could not render image \"%s\" (%s).",
        cacheItem->uri, DirectFBErrorString(drc) );
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
    Get or create an cached image item
\*=========================================================================*/
static ImageCacheItem *_cacheGetImage( const char *uri, bool isfile )
{
  ImageCacheItem      *cacheItem;
  char                *theuri;

  DBGMSG( "_chacheGetImage: \"%s\"", uri );

/*------------------------------------------------------------------------*\
    Create full path for non-absolute file names
\*------------------------------------------------------------------------*/
  if( isfile && _dfbtResourcePath && *uri!='/' ) {
    theuri = malloc( strlen(_dfbtResourcePath)+strlen(uri)+2 );
    sprintf( theuri, "%s/%s", _dfbtResourcePath, uri );
  }
  else
    theuri = strdup( uri );

/*------------------------------------------------------------------------*\
    Already in cache?
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &cacheMutex );
  for( cacheItem=cacheList; cacheItem; cacheItem=cacheItem->next )
    if( !strcmp(theuri,cacheItem->uri) )
      break;
  if( cacheItem ) {
    cacheItem->refCounter++;
    pthread_mutex_unlock( &cacheMutex );
    DBGMSG( "_cacheGetImage (%p): \"%s\" found in cache, now %d references",
            cacheItem, theuri, cacheItem->refCounter );
    cacheItem->lastaccess = srvtime();
    free( theuri );
    return cacheItem;
  }
  DBGMSG( "_cacheGetImage (%s): no cache hit, will load...", theuri );
  pthread_mutex_unlock( &cacheMutex );

/*------------------------------------------------------------------------*\
    Create object
\*------------------------------------------------------------------------*/
  cacheItem = calloc( 1, sizeof(ImageCacheItem) );
  if( !cacheItem ) {
    logerr( "_cacheGetImage (%s): out of memory!", uri );
    free( theuri );
    return NULL;
  }
  cacheItem->refCounter = 1;
  cacheItem->state      = DfbtImageInitialized;
  cacheItem->uri        = theuri;   // already dupped

/*------------------------------------------------------------------------*\
    Files: Try to read directly
\*------------------------------------------------------------------------*/
  if( isfile ) {
    IDirectFB *dfb = dfbtGetDdb();
    DFBResult drc = dfb->CreateImageProvider( dfb, cacheItem->uri, &cacheItem->dfbProvider );
    if( drc!=DFB_OK ) {
      logerr( "_cacheGetImage (%s): could not create image provider (%s).",
              cacheItem->uri, DirectFBErrorString(drc) );
      Sfree( cacheItem->uri );
      Sfree( cacheItem );
      return NULL;
    }
    cacheItem->state = DfbtImageComplete;
  }

/*------------------------------------------------------------------------*\
    Non-Files: Create loader thread, which encapsulates all curl actions
\*------------------------------------------------------------------------*/
  else {
    int rc = pthread_create( &cacheItem->thread, NULL, _imageLoaderThread, cacheItem );
    if( rc ) {
      logerr( "dfbImageGet (%s): Unable to start feeder thread: %s",
              cacheItem->uri, strerror(rc) );
      Sfree( cacheItem->uri );
      Sfree( cacheItem );
      return NULL;
    }
    cacheItem->state = DfbtImageConnecting;
  }

/*------------------------------------------------------------------------*\
    Init mutex and conditions
\*------------------------------------------------------------------------*/
  ickMutexInit( &cacheItem->mutex );
  pthread_cond_init( &cacheItem->condIsComplete, NULL );

/*------------------------------------------------------------------------*\
    Link entry to list
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &cacheMutex );
  cacheItem->next = cacheList;
  cacheList = cacheItem;
  pthread_mutex_unlock( &cacheMutex );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return cacheItem;
}


/*=========================================================================*\
    Release an image instance
\*=========================================================================*/
void _cacheItemRelease( ImageCacheItem *dfbi )
{
  DBGMSG( "_cacheItemRelease (%p, %s): Now %d references.", dfbi, dfbi->uri, dfbi->refCounter-1 );

  if( --dfbi->refCounter<0 )
    logerr( "_cacheItemRelease (%s): Reached negative reference counter %d",
            dfbi->uri, dfbi->refCounter );

  // fixme: garbage collection
}


/*=========================================================================*\
    Destruct an image instance
\*=========================================================================*/
void _cacheItemDestruct( ImageCacheItem *cacheItem )
{
  ImageCacheItem *walk;
  DBGMSG( "_cacheItemDestruct (%p, %s): %d references.",
          cacheItem, cacheItem->uri, cacheItem->refCounter );

/*------------------------------------------------------------------------*\
    Check reference counter
\*------------------------------------------------------------------------*/
  if( cacheItem->refCounter>0 )
    logerr( "dfbImageDelete (%s): Reference counter still positive %d",
            cacheItem->uri, cacheItem->refCounter );

/*------------------------------------------------------------------------*\
    Unlink from list
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &cacheMutex );
  if( cacheItem==cacheList )
    cacheList = cacheList->next;
  else
    for( walk=cacheList; walk; walk=walk->next )
      if( walk->next==cacheItem )
        walk->next = walk->next->next;
  pthread_mutex_unlock( &cacheMutex );

/*------------------------------------------------------------------------*\
    Delete mutex and conditions
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &cacheItem->mutex );
  pthread_cond_destroy( &cacheItem->condIsComplete );

/*------------------------------------------------------------------------*\
    Free all allocated elements
\*------------------------------------------------------------------------*/
  if( cacheItem->dfbProvider )
    DFBRELEASE( cacheItem->dfbProvider );
  if( cacheItem->dfbBuffer )
    DFBRELEASE( cacheItem->dfbBuffer );
  Sfree( cacheItem->bufferData );
  Sfree( cacheItem->oAuthToken );
  Sfree( cacheItem->uri );
  Sfree( cacheItem );
}


/*=========================================================================*\
       A feeder thread
\*=========================================================================*/
static void *_imageLoaderThread( void *arg )
{
  ImageCacheItem    *cacheItem = arg;
  int                rc;
  struct curl_slist *addedHeaderFields = NULL;

  DBGMSG( "Image loader thread (%p,%s): starting.", cacheItem, cacheItem->uri );
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
  cacheItem->curlHandle = curl_easy_init();
  if( !cacheItem->curlHandle ) {
    logerr( "Image loader thread (%s): Unable to init cURL.", cacheItem->uri );
    cacheItem->state = DfbtImageError;
    goto end;
  }

  // Set URI
  rc = curl_easy_setopt( cacheItem->curlHandle, CURLOPT_URL, cacheItem->uri );
  if( rc ) {
    logerr( "Image loader thread (%s): Unable to set URI.", cacheItem->uri );
    cacheItem->state = DfbtImageError;
    goto end;
  }

  // Need oAuth header
  if( cacheItem->oAuthToken ) {
    char *hdr = malloc( strlen(cacheItem->oAuthToken)+32 );
    sprintf( hdr, "Authorization: Bearer %s", cacheItem->oAuthToken );
    addedHeaderFields = curl_slist_append( addedHeaderFields, hdr );  // Performs a strdup(hdr)
    DBGMSG( "Image loader thread (%p,%s): Added request header \"%s\".", cacheItem, cacheItem->uri, hdr );
    Sfree( hdr );
  }

  // Add headers
  if( addedHeaderFields ) {
    rc = curl_easy_setopt( cacheItem->curlHandle, CURLOPT_HTTPHEADER, addedHeaderFields );
    if( rc ) {
      logerr( "Image loader thread (%s): Unable to add HTTP headers.", cacheItem->uri );
      cacheItem->state = DfbtImageError;
      goto end;
    }
  }

  // Set our identity
  rc = curl_easy_setopt( cacheItem->curlHandle, CURLOPT_USERAGENT, HttpAgentString );
  if( rc ) {
    logerr( "Image loader thread (%s): unable to set user agent to \"%s\"", cacheItem->uri, HttpAgentString );
    cacheItem->state = DfbtImageError;
    goto end;
  }

  // Enable HTTP redirects
  rc = curl_easy_setopt( cacheItem->curlHandle, CURLOPT_FOLLOWLOCATION, 1L );
  if( rc ) {
    logerr( "audioFeedCreate (%s): unable to enable redirects", cacheItem->uri );
    cacheItem->state = DfbtImageError;
    goto end;
  }

  // Set receiver callback
  rc = curl_easy_setopt( cacheItem->curlHandle, CURLOPT_WRITEDATA, (void*)cacheItem );
  if( rc ) {
    logerr( "Image loader thread (%s): Unable set callback mode.", cacheItem->uri );
    cacheItem->state = DfbtImageError;
    goto end;
  }
  rc = curl_easy_setopt( cacheItem->curlHandle, CURLOPT_WRITEFUNCTION, _curlWriteCallback );
  if( rc ) {
    logerr( "Image loader thread (%s): Unable set callback function.", cacheItem->uri );
    cacheItem->state = DfbtImageError;
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
  cacheItem->state = DfbtImageConnecting;
  rc = curl_easy_perform( cacheItem->curlHandle );
  if( rc!=CURLE_OK ) {
    logerr( "Image loader thread (%p,%s): %s", cacheItem, cacheItem->uri, curl_easy_strerror(rc) );
    cacheItem->state = DfbtImageError;
  }
  DBGMSG( "Image loader thread (%p,%s): terminating with curl state \"%s\".",
          cacheItem, cacheItem->uri, curl_easy_strerror(rc) );

end:

/*------------------------------------------------------------------------*\
    Clean up curl
\*------------------------------------------------------------------------*/
  if( cacheItem->curlHandle )
    curl_easy_cleanup( cacheItem->curlHandle );
  cacheItem->curlHandle = NULL;
  if( addedHeaderFields )
    curl_slist_free_all( addedHeaderFields );

/*------------------------------------------------------------------------*\
    Mark cache item as complete
\*------------------------------------------------------------------------*/
  if( cacheItem->state == DfbtImageLoading )
    cacheItem->state = DfbtImageComplete;

/*------------------------------------------------------------------------*\
    Mark all images referencing this item for redraw
\*------------------------------------------------------------------------*/
  DfbtWidget *walk;
  pthread_mutex_lock( &imageMutex );
  for( walk=imageList; walk; walk=NEXTIMAGE(walk) ) {
    DfbtImageData  *imageData = walk->data;
    if( imageData->cacheItem==cacheItem ) {
      DBGMSG( "Image loader thread (%s): marking widget %p (%s) for update",
              cacheItem->uri, walk, walk->name );
      walk->needsUpdate = true;
    }
  }
  pthread_mutex_unlock( &imageMutex );

/*------------------------------------------------------------------------*\
    Signal item completeness and set redraw flag
\*------------------------------------------------------------------------*/
  pthread_cond_signal( &cacheItem->condIsComplete );
  if( _dfbtRedrawRequestPtr )
    *_dfbtRedrawRequestPtr = true;

/*------------------------------------------------------------------------*\
    That's all ...
\*------------------------------------------------------------------------*/
  DBGMSG( "Image loader thread (%p,%s): terminated.", cacheItem, cacheItem->uri );
  return NULL;
}


/*=========================================================================*\
      cURL write callback
\*=========================================================================*/
static size_t _curlWriteCallback( void *buffer, size_t size, size_t nmemb, void *userp )
{
  size                     *= nmemb;       // get real size in bytes
  size_t          retVal    = size;        // Ok
  size_t          errVal    = size+1;      // Signal error by size mismatch
  ImageCacheItem *cacheItem = userp;

  DBGMSG( "Image loader thread (%s): receiving %ld bytes",
          cacheItem->uri, (long)size );

/*------------------------------------------------------------------------*\
    Feed termination requested?
\*------------------------------------------------------------------------*/
  if( cacheItem->state>DfbtImageLoading )
    return errVal;
  cacheItem->state = DfbtImageLoading;

/*------------------------------------------------------------------------*\
    Try to create or expand buffer
\*------------------------------------------------------------------------*/
  if( cacheItem->bufferData )
    cacheItem->bufferData = realloc( cacheItem->bufferData, cacheItem->bufferLen+size );
  else
    cacheItem->bufferData = malloc( size );

  if( !cacheItem->bufferData ) {
    logerr( "Image loader thread (%s): Out of memory!", cacheItem->uri );
    return errVal;
  }

/*------------------------------------------------------------------------*\
    Append data to buffer
\*------------------------------------------------------------------------*/
  memcpy( cacheItem->bufferData+cacheItem->bufferLen, buffer, size );
  cacheItem->bufferLen += size;

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




