/*$*********************************************************************\

Name            : -

Source File     : ickService.c

Description     : manage ickstream services 

Comments        : -

Called by       : ickstream wrapper 

Calls           : 

Error Messages  : -
  
Date            : 01.03.2013

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

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <jansson.h>

#include "utils.h"
#include "ickService.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private definitions and symbols
\*=========================================================================*/

// A service list item
typedef struct _serviceListItem {
  struct _serviceListItem *next;
  json_t                  *jItem;
  const char              *id;               // weak
  const char              *name;             // weak
  const char              *type;             // weak
  const char              *url;              // weak, optional
  const char              *serviceUrl;       // weak, optional
} ServiceListItem;
ServiceListItem *serviceList;
pthread_mutex_t  serviceListMutex = PTHREAD_MUTEX_INITIALIZER;


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static ServiceListItem *_getService( const char *id, const char *type );
static void _removeService( ServiceListItem *item ); 


/*=========================================================================*\
        Add a new service
          jService is the result part of a getServiceInformation answer
\*=========================================================================*/
int ickServiceAdd( json_t *jService )
{
  ServiceListItem *item;
  json_t          *jObj;
  
/*------------------------------------------------------------------------*\
    Allocate header
\*------------------------------------------------------------------------*/
  item = calloc( 1, sizeof(ServiceListItem) );
  if( !item ) {
    logerr( "ickServiceAdd: out of memeory!" );
    return -1;
  }
  item->jItem = json_incref( jService );

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
    logerr( "ickServiceAdd: Missing field \"name\"!" );
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
    logerr( "ickServiceAdd: Missing field \"type\"!" );
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
  ServiceListItem *oldItem = _getService( item->id, item->type );
  if( oldItem ) {
     logwarn( "ickServiceAdd: Replacing service (%s:%s).", 
                          item->type, item->id );
  	_removeService( oldItem );
  }
  
  // Insert new item in list
  item->next = serviceList;
  serviceList = item;
  pthread_mutex_unlock( &serviceListMutex );
  
/*------------------------------------------------------------------------*\
    Be verbose
\*------------------------------------------------------------------------*/
  DBGMSG( "Added service \"%s\" of type %s (%s)", 
                     item->name, item->type, item->id );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
        Remove a service by Id and type
           Type might be NULL, which removes all entries with the id.
\*=========================================================================*/
void ickServiceRemove( const char *id, const char *type )
{
  ServiceListItem *item; 	
  DBGMSG( "ickServiceRemove: \"%s\"", id );
  
/*------------------------------------------------------------------------*\
    Lock list and delete all matching entry 
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &serviceListMutex );
  while( (item=_getService(id,type)) != NULL )
    _removeService( item );
  
/*------------------------------------------------------------------------*\
    Unlock list 
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &serviceListMutex );	    
}


/*=========================================================================*\
        Dereference an item URI using the service hints
\*=========================================================================*/
char *ickServiceResolveURI( const char* uri, const char* type )
{
  ServiceListItem *service;
  char            *serviceId;
  char            *urlStub;
  
  DBGMSG( "ickServiceResolveURI for %s: \"%s\"", type, uri );
  
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
  service = _getService( serviceId, type );

/*------------------------------------------------------------------------*\
    Build result
\*------------------------------------------------------------------------*/
  char *retval = NULL;
  if( service  && service->serviceUrl ) {
    retval = malloc( strlen(service->serviceUrl) + +strlen(urlStub) + 2 );
    strcpy( retval, service->serviceUrl );
    strcat( retval, "/" );
    strcat( retval, urlStub );
  }
  pthread_mutex_unlock( &serviceListMutex );
     
/*------------------------------------------------------------------------*\
    That's all: clean up
\*------------------------------------------------------------------------*/
  DBGMSG( "ickServiceResolveURI result: \"%s\"", retval );
  Sfree( serviceId );
  return retval;
}


/*=========================================================================*\
        Get service by ID and type
          Does not lock the list, so caller needs to set mutex!
\*=========================================================================*/
static ServiceListItem *_getService( const char *id, const char *type )
{
  ServiceListItem *item;

/*------------------------------------------------------------------------*\
    Loop over all elements
\*------------------------------------------------------------------------*/
  for( item=serviceList; item; item=item->next ) {
    
    // Check id an type (if requested)
  	if( strcmp(item->id,id) )
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


