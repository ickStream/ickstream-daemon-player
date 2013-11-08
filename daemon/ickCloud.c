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
#include "persist.h"
#include "player.h"
#include "ickMessage.h"
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

static char *_cloudCoreUrl;
static char *_accessToken;


/*=========================================================================*\
    Private prototypes
\*=========================================================================*/
static void _registerDeviceCb( const char *method, json_t *jParams, json_t *jResult, int rc, int httpCode, void *userData );

static size_t  _curlWriteCallback( void *buffer, size_t size, size_t nmemb, void *userp );
static void   *_cloudRequestThread( void *arg );


/*=========================================================================*\
      Init cloud module
\*=========================================================================*/
int ickCloudInit( void )
{
  const char *val;
  DBGMSG( "Initializing cloud module..." );

/*------------------------------------------------------------------------*\
    Get core URL
\*------------------------------------------------------------------------*/
  val = persistGetString( "IckCloudCoreUrl" );
  if( !val )
    val = IckCloudCoreURI;
  if( ickCloudSetCoreUrl(val) )
    return -1;

/*------------------------------------------------------------------------*\
    Get access Token
\*------------------------------------------------------------------------*/
   val = persistGetString( "IckAccessToken" );
   if( val )
     _accessToken = strdup( val );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Shut down cloud module
\*=========================================================================*/
void ickCloudShutdown( void )
{
  DBGMSG( "Shutting down cloud module..." );

}


/*=========================================================================*\
    Set cloud core URL to use
\*=========================================================================*/
int ickCloudSetCoreUrl( const char *url )
{
  DBGMSG( "ickCloudSetCoreUrl: %s", url );

/*------------------------------------------------------------------------*\
    Free old value (if any)
\*------------------------------------------------------------------------*/
  Sfree( _cloudCoreUrl );

/*------------------------------------------------------------------------*\
    Set new value (if any)
\*------------------------------------------------------------------------*/
  _cloudCoreUrl = strdup( url );
  if( !_cloudCoreUrl ) {
    logerr( "ickCloudSetCoreUrl: Out of memory." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Persist value
\*------------------------------------------------------------------------*/
  if( persistSetString("IckCloudCoreUrl",url) )
    return -1;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Get cloud core URL
\*=========================================================================*/
const char *ickCloudGetCoreUrl( void )
{
  DBGMSG( "ickCloudGetCoreUrl: \"%s\"", _cloudCoreUrl );
  return _cloudCoreUrl;
}


/*=========================================================================*\
    Set access token
\*=========================================================================*/
int ickCloudSetAccessToken( const char *token )
{
  DBGMSG( "ickCloudSetAccessToken: %s", token?token:"(nil)" );

/*------------------------------------------------------------------------*\
    Free old value (if any)
\*------------------------------------------------------------------------*/
  Sfree( _accessToken );

/*------------------------------------------------------------------------*\
    Set new value (if any)
\*------------------------------------------------------------------------*/
  if( token ) {
    _accessToken = strdup( token );
    if( !_accessToken ) {
      logerr( "ickCloudSetAccessToken: Out of memory." );
      return -1;
    }
  }

/*------------------------------------------------------------------------*\
    Persist value
\*------------------------------------------------------------------------*/
  if( persistSetString("IckAccessToken",token) )
    return -1;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Get access token
\*=========================================================================*/
const char *ickCloudGetAccessToken( void )
{
  DBGMSG( "ickCloudGetAccessToken: \"%s\"", _accessToken?_accessToken:"(nil)" );
  return _accessToken;
}


/*=========================================================================*\
    Init registration for this device with the cloud.
      token - temporary registration token as received from the controller
              if NULL, the local regsitration state is set to unregistered
    returns 0 on success
\*=========================================================================*/
int ickCloudRegisterDevice( const char *token )
{
  json_t     *jParams;
  int         rc;
  const char *deviceAddress;
  const char *hwid;

  DBGMSG( "ickCloudRegisterDevice: %s", token?token:"(nil)" );

/*------------------------------------------------------------------------*\
    Reset old access token
\*------------------------------------------------------------------------*/
  if( ickCloudSetAccessToken(NULL) )
    return -1;

/*------------------------------------------------------------------------*\
    Prepare cloud request parameters
\*------------------------------------------------------------------------*/
  jParams = json_object();

  // Device IP (optional)
  deviceAddress = playerGetIpAddress();
  if( deviceAddress )
    json_object_set( jParams, "address", json_string(deviceAddress) );

  // Hardware Id
  hwid = playerGetHWID();
  if( hwid )
    json_object_set_new( jParams, "hardwareId", json_string(hwid) );

  // Application ID
  json_object_set_new( jParams, "applicationId", json_string(ICKPD_APPID) );

/*------------------------------------------------------------------------*\
    Fire off request, set callback
\*------------------------------------------------------------------------*/
  rc = ickCloudRequestAsync( NULL, token, "addDevice", jParams, _registerDeviceCb, NULL );
  if( rc )
    logerr( "ickCloudRegisterDevice: Could not register device (%d).", rc );

/*------------------------------------------------------------------------*\
    Clean up, that's it
\*------------------------------------------------------------------------*/
  json_decref( jParams );
  return 0;
}


static void _registerDeviceCb( const char *method, json_t *jParams, json_t *jResult, int rc, int httpCode, void *userData )
{
  json_t *jObj;

/*------------------------------------------------------------------------*\
    Error?
\*------------------------------------------------------------------------*/
  if( rc || !jResult ) {
    loginfo( "_registerDeviceCb: cloud transaction unsuccessful" );
    ickMessageNotifyPlayerState( NULL );
    return;
  }

/*------------------------------------------------------------------------*\
    Server indicated error?
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jResult, "error" );
  if( jObj ) {
    loginfo( "_registerDeviceCb: Error %s.", json_rpcerrstr(jObj) );
    ickMessageNotifyPlayerState( NULL );
    return;
  }

/*------------------------------------------------------------------------*\
    Interpret result
\*------------------------------------------------------------------------*/
  jResult = json_object_get( jResult, "result" );
  if( !jResult ) {
    logerr( "_registerDeviceCb: No \"result\" object in answer." );
    ickMessageNotifyPlayerState( NULL );
    return;
  }

/*------------------------------------------------------------------------*\
    Get new access token
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jResult, "accessToken" );
  if( !jObj || !json_is_string(jObj) )  {
    logerr( "_registerDeviceCb: missing field \"accessToken\"." );
    ickMessageNotifyPlayerState( NULL );
    return;
  }
  if( ickCloudSetAccessToken(json_string_value(jObj)) ) {
    logerr( "_registerDeviceCb: could not set \"accessToken\"." );
    ickMessageNotifyPlayerState( NULL );
    return;
  }

/*------------------------------------------------------------------------*\
    Get new player name
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jResult, "name" );
  if( jObj && json_is_string(jObj) )
    playerSetName( json_string_value(jObj), false );
  else if( jObj )
    logerr( "_registerDeviceCb: field \"name\" is not a string" );

  /*
  "id": < The unique identity for this device which should be used in any further communication >,
  "accessToken": < The device access token which should be used in any further communication >,
  "name": < The new user friendly name of the device >,
  "model": < Model identification for this device >,
  "address": < Optional,current local IP-address of this device >,
  "publicAddress": < current public IP-address of this device, only available if address is specified >
  */

/*------------------------------------------------------------------------*\
    Send notification about new device state, that's all
\*------------------------------------------------------------------------*/
  ickMessageNotifyPlayerState( NULL );
}


/*=========================================================================*\
    Set current device IP address
\*=========================================================================*/
int ickCloudSetDeviceAddress( void )
{
  const char *deviceId;
  const char *deviceAddress;
  json_t     *jParams, *jResult;
  int         httpCode;

/*------------------------------------------------------------------------*\
    Need token...
\*------------------------------------------------------------------------*/
  if( !_accessToken ) {
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
  jResult = ickCloudRequestSync( NULL, _accessToken, "setDeviceAddress", jParams, &httpCode );
  json_decref( jParams );

/*------------------------------------------------------------------------*\
    Not authorized
\*------------------------------------------------------------------------*/
  if( httpCode==401 ) {
    loginfo( "ickCloudSetDeviceAddress: not authorized" );
    ickCloudSetAccessToken( NULL );
    ickMessageNotifyPlayerState( NULL );
    return 0;
  }

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
json_t *ickCloudRequestSync( const char *uri, const char *oAuthToken, const char *method, json_t *jParams, int *httpCode )
{
  json_t *jResult;
  long    id;
  int     rc;

  // Get ID
  id = getAndIncrementCounter();

  // Do transaction
  rc = jsonRpcTransact( uri, oAuthToken, id, method, jParams, &jResult, httpCode );
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
  return jsonRpcTransact( uri, oAuthToken, 0, method, jParams, NULL, NULL );
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
  int           httpCode;

  DBGMSG( "Cloud Request thread (%p,%s): starting.", request, request->method );
  PTHREADSETNAME( "cloudReq" );

/*------------------------------------------------------------------------*\
    Do the transaction
\*------------------------------------------------------------------------*/
  rc = jsonRpcTransact( request->uri, request->oAuthToken, request->id,
                        request->method, request->jParams, &jResult, &httpCode );
  DBGMSG( "Cloud Request thread (%p,%s): Performed request (%d).",
          request, request->method, rc );

/*------------------------------------------------------------------------*\
    Call callback (if any)
\*------------------------------------------------------------------------*/
  if( request->callback ) {
    DBGMSG( "Cloud Request thread (%p,%s): Calling call back function.",
            request, request->method, rc );
    request->callback( request->method, request->jParams, jResult, rc, httpCode, request->userData );
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
      uri      - server HTTP interface address,
                 if NULL the default ickstream endpoint is used
      oAuth    - oAuth header token (NULL for none)
      method   - method name
      id       - JSON-RPC id (0 for notifications)
      jParams  - parameters (NULL for none)
      jResult  - pointer to result (can be NULL for notifications)
      httpCode - pointer to http status code (might be NULL)
    returns -1 on error
\*=========================================================================*/
int jsonRpcTransact( const char *uri, const char *oAuthToken, int id,
                     const char *method, json_t *jParams, json_t **jResult, int *httpCode )
{
  json_t            *jCmd         = NULL;
  char              *cmdStr       = NULL;
  CURL              *curlHandle   = NULL;
  struct curl_slist *headers      = NULL;
  char              *receivedData = NULL;
  int                retval       = 0;
  int                rc;
  int                code         = 0;

/*------------------------------------------------------------------------*\
    Use core URI?
\*------------------------------------------------------------------------*/
  if( !uri )
    uri = ickCloudGetCoreUrl();

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

  // We are interested in the HTTP response header
  rc = curl_easy_setopt( curlHandle, CURLOPT_HEADER, 1 );
  if( rc ) {
    logerr( "jsonRpcTransact (%s): Unable to set header mode.", uri );
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
  if( !receivedData ) {
    logerr( "jsonRpcTransact (%s): No data.", uri );
    retval = -1;
    goto end;
  }

/*------------------------------------------------------------------------*\
    Check for last http code in header
\*------------------------------------------------------------------------*/
  long headerSize = 0;
  curl_easy_getinfo( curlHandle, CURLINFO_HEADER_SIZE, &headerSize );
  char *ptr = receivedData;
  for(;;) {
    char *ptr1 = strstr( ptr, "HTTP/1.1 " );
    if( !ptr1 || ptr1>=receivedData+headerSize )
      break;
    ptr = ptr1 + strlen( "HTTP/1.1 " );
  }
  if( ptr==receivedData ) {
    logerr( "jsonRpcTransact (%s): bad HTML header \"%.20s...\"", uri, receivedData );
    retval = -1;
    goto end;
  }
  code = atoi( ptr );
  DBGMSG( "jsonRpcTransact (%s): HTTP code is %d", uri, code );
  DBGMSG( "jsonRpcTransact (%s): HTTP content is \"%.20s\"...", uri, receivedData+headerSize );
  if( code!=200 )
    goto end;

/*------------------------------------------------------------------------*\
    Notification: expect no data
\*------------------------------------------------------------------------*/
  if( !id ) {
    if( receivedData[headerSize] )
      logwarn( "jsonRpcTransact (%s): Received data for notification (%s).",
               uri, receivedData );
  }

/*------------------------------------------------------------------------*\
    Request: expect data
\*------------------------------------------------------------------------*/
  else if( !receivedData[headerSize] ) {
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
    *jResult = json_loads( receivedData+headerSize, 0, &error );
    if( !*jResult ) {
      logerr( "jsonRpcTransact (%s): corrupt line %d: %s",
               uri, error.line, error.text );
      retval = -1;
    }
    else if( !json_is_object(*jResult) ) {
      logerr( "jsonRpcTransact (%s): could not parse to object: %s",
               uri, receivedData+headerSize );
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

  if( httpCode )
    *httpCode = code;
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


