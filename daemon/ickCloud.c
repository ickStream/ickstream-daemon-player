/*$*********************************************************************\

Name            : -

Source File     : ickCloud.c

Description     : manage ickstream could services

Comments        : -

Called by       : ickstream wrapper 

Calls           : 

Error Messages  : -
  
Date            : 10.04.2013

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

#include <string.h>
#include <pthread.h>
#include <curl/curl.h>
#include <jansson.h>

#include "ickpd.h"
#include "ickutils.h"
#include "player.h"
#include "ickCloud.h"


/*=========================================================================*\
    Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
    Private definitions and symbols
\*=========================================================================*/
typedef struct {
  char              *uri;          // strong
  char              *oAuthToken;   // strong
  char              *method;       // string
  json_t            *jParams;      // strong
  long               id;
  IckCloudCb         callback;
  void              *userData;     // weak
} CloudRequest;


/*=========================================================================*\
    Private prototypes
\*=========================================================================*/
static size_t  _curlWriteCallback( void *buffer, size_t size, size_t nmemb, void *userp );
static void   *_cloudRequestThread( void *arg );


/*=========================================================================*\
    Set current device IP address
\*=========================================================================*/
int ickCloudSetDeviceAddress( void )
{
  const char *token;
  const char *deviceId;
  const char *deviceAddress;
  json_t     *jParams, *jResult;

/*------------------------------------------------------------------------*\
    Need token...
\*------------------------------------------------------------------------*/
  token = playerGetToken();
  if( !token ) {
    logwarn( "ickCloudSetDeviceAdress: No device token set." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    ... and Id ...
\*------------------------------------------------------------------------*/
  deviceId = playerGetUUID();

/*------------------------------------------------------------------------*\
    ... and address
\*------------------------------------------------------------------------*/
  deviceAddress = playerGetIpAddress();
  if( !deviceAddress ) {
    logwarn( "ickCloudSetDeviceAddress: No device IP address set." );
    return -1;
  }

  DBGMSG( "ickCloudSetDeviceAddress (%s): %s", deviceId, deviceAddress );

/*------------------------------------------------------------------------*\
    Collect parameters
\*------------------------------------------------------------------------*/
  jParams = json_pack( "{ssss}",
                       "deviceId", deviceId,
                       "address",  deviceAddress );

/*------------------------------------------------------------------------*\
    Interact with cloud
\*------------------------------------------------------------------------*/
  jResult = ickCloudRequestSync( NULL, playerGetToken(), "setDeviceAddress", jParams );
  json_decref( jParams );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  if( jResult )
    json_decref( jResult );
  return 0;
}


/*=========================================================================*\
    Send a JSON request (synchronous mode)
      uri can b NULL to use the standard ickstream cloud endpoint
\*=========================================================================*/
json_t *ickCloudRequestSync( const char *uri, const char *oAuthToken, const char *method, json_t *jParams )
{
  json_t *jResult;
  long    id;
  int     rc;

  // Get ID
  id = getAndIncrementCounter();

  // Do transaction
  rc = jsonRpcTransact( uri, oAuthToken, id, method, jParams, &jResult );
  if( rc )
    return NULL;

  // Return result
  return jResult;
}


/*=========================================================================*\
    Send a JSON notification
      uri can be NULL to use the standard ickstream cloud endpoint
\*=========================================================================*/
int ickCloudNotify( const char *uri, const char *oAuthToken, const char *method, json_t *jParams )
{
  return jsonRpcTransact( uri, oAuthToken, 0, method, jParams, NULL );
}


/*=========================================================================*\
    Send a JSON request (asynchronous mode)
      uri can be NULL to use the standard ickstream cloud endpoint
\*=========================================================================*/
int ickCloudRequestAsync( const char *uri, const char *oAuthToken, const char *method,
                          json_t *jParams, IckCloudCb callback, void *userData )
{
  CloudRequest *request;
  pthread_t     thread;
  long          id;
  int           rc;

/*------------------------------------------------------------------------*\
    Get id
\*------------------------------------------------------------------------*/
  id = getAndIncrementCounter();

/*------------------------------------------------------------------------*\
    Create and init request object
\*------------------------------------------------------------------------*/
  request = calloc( 1, sizeof(CloudRequest) );
  if( !request ) {
    logerr( "ickCloudRequestAsync (%s): out of memory!", method );
    return -1;
  }
  request->uri        = uri ? strdup( uri ) : NULL;
  request->oAuthToken = strdup( oAuthToken );
  request->method     = strdup( method );
  request->jParams    = json_incref( jParams );
  request->id         = id;
  request->callback   = callback;
  request->userData   = userData;

/*------------------------------------------------------------------------*\
    Create thread
\*------------------------------------------------------------------------*/
  rc = pthread_create( &thread, NULL, _cloudRequestThread, request );
  if( rc ) {
    logerr( "ickCloudRequestAsync (%s): Unable to start request thread (%s).", method, strerror(rc) );
    json_decref( jParams );
    Sfree( request->uri );
    Sfree( request->oAuthToken );
    Sfree( request->method );
    Sfree( request );
    return -1;
  }

/*------------------------------------------------------------------------*\
    We don't care for that thread any more
\*------------------------------------------------------------------------*/
  rc = pthread_detach( thread );
  if( rc )
    logerr( "ickCloudRequestAsync (%s): Unable to detach request thread (%s).", method, strerror(rc) );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
       A thread for an asynchronous cloud request
\*=========================================================================*/
static void *_cloudRequestThread( void *arg )
{
  CloudRequest *request = (CloudRequest*)arg;
  int           rc;
  json_t       *jResult = NULL;

  DBGMSG( "Cloud Request thread (%p,%s): starting.", request, request->method );
  PTHREADSETNAME( "cloudReq" );

/*------------------------------------------------------------------------*\
    Do the transaction
\*------------------------------------------------------------------------*/
  rc = jsonRpcTransact( request->uri, request->oAuthToken, request->id,
                        request->method, request->jParams, &jResult );
  DBGMSG( "Cloud Request thread (%p,%s): Performed request (%d).",
          request, request->method, rc );

/*------------------------------------------------------------------------*\
    Call callback (if any)
\*------------------------------------------------------------------------*/
  if( request->callback ) {
    DBGMSG( "Cloud Request thread (%p,%s): Calling call back function.",
            request, request->method, rc );
    request->callback( request->method, request->jParams, jResult, rc, request->userData );
  }

/*------------------------------------------------------------------------*\
    Clean up
\*------------------------------------------------------------------------*/
  DBGMSG( "Cloud Request thread (%p,%s): Done.", request, request->method );
  json_decref( request->jParams );
  if( jResult )
    json_decref( jResult );
  Sfree( request->uri );
  Sfree( request->oAuthToken );
  Sfree( request->method );
  Sfree( request );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return NULL;
}


/*=========================================================================*\
    Perform a generic JSON-RPC transaction
      uri     - server HTTP interface address,
                if NULL the default ickstream endpoint is used
      oAuth   - oAuth header token (NULL for none)
      method  - method name
      id      - JSON-RPC id (0 for notifications)
      jParams - parameters (NULL for none)
      jResult - pointer to result (can be NULL for notifications)
    returns -1 on error
\*=========================================================================*/
int jsonRpcTransact( const char *uri, const char *oAuthToken, int id,
                     const char *method, json_t *jParams, json_t **jResult )
{
  json_t            *jCmd         = NULL;
  char              *cmdStr       = NULL;
  CURL              *curlHandle   = NULL;
  struct curl_slist *headers      = NULL;
  char              *receivedData = NULL;
  int                retval       = 0;
  int                rc;

/*------------------------------------------------------------------------*\
    Use default URI?
\*------------------------------------------------------------------------*/
  if( !uri )
    uri = IckCloudCoreURI;

/*------------------------------------------------------------------------*\
    Be verbose
\*------------------------------------------------------------------------*/
#ifdef ICKDEBUG
  char *txt = json_dumps( jParams, JSON_PRESERVE_ORDER | JSON_COMPACT | JSON_ENSURE_ASCII );
  DBGMSG( "jsonRpcTransact (%s): %s id=%ld, params=\"%s\".", uri, method, id, txt );
  Sfree( txt );
#endif

/*------------------------------------------------------------------------*\
    Build full Message
\*------------------------------------------------------------------------*/
  jCmd = json_pack( "{ssss}",
                        "jsonrpc", "2.0",
                        "method", method );
  if( id>0 )
    json_object_set( jCmd, "id", json_integer(id) );
  if( jParams )
    json_object_set( jCmd, "params", jParams );
  cmdStr = json_dumps( jCmd, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_ENSURE_ASCII );
  if( !cmdStr ) {
    logerr( "jsonRpcTransact (%s): Unable to build command string.", uri );
    retval = -1;
    goto end;
  }

/*------------------------------------------------------------------------*\
    Setup cURL
\*------------------------------------------------------------------------*/
  curlHandle = curl_easy_init();
  if( !curlHandle ) {
    logerr( "jsonRpcTransact (%s): Unable to init cURL.", uri );
    retval = -1;
    goto end;
  }

  // Set URI
  rc = curl_easy_setopt( curlHandle, CURLOPT_URL, uri );
  if( rc ) {
    logerr( "jsonRpcTransact (%s): Unable to set URI.", uri );
    retval = -1;
    goto end;
  }

  // Set our identity
  rc = curl_easy_setopt( curlHandle, CURLOPT_USERAGENT, HttpAgentString );
  if( rc ) {
    logerr( "jsonRpcTransact (%s): Unable to set user agent to \"%s\".", uri, HttpAgentString );
    retval = -1;
    goto end;
  }

  // Enable HTTP redirects
  rc = curl_easy_setopt( curlHandle, CURLOPT_FOLLOWLOCATION, 1L );
  if( rc ) {
    logerr( "jsonRpcTransact (%s): Unable to enable redirects.", uri );
    retval = -1;
    goto end;
  }

  // Construct and set HTTP header lines
  headers = curl_slist_append( headers, "Content-Type: application/json; charset=UTF-8" );
  if( oAuthToken ) {
    char *hdr = malloc( strlen(oAuthToken)+32 );
    sprintf( hdr, "Authorization: Bearer %s", oAuthToken );
    headers = curl_slist_append( headers, hdr );  // Performs a strdup(hdr)
    Sfree( hdr );
  }
  rc = curl_easy_setopt( curlHandle, CURLOPT_HTTPHEADER, headers );
  if( rc ) {
    logerr( "jsonRpcTransact (%s): Unable to set HTTP headers.", uri );
    retval = -1;
    goto end;
  }

  // Set HTTP request
  rc = curl_easy_setopt( curlHandle, CURLOPT_CUSTOMREQUEST, "POST" );
  if( rc ) {
    logerr( "jsonRpcTransact (%s): Unable to set HTTP request.", uri );
    retval = -1;
    goto end;
  }

  // Set payload
  rc = curl_easy_setopt( curlHandle, CURLOPT_POSTFIELDS, cmdStr );
  if( rc ) {
    logerr( "jsonRpcTransact (%s): Unable to set HTTP payload.", uri );
    retval = -1;
    goto end;
  }

  // Set receiver callback
  rc = curl_easy_setopt( curlHandle, CURLOPT_WRITEDATA, &receivedData );
  if( rc ) {
    logerr( "jsonRpcTransact (%s): Unable set callback pointer.", uri );
    retval = -1;
    goto end;
  }
  rc = curl_easy_setopt( curlHandle, CURLOPT_WRITEFUNCTION, _curlWriteCallback );
  if( rc ) {
    logerr( "jsonRpcTransact (%s): Unable set callback function.", uri );
    retval = -1;
    goto end;
  }

/*------------------------------------------------------------------------*\
    Collect data.
\*------------------------------------------------------------------------*/
  DBGMSG( "jsonRpcTransact (%s): send \"%s\".", uri, cmdStr );
  rc = curl_easy_perform( curlHandle );
  DBGMSG( "jsonRpcTransact (%s): curl returned with state \"%s\".",
          uri, curl_easy_strerror(rc) );
  DBGMSG( "jsonRpcTransact (%s): received \"%s\".", uri, receivedData );
  if( rc!=CURLE_OK ) {
    logerr( "jsonRpcTransact (%s): %s", uri, curl_easy_strerror(rc) );
    retval = -1;
    goto end;
  }

/*------------------------------------------------------------------------*\
    Notification: expect no data
\*------------------------------------------------------------------------*/
  if( !id ) {
    if( receivedData )
      logwarn( "jsonRpcTransact (%s): Received data for notification (%s).",
               uri, receivedData );
  }

/*------------------------------------------------------------------------*\
    Request: expect data
\*------------------------------------------------------------------------*/
  else if( !receivedData ) {
    logerr( "jsonRpcTransact (%s): Received no data for request.", uri );
    retval = -1;
  }

/*------------------------------------------------------------------------*\
    Transcript data
\*------------------------------------------------------------------------*/
  else if( !jResult ){
    logerr( "jsonRpcTransact (%s): Need target for data." );
    retval = -1;
  }
  else {
    json_error_t  error;
    *jResult = json_loads( receivedData, 0, &error );
    if( !*jResult ) {
      logerr( "jsonRpcTransact (%s): corrupt line %d: %s",
               uri, error.line, error.text );
      retval = -1;
    }
    else if( !json_is_object(*jResult) ) {
      logerr( "jsonRpcTransact (%s): could not parse to object: %s",
               uri, receivedData );
      json_decref( *jResult );
      *jResult = NULL;
      retval = -1;
    }
  }

/*------------------------------------------------------------------------*\
    Be verbose
\*------------------------------------------------------------------------*/
#ifdef ICKDEBUG
  if( *jResult ) {
    char *txt = json_dumps( *jResult, JSON_PRESERVE_ORDER | JSON_COMPACT | JSON_ENSURE_ASCII );
    DBGMSG( "jsonRpcTransact (%s): %s id=%ld, result=\"%s\".", uri, method, id, txt );
    Sfree( txt );
  }
#endif

/*------------------------------------------------------------------------*\
    Clean up and exit
\*------------------------------------------------------------------------*/
end:
  if( jCmd )
    json_decref( jCmd );
  if( curlHandle )
    curl_easy_cleanup( curlHandle );
  if( headers )
    curl_slist_free_all( headers );
  Sfree( cmdStr );
  Sfree( receivedData );
  return retval;
}


/*=========================================================================*\
      cURL write callback
\*=========================================================================*/
static size_t _curlWriteCallback( void *buffer, size_t size, size_t nmemb, void *userp )
{
  size             *= nmemb;       // get real size in bytes
  size_t     retVal = size;        // Ok
  size_t     errVal = size+1;      // Signal error by size mismatch
  char     **target = userp;

  DBGMSG( "jsonRpcTransact: receiving %ld bytes", (long)size );
  //DBGMEM( "Received data", buffer, size );

/*------------------------------------------------------------------------*\
    Received intermediate zero bytes?
\*------------------------------------------------------------------------*/
  char *idx = memchr( buffer, 0, size );
  if( idx && idx-(char*)buffer!=size ) {
    logerr( "jsonRpcTransact: Received zero byte." );
    return errVal;
  }

/*------------------------------------------------------------------------*\
    Alloc or realloc target (this is a 0-terminated string...)
\*------------------------------------------------------------------------*/
  if( !*target )
    *target = strndup( buffer, size );
  else {
    *target = realloc( *target, strlen(*target)+size+1 );
    if( *target )
      strncat( *target, buffer, size );
  }
  if( !*target ) {
    logerr( "jsonRpcTransact: Out of memory" );
    return errVal;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  //DBGMEM( "Accumulated data", buffer, size );
  return retVal;
}



/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/


