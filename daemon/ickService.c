/*$*********************************************************************\

Name            : -

Source File     : ickService.c

Description     : manage ickstream services 

Comments        : -

Called by       : ickstream wrapper 

Calls           : 

Error Messages  : -
  
Date            : 01.03.2013

Updates         : 14.04.2013 added support for cloud service     //MAF
                  
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
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <jansson.h>

#include "ickutils.h"
#include "player.h"
#include "ickCloud.h"
#include "ickService.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/

// A service list item
struct _serviceListItem {
  struct _serviceListItem *next;
  ServiceOrigin            origin;
  json_t                  *jItem;
  const char              *id;               // weak
  const char              *name;             // weak
  const char              *type;             // weak
  const char              *url;              // weak, optional
  const char              *serviceUrl;       // weak, optional
};

static ServiceListItem *serviceList;
static pthread_mutex_t  serviceListMutex = PTHREAD_MUTEX_INITIALIZER;


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static ServiceListItem *_getService( ServiceListItem *item, const char *id, const char *type, ServiceOrigin origin );
static void _removeService( ServiceListItem *item ); 


/*=========================================================================*\
    Add a new service
      jService is the result part of a getServiceInformation answer
\*=========================================================================*/
int ickServiceAdd( json_t *jService, ServiceOrigin origin )
{
  ServiceListItem *item;
  json_t          *jObj;

/*------------------------------------------------------------------------*\
    Allocate header
\*------------------------------------------------------------------------*/
  item = calloc( 1, sizeof(ServiceListItem) );
  if( !item ) {
    logerr( "ickServiceAdd: out of memory!" );
    return -1;
  }
  item->origin = origin;
  item->jItem  = json_incref( jService );

/*------------------------------------------------------------------------*\
    Extract id for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jService, "id" );
  if( !jObj || !json_is_string(jObj) ) {
    logerr( "ickServiceAdd: Missing field \"id\"!" );
    Sfree( item );
    json_decref( jService );
    return -1; 
  }
  item->id = json_string_value( jObj );

/*------------------------------------------------------------------------*\
    Extract name for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jService, "name" );
  if( !jObj || !json_is_string(jObj) ) {
    logerr( "ickServiceAdd (%s): Missing field \"name\"!", item->id );
    Sfree( item );
    json_decref( jService );
    return -1; 
  }
  item->name = json_string_value( jObj );   

/*------------------------------------------------------------------------*\
    Extract type for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jService, "type" );
  if( !jObj || !json_is_string(jObj) ) {
    logerr( "ickServiceAdd (%s): Missing field \"type\"!", item->id );
    Sfree( item );
    json_decref( jService );
    return -1; 
  }
  item->type = json_string_value( jObj );

/*------------------------------------------------------------------------*\
    Extract url for quick access (optional, weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jService, "url" );
  if( jObj ) 
    item->url = json_string_value( jObj );   

/*------------------------------------------------------------------------*\
    Extract service url for quick access (optional, weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jService, "serviceUrl" );
  if( jObj ) 
    item->serviceUrl = json_string_value( jObj );   

/*------------------------------------------------------------------------*\
    Insert or replace item
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &serviceListMutex );

  // Check for duplicates
  ServiceListItem *oldItem = _getService( NULL, item->id, item->type, origin );
  if( oldItem ) {
     DBGMSG( "ickServiceAdd (%s): Replacing service (%s:%s).",
                            item->id, item->type, item->name );
    _removeService( oldItem );
  }

  // Insert new item in list
  item->next = serviceList;
  serviceList = item;
  pthread_mutex_unlock( &serviceListMutex );

/*------------------------------------------------------------------------*\
    Be verbose
\*------------------------------------------------------------------------*/
  DBGMSG( "ickServiceAdd (%s): Added (%s:%s), origin %d.",
                     item->id, item->type, item->name, item->origin );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Get services from cloud
      type  - only consider services of this type (might be NULL)
      reset - first delete all cloud services (of type if given)
      return -1 on error
\*=========================================================================*/
int ickServiceAddFromCloud( const char *type, bool reset )
{
  const char      *token;
  ServiceListItem *service;
  json_t          *jParams;
  json_t          *jResult;
  json_t          *jObj;
  int              i;

  DBGMSG( "ickServiceAddFromCloud: type=\"%s\" reset=%s",
           type?type:"(no type)", reset?"On":"Off" );

/*------------------------------------------------------------------------*\
    Need token for cloud access...
\*------------------------------------------------------------------------*/
  token = ickCloudGetAccessToken();
  if( !token ) {
    logwarn( "ickServiceAddFromCloud: Device not registered (no access token)." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Collect parameters
\*------------------------------------------------------------------------*/
  jParams = json_object();
  if( type )
    json_object_set_new( jParams, "type", json_string(type) );

/*------------------------------------------------------------------------*\
    Interact with cloud
\*------------------------------------------------------------------------*/
  jResult = ickCloudRequestSync( NULL, token, "findServices", jParams, NULL );
  json_decref( jParams );
  if( !jResult ) {
    DBGMSG( "ickServiceAddFromCloud: No answer from cloud." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Server indicated error?
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jResult, "error" );
  if( jObj ) {
    DBGMSG( "ickServiceAddFromCloud: Error %s.", json_rpcerrstr(jObj) );
    json_decref( jResult );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Get result
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jResult, "result" );
  if( !jObj ) {
    DBGMSG( "ickServiceAddFromCloud: No \"result\" object in answer." );
    json_decref( jResult );
    return -1;
  }
  jObj = json_object_get( jObj, "items" );
  if( !jObj ) {
    DBGMSG( "ickServiceAddFromCloud: No \"items\" object in answer." );
    json_decref( jResult );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Remove existing services from cloud if requested
\*------------------------------------------------------------------------*/
  if( reset ) {
    pthread_mutex_lock( &serviceListMutex );
    while( (service=_getService(NULL,NULL,type,ServiceCloud)) != NULL )
      _removeService( service );
    pthread_mutex_unlock( &serviceListMutex );
  }

/*------------------------------------------------------------------------*\
    Loop over all items and add them to list
\*------------------------------------------------------------------------*/
  for( i=0; i<json_array_size(jObj); i++ ) {
    json_t  *jItem = json_array_get( jObj, i );
    if( ickServiceAdd(jItem,ServiceCloud) )
      logerr( "ickServiceAddFromCloud: Could not add service item #%d.", i );
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  json_decref( jResult );
  return 0;
}


/*=========================================================================*\
    Remove a service by Id and type
      type might be NULL, which removes all entries with the id.
\*=========================================================================*/
void ickServiceRemove( const char *id, const char *type, ServiceOrigin origin )
{
  ServiceListItem *item;
  DBGMSG( "ickServiceRemove: id=\"%s\" type=%s origin=%d.", id, type?type:"(no type)", origin );

/*------------------------------------------------------------------------*\
    Lock list and delete all matching entry 
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &serviceListMutex );
  while( (item=_getService(NULL,id,type,origin)) != NULL )
    _removeService( item );

/*------------------------------------------------------------------------*\
    Unlock list 
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &serviceListMutex );	    
}


/*=========================================================================*\
    Find (next) service for id, type and origin
      item is the last result, supply NULL for first call
      id and type might be NULL, origin might be 0 for joker
      Return (next) service item or NULL if no (next) match
\*=========================================================================*/
ServiceListItem *ickServiceFind( ServiceListItem *item, const char *id, const char *type, ServiceOrigin origin )
{

  pthread_mutex_lock( &serviceListMutex );
  item = _getService( item, id, type, origin );
  pthread_mutex_unlock( &serviceListMutex );

  return item;
}


/*=========================================================================*\
    Dereference an item URI using the service hints
      returns an allocated string (called needs to free that) or NULL on error
\*=========================================================================*/
char *ickServiceResolveURI( const char* uri, const char* type )
{
  ServiceListItem *service;
  char            *serviceId;
  char            *urlStub;

  DBGMSG( "ickServiceResolveURI: \"%s\" type=\"%s\".", uri, type?type:"(no type)" );

/*------------------------------------------------------------------------*\
    No service prefix ?
\*------------------------------------------------------------------------*/
  if( strncasecmp(uri,IckServiceSchemePrefix,strlen(IckServiceSchemePrefix)) )
    return strdup( uri );

/*------------------------------------------------------------------------*\
    Get service id
\*------------------------------------------------------------------------*/
  serviceId = strdup( uri+strlen(IckServiceSchemePrefix) );
  urlStub   = strchr( serviceId, '/' );
  if( urlStub )
    *(urlStub++) = 0; 

/*------------------------------------------------------------------------*\
    Look up service by id and (optionally) type
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &serviceListMutex );
  service = _getService( NULL, serviceId, type, 0 );

/*------------------------------------------------------------------------*\
    Build result
\*------------------------------------------------------------------------*/
  char *retval = NULL;
  if( service  && service->serviceUrl ) {
    retval = malloc( strlen(service->serviceUrl) + strlen(urlStub) + 2 );
    strcpy( retval, service->serviceUrl );
    strcat( retval, "/" );
    strcat( retval, urlStub );
  }
  pthread_mutex_unlock( &serviceListMutex );

/*------------------------------------------------------------------------*\
    That's all: clean up
\*------------------------------------------------------------------------*/
  DBGMSG( "ickServiceResolveURI (%s): \"%s\".", uri, retval );
  Sfree( serviceId );
  return retval;
}


/*=========================================================================*\
    Get streaming reference for an item from cloud
      item - the item to be resolved
      returns a json list containing one element
              (equivalent to streamingRefs feature of items)
      returns NULL on error
\*=========================================================================*/
json_t *ickServiceGetStreamingRef( PlaylistItem *item )
{
  const char      *token;
  char            *sid;
  ServiceListItem *service;
  json_t          *jParams;
  json_t          *jResult;
  json_t          *jObj;
  json_t          *jStreamingRefs;

  DBGMSG( "ickServiceGetStreamingRef: Item \"%s\" (%s)",
      playlistItemGetText(item), playlistItemGetId(item) );

/*------------------------------------------------------------------------*\
    Need token for cloud access...
\*------------------------------------------------------------------------*/
  token = ickCloudGetAccessToken();
  if( !token ) {
    logwarn( "ickServiceGetStreamingRef: Device not registered (no access token)." );
    return NULL;
  }

/*------------------------------------------------------------------------*\
  Get service id from item id
\*------------------------------------------------------------------------*/
  if( !playlistItemGetId(item) ) {
    playlistItemLock( item );
    logwarn( "ickServiceGetStreamingRef (%s): Item contains no id!",
             playlistItemGetText(item) );
    playlistItemUnlock( item );
    return NULL;
  }
  sid = strdup( playlistItemGetId(item) );
  if( !sid ) {
    logwarn( "ickServiceGetStreamingRef: out of memory!" );
    return NULL;
  }
  if( !strchr(sid,':') ) {
    playlistItemLock( item );
    logwarn( "ickServiceGetStreamingRef (%s,%s): Malformed id (missing ':')!",
            playlistItemGetText(item), playlistItemGetId(item) );
    playlistItemUnlock( item );
    Sfree ( sid );
    return NULL;
  }
  *strchr( sid, ':' ) = 0;

/*------------------------------------------------------------------------*\
  Find service descriptor
\*------------------------------------------------------------------------*/
  service = ickServiceFind( NULL, sid, NULL, ServiceCloud );
  if( !service ) {
    playlistItemLock( item );
    logwarn( "ickServiceGetStreamingRef (%s,%s): No such service \"%s\"!",
            playlistItemGetText(item), playlistItemGetId(item), sid );
    playlistItemUnlock( item );
    Sfree ( sid );
    return NULL;
  }
  Sfree ( sid );
  DBGMSG( "ickServiceGetStreamingRef (%s,%s): using service \"%s\" (%s)",
          playlistItemGetText(item), playlistItemGetId(item),
          service->name, service->type );

/*------------------------------------------------------------------------*\
    Collect parameters
\*------------------------------------------------------------------------*/
  jParams = json_object();
  json_object_set_new( jParams, "itemId", json_string(playlistItemGetId(item)) );
  //Fixme: collect supported formats

/*------------------------------------------------------------------------*\
    Interact with cloud
\*------------------------------------------------------------------------*/
  jResult = ickCloudRequestSync( service->url, token, "getItemStreamingRef", jParams, NULL );
  json_decref( jParams );
  if( !jResult ) {
    logwarn( "ickServiceGetStreamingRef: No answer from cloud." );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Server indicated error?
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jResult, "error" );
  if( jObj ) {
    logwarn( "ickServiceGetStreamingRef: Error %s.", json_rpcerrstr(jObj) );
    json_decref( jResult );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Get result
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jResult, "result" );
  if( !jObj ) {
    logerr( "ickServiceGetStreamingRef: No \"result\" object in answer." );
    json_decref( jResult );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Convert to list to be compatible with streamingRefs feature of items
\*------------------------------------------------------------------------*/
  jStreamingRefs = json_array();
  if( !jObj ) {
    logerr( "ickServiceGetStreamingRef: out of memory!" );
    json_decref( jResult );
    return NULL;
  }
  if( json_array_append(jStreamingRefs,jObj) ) {
    logerr( "ickServiceGetStreamingRef: json_arry_append() failed." );
    json_decref( jResult );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  json_decref( jResult );
  return jStreamingRefs;
}


/*=========================================================================*\
    Get JSON object defining a service
\*=========================================================================*/
json_t *ickServiceGetJSON( const ServiceListItem *item )
{
  return item->jItem;
}


/*=========================================================================*\
    Get service id (convenience function)
\*=========================================================================*/
const char *ickServiceGetId( const ServiceListItem *item )
{
  return item->id;
}


/*=========================================================================*\
    Get service name (convenience function)
\*=========================================================================*/
const char *ickServiceGetName( const ServiceListItem *item )
{
  return item->name;
}


/*=========================================================================*\
    Get service type (convenience function)
\*=========================================================================*/
const char *ickServiceGetType( const ServiceListItem *item )
{
  return item->type;
}


/*=========================================================================*\
    Get service url (convenience function)
\*=========================================================================*/
const char *ickServiceGetURI( const ServiceListItem *item )
{
  return item->url;
}


/*=========================================================================*\
    Get service url (convenience function)
\*=========================================================================*/
const char *ickServiceGetServiceURI( const ServiceListItem *item )
{
  return item->serviceUrl;
}


/*=========================================================================*\
    Get service by ID, type and origin
      item is the last result, supply NULL for first call
      id or type might be NULL, origin might be 0,
         in which case all entries match that criteria
      Does not lock the list, so caller needs to set mutex!
\*=========================================================================*/
static ServiceListItem *_getService( ServiceListItem *item, const char *id, const char *type, ServiceOrigin origin )
{

/*------------------------------------------------------------------------*\
    Where to start?
\*------------------------------------------------------------------------*/
  item = item ? item->next : serviceList;

/*------------------------------------------------------------------------*\
    Loop over all elements
\*------------------------------------------------------------------------*/
  for( ; item; item=item->next ) {

    // Check all criteria that are defined
    if( origin && !(item->origin&origin) )
      continue;
    if( id && strcmp(item->id,id) )
      continue;
    if( type && strcmp(item->type,type) )
      continue;

    // match !
    break;
  }

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return item;
}


/*=========================================================================*\
    Remove and free a service from list
      Does not lock the list, so caller needs to set mutex!
\*=========================================================================*/
static void _removeService( ServiceListItem *item ) 
{
  DBGMSG( "_removeService (%s): (%s:%s).", item->id, item->type, item->name );

/*------------------------------------------------------------------------*\
    Search for entry 
\*------------------------------------------------------------------------*/
  ServiceListItem *prevElement = NULL;
  ServiceListItem *element     = serviceList;
  while( element ) {
    if( element==item )
      break;
    prevElement = element;
    element = element->next;
  }

/*------------------------------------------------------------------------*\
    Not found 
\*------------------------------------------------------------------------*/
  if( !element )
    return;

/*------------------------------------------------------------------------*\
    Unlink element 
\*------------------------------------------------------------------------*/
  if( !prevElement )    // replace list root
    serviceList = element->next;
  else
    prevElement->next = element->next;

/*------------------------------------------------------------------------*\
    Free resources 
\*------------------------------------------------------------------------*/
  json_decref( element->jItem );
  Sfree( element );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
