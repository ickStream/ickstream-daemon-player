/*$*********************************************************************\

Name            : -

Source File     : playlist.c

Description     : handle playlist (used as player queue) 

Comments        : -

Called by       : player, ickstream wrapper 

Calls           : 

Error Messages  : -
  
Date            : 22.02.2013

Updates         : 14.03.2013 protext list modifications by mutex //MAF
                  
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
 * this SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS for A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE for ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF this SOFTWARE, 
 * EVEN if ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\************************************************************************/

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <jansson.h>
#include <ickDiscovery.h>

#include "utils.h"
#include "playlist.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
	Private definitions and symbols
\*=========================================================================*/
// none

/*=========================================================================*\
        Private prototypes
\*=========================================================================*/
static int _playlistGetCursorPos( Playlist *plst );


/*=========================================================================*\
       Allocate and init new playlist 
\*=========================================================================*/
Playlist *playlistNew( void )
{
  Playlist *plst = calloc( 1, sizeof(Playlist) );
  if( !plst )
    srvmsg( LOG_ERR, "playlistNew: out of memeory!" );
    
/*------------------------------------------------------------------------*\
    Init header fields
\*------------------------------------------------------------------------*/
  // plst->id   = strdup( "<undefined>" );
  // plst->name = strdup( "ickpd player queue" );
 
/*------------------------------------------------------------------------*\
    init mutex and conditions 
\*------------------------------------------------------------------------*/
  pthread_mutex_init( &plst->mutex, NULL );
 
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistNew: %p", plst );
  return plst; 
}


/*=========================================================================*\
       Get playlist from JSON buffer
         if jQueue is NULL, a new playlist is created
\*=========================================================================*/
Playlist *playlistFromJSON( json_t *jQueue )
{
  Playlist *plst;
  json_t   *jObj;
  int       i;
    
/*------------------------------------------------------------------------*\
    Create header
\*------------------------------------------------------------------------*/
  plst = playlistNew();   
      
/*------------------------------------------------------------------------*\
    No json date?
\*------------------------------------------------------------------------*/
  if( !jQueue )
    return plst;
    
/*------------------------------------------------------------------------*\
    Lazy init: Create playlist header from JSON 
\*------------------------------------------------------------------------*/

  // ID (optional)
  jObj = json_object_get( jQueue, "playlistId" );
  if( jObj && json_is_string(jObj) )
    playlistSetId( plst, json_string_value(jObj) );

  // Name (optional)
  jObj = json_object_get( jQueue, "playlistName" );
  if( jObj && json_is_string(jObj) )
    playlistSetName( plst, json_string_value(jObj) );
    
  // Last Modification
  jObj = json_object_get( jQueue, "lastChanged" );
  if( jObj && json_is_real(jObj) )
    plst->lastChange = json_real_value( jObj );
  else
  	srvmsg( LOG_ERR, "Playlist: missing field \"playlistName\"" );
    
  // Get list of items
  jObj = json_object_get( jQueue, "items" );
  if( jObj && json_is_array(jObj) ) {
  
    // Loop over all utems and add them to list	
  	for( i=0; i<json_array_size(jObj); i++ ) {
      json_t       *jItem = json_array_get( jObj, i );
      PlaylistItem *pItem = playlistItemFromJSON( jItem );
      if( pItem )
        playlistAddItemAfter( plst, NULL, pItem );
      else
        srvmsg( LOG_ERR, "Playlist: could not parse item #%d", i+1 );
    } 
  }
  else
    srvmsg( LOG_ERR, "Playlist: missing field \"items\"" );

/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
  return plst;
}


/*=========================================================================*\
       Delete and free playlist 
\*=========================================================================*/
void playlistDelete( Playlist *plst )
{
  PlaylistItem *item, *next;

  DBGMSG( "playlistDelete: %p", plst );
  
/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Free all items (unlinking not necessary)
\*------------------------------------------------------------------------*/
  for( item=plst->firstItem; item; item=next ) {
    next = item->next;
    playlistItemDelete( item );
  }
  
/*------------------------------------------------------------------------*\
    Free all header features
\*------------------------------------------------------------------------*/
  Sfree( plst->id );
  Sfree( plst->name );

/*------------------------------------------------------------------------*\
    Unlock and free header
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  Sfree( plst );
}


/*=========================================================================*\
       Set ID for playlist 
\*=========================================================================*/
void playlistSetId( Playlist *plst, const char *id )
{
  Sfree( plst->id );
  plst->id = strdup( id );    
  DBGMSG( "playlistSetID %p: %s", plst, id?id:"NULL" ); 
}


/*=========================================================================*\
       Set Name for playlist 
\*=========================================================================*/
void playlistSetName( Playlist *plst, const char *name )
{
  Sfree( plst->name );
  plst->name = strdup( name );    
  DBGMSG( "playlistSetName %p: %s", plst, name?name:"NULL" ); 
}


/*=========================================================================*\
       Get ID for playlist 
\*=========================================================================*/
const char *playlistGetId( Playlist *plst )
{
  DBGMSG( "playlistGetID %p: %s", plst, plst->id?plst->id:"NULL" ); 
  return plst->id;
}


/*=========================================================================*\
       Get Name for playlist 
\*=========================================================================*/
const char *playlistGetName( Playlist *plst )
{
  DBGMSG( "playlistGetName %p: %s", plst, plst->name?plst->name:"NULL" ); 
  return plst->name;
}


/*=========================================================================*\
       Get length of playlist 
\*=========================================================================*/
int playlistGetLength( Playlist *plst )
{
  DBGMSG( "playlistGetLength %p: %d", plst, plst->_numberOfItems ); 
  return plst->_numberOfItems;    
}


/*=========================================================================*\
       Get timestamp of last change 
\*=========================================================================*/
double playlistGetLastChange( Playlist *plst )
{
  DBGMSG( "playlistGetLastChange %p: %lf", plst, plst->lastChange ); 
  return plst->lastChange;        
}


/*=========================================================================*\
       Get cursor position (aka current track) 
         counting starts with 0 (which is also the default)
         return 0 on empty list
\*=========================================================================*/
int playlistGetCursorPos( Playlist *plst )
{

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );
    
/*------------------------------------------------------------------------*\
    Find position of current item if index is invalid
\*------------------------------------------------------------------------*/
  _playlistGetCursorPos( plst );

/*------------------------------------------------------------------------*\
    Unlock mutex and return result
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  return plst->_cursorPos;    
}


/*=========================================================================*\
       Get cursor position (aka current track), internal version without locking 
         counting starts with 0 (which is also the default)
         return 0 on empty list
\*=========================================================================*/
static int _playlistGetCursorPos( Playlist *plst )
{

/*------------------------------------------------------------------------*\
    Find position of current item if index is invalid
\*------------------------------------------------------------------------*/
  if( plst->_cursorPos<0 ) {
    int pos = 0;
    PlaylistItem *item = plst->firstItem;
    while( item && item!=plst->_cursorItem && plst->_cursorItem ) {
      item = item->next;
      pos++;
    }
    plst->_cursorPos = pos;
  }

/*------------------------------------------------------------------------*\
    Return position
\*------------------------------------------------------------------------*/
  DBGMSG( "_playlistGetCursorPos %p: %d", plst, plst->_cursorPos );
  return plst->_cursorPos;
}


/*=========================================================================*\
       Get cursor item (aka current track) 
         default is the first item
         returns NULL on empty list
\*=========================================================================*/
PlaylistItem *playlistGetCursorItem( Playlist *plst )
{
    
/*------------------------------------------------------------------------*\
    Set first item as default
\*------------------------------------------------------------------------*/
  if( !plst->_cursorItem )
    plst->_cursorItem = plst->firstItem;
      
/*------------------------------------------------------------------------*\
    Return item at cursor
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistGetCursorItem %p: %p", plst, plst->_cursorItem );       
  return plst->_cursorItem;
}


/*=========================================================================*\
       Set cursor position (aka current track)
         count starts with 0 
         return actual position (clipped) or -1 (empty list)
\*=========================================================================*/
int playlistSetCursorPos( Playlist *plst, int pos )
{
  PlaylistItem *item;
  int i;

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );
     
/*------------------------------------------------------------------------*\
    Clipping
\*------------------------------------------------------------------------*/
  if( pos<0 )
    pos = 0;
  if( pos>=playlistGetLength(plst) )
    pos = plst->_numberOfItems-1;

/*------------------------------------------------------------------------*\
    Get item
\*------------------------------------------------------------------------*/
  item = plst->firstItem;
  i    = pos;
  while( i-- && item )
     item = item->next;
  if( item ) {
    plst->_cursorPos  = pos;
    plst->_cursorItem = item;
  }
  
  // error handling
  else
    pos = -1;

/*------------------------------------------------------------------------*\
   Unlock and return 
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  DBGMSG( "playlistSetCursor %p: %d", plst, pos );            
  return pos;
}


/*=========================================================================*\
       Set cursor position (aka current track) to next entry
         return new item (clipped) or NULL (empty list or end of list)
\*=========================================================================*/
PlaylistItem *playlistIncrCursorItem( Playlist *plst )
{
  PlaylistItem *item =  playlistGetCursorItem( plst );

  DBGMSG( "playlistIncrCursorPos %p: items %p->%p", plst, item, item?item->next:NULL );  

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    No successor?
\*------------------------------------------------------------------------*/
  if( !item || !item->next ) {
    pthread_mutex_unlock( &plst->mutex );
    return NULL;
  } 

/*------------------------------------------------------------------------*\
    Set successor
\*------------------------------------------------------------------------*/
  plst->_cursorItem = item->next;

/*------------------------------------------------------------------------*\
    Return position in list (increment only if valid index is stored)
\*------------------------------------------------------------------------*/
  if( plst->_cursorPos>=0 )
    plst->_cursorPos++;

/*------------------------------------------------------------------------*\
    Unlock mutex and return current item
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  return plst->_cursorItem;
}


/*=========================================================================*\
       Get playlist in ickstream JSON format for method "getPlaylist"
         offset is the offset of the first element to be included
         count is the (max.) number of elements included
\*=========================================================================*/
json_t *playlistGetJSON( Playlist *plst, int offset, int count )
{
  json_t       *jResult = json_array();
  PlaylistItem *pItem   = playlistGetItem( plst, offset );
  
  DBGMSG( "playlistGetJSON %p: offset:%d count:%d", plst, offset, count );  

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    A zero count argument means all
\*------------------------------------------------------------------------*/
  if( !count )
    count = playlistGetLength(plst);
      
/*------------------------------------------------------------------------*\
    Collect all requested items
\*------------------------------------------------------------------------*/
  while( pItem && count-- ) {
    json_array_append(jResult, pItem->jItem);
    // DBGMSG( "playlistGetJSON: JSON refCnt=%d", pItem->jItem->refcount );
    pItem = pItem->next;
  }  
  
/*------------------------------------------------------------------------*\
    Build header
\*------------------------------------------------------------------------*/
  jResult = json_pack( "{ss sf si si si so}",
                         "jsonrpc",      "2.0", 
                         "lastChanged",  plst->lastChange,
                         "count",        json_array_size(jResult),
                         "countAll",     playlistGetLength(plst),
                         "offset",       offset,
                         "items",        jResult );
  // Name and ID are optional                       
  if( plst->id )
    json_object_set_new( jResult, "playlistId", json_string(plst->id) );
  if( plst->name )
    json_object_set_new( jResult, "playlistName", json_string(plst->name) );
                                                    
/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  DBGMSG( "playlistGetJSON: result %p", jResult ); 
  return jResult;                       
}


/*=========================================================================*\
       Get item at a given position
         count starts with 0
         returns weak pointer to item or NULL (empty list, pos out of bounds)
\*=========================================================================*/
PlaylistItem *playlistGetItem( Playlist *plst, int pos )
{
  PlaylistItem *item = plst->firstItem;

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );
  
/*------------------------------------------------------------------------*\
    Check for lower bound
\*------------------------------------------------------------------------*/
  if( pos<0 )
    item = NULL;
    
/*------------------------------------------------------------------------*\
    Loop over list
\*------------------------------------------------------------------------*/
  while( pos-- && item )
     item = item->next;
     
/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  DBGMSG( "playlistGetItem: %p", item );         
  return item;         
}


/*=========================================================================*\
       Get first item with a given ID
         returns weak pointer to item or NULL if nor found
\*=========================================================================*/
PlaylistItem *playlistGetItemById( Playlist *plst, const char *id )
{
  PlaylistItem *item;

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Loop over list and check Id
\*------------------------------------------------------------------------*/
  for( item=plst->firstItem; item; item=item->next )
    if( !strcmp(item->id,id) )
      break;
  
/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  DBGMSG( "playlistGetItemById(%s): %p", id, item );         
  return item;         
}


/*=========================================================================*\
       Create (allocate and init) an item from ickstream JSON object
          return NULL on error
\*=========================================================================*/
PlaylistItem *playlistItemFromJSON( json_t *jItem )
{
  PlaylistItem *item;
  json_t       *jObj;
    
/*------------------------------------------------------------------------*\
    Allocate header
\*------------------------------------------------------------------------*/
  item = calloc( 1, sizeof(PlaylistItem) );
  if( !item ) {
    srvmsg( LOG_ERR, "playlistItemFromJSON: out of memeory!" );
    return NULL;
  }
  
/*------------------------------------------------------------------------*\
    Keep instance of JSON object
\*------------------------------------------------------------------------*/
  item->jItem = json_incref( jItem );
  // DBGMSG( "playlistItemFromJSON: JSON refCnt=%d", jItem->refcount );

/*------------------------------------------------------------------------*\
    Extract id for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jItem, "id" );
  if( !jObj || !json_is_string(jObj) ) {
    srvmsg( LOG_ERR, "playlistItemFromJSON: Missing field \"id\"!" );
    Sfree( item );
    json_decref( jItem );
    return NULL; 
  }
  item->id = json_string_value( jObj );   

/*------------------------------------------------------------------------*\
    Extract text for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jItem, "text" );
  if( !jObj || !json_is_string(jObj) ) {
    srvmsg( LOG_ERR, "playlistItemFromJSON: Missing field \"text\"!" );
    Sfree( item );
    json_decref( jItem );
    return NULL; 
  }
  item->text = json_string_value( jObj );   

/*------------------------------------------------------------------------*\
    Extract streaming data list for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jItem, "streamingRefs" );
  if( !jObj || !json_is_array(jObj) ) {
    srvmsg( LOG_ERR, "playlistItemFromJSON: Missing field \"streamingRefs\"!" );
    Sfree( item );
    json_decref( jItem );
    return NULL; 
  }
  item->jStreamingRefs = jObj;   
    
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistItemFromJSON: %p id:%s (%s)", 
                     item, item->text, item->id ); 
  return item;     
}


/*=========================================================================*\
       Delete playlist item (needs to be unliked before!!!)
\*=========================================================================*/
void playlistItemDelete( PlaylistItem *pItem )
{
  DBGMSG( "playlistItemDelete: %p refcnt=%d", pItem, pItem->jItem->refcount ); 
  /* {  
    char *out = json_dumps( pItem->jItem, JSON_PRESERVE_ORDER | JSON_COMPACT | JSON_ENSURE_ASCII );
    DBGMSG( "playlistItemDelete: %s", out );
    free( out );
  } */
    
/*------------------------------------------------------------------------*\
    Free all header features
\*------------------------------------------------------------------------*/
  json_decref( pItem->jItem );

/*------------------------------------------------------------------------*\
    Free header
\*------------------------------------------------------------------------*/
  Sfree( pItem );
}


/*=========================================================================*\
       Add an item before another one
          if anchor is NULL, the item is added to the beginning of the list
\*=========================================================================*/
void playlistAddItemBefore( Playlist *plst, PlaylistItem *anchorItem, 
                                            PlaylistItem *newItem )
{
  DBGMSG( "playlistAddItemBefore: anchor:%p new:%p", anchorItem, newItem ); 

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Default
\*------------------------------------------------------------------------*/
  if( !anchorItem )
    anchorItem = plst->firstItem;
         
/*------------------------------------------------------------------------*\
    Link item to list
\*------------------------------------------------------------------------*/
  newItem->next = anchorItem;
  newItem->prev = anchorItem ? anchorItem->prev : NULL;
  if( anchorItem ) {
    if( anchorItem->prev )
      anchorItem->prev->next = newItem;
    anchorItem->prev = newItem;
  }
  
/*------------------------------------------------------------------------*\
    Adjust list roots
\*------------------------------------------------------------------------*/
  if( !plst->firstItem || plst->firstItem==anchorItem )
    plst->firstItem = newItem;
  if( ! plst->lastItem )
    plst->lastItem = newItem;  

/*------------------------------------------------------------------------*\
    Adjust list count and invalidate cursor index
\*------------------------------------------------------------------------*/
  plst->_numberOfItems ++;  
  plst->_cursorPos = -1;
  
/*------------------------------------------------------------------------*\
    Adjust timestamp
\*------------------------------------------------------------------------*/
  plst->lastChange = srvtime();

/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
}


/*=========================================================================*\
       Add an item after another one
          if anchor is NULL, the item is added to the end of the list
\*=========================================================================*/
void playlistAddItemAfter( Playlist *plst, PlaylistItem *anchorItem, 
                                           PlaylistItem *newItem )
{
   DBGMSG( "playlistAddItemAfter: anchor:%p new:%p", anchorItem, newItem ); 

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Default
\*------------------------------------------------------------------------*/
  if( !anchorItem )
    anchorItem = plst->lastItem;
         
/*------------------------------------------------------------------------*\
    Link item to list
\*------------------------------------------------------------------------*/
  newItem->next = anchorItem ? anchorItem->next : NULL;
  newItem->prev = anchorItem;
  if( anchorItem ) {
    if( anchorItem->next )
      anchorItem->next->prev = newItem;
    anchorItem->next = newItem;
  }
  
/*------------------------------------------------------------------------*\
    Adjust list roots
\*------------------------------------------------------------------------*/
  if( !plst->firstItem )
    plst->firstItem = newItem;
  if( !plst->lastItem || plst->lastItem==anchorItem )
    plst->lastItem = newItem;  

/*------------------------------------------------------------------------*\
    Adjust list count and invalidate cursor index
\*------------------------------------------------------------------------*/
  plst->_numberOfItems ++;  
  plst->_cursorPos = -1;
  
/*------------------------------------------------------------------------*\
    Adjust timestamp
\*------------------------------------------------------------------------*/
  plst->lastChange = srvtime();    

/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
}


/*=========================================================================*\
       Unlink an item from list
         does not check if item is actually a member of this playlist!
         item needs to be deleted afterwards
         links of items will not be changed and can be used after unlinking
         if the cursor item is unlinked, the curser shifts to the next one
\*=========================================================================*/
void playlistUnlinkItem( Playlist *plst, PlaylistItem *pItem )
{
  DBGMSG( "playlistUnlinkItem: %p", pItem ); 

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Unlink item from list
\*------------------------------------------------------------------------*/
  if( pItem->next )
    pItem->next->prev = pItem->prev;
  if( pItem->prev )
    pItem->prev->next = pItem->next;
  if( pItem==plst->firstItem )
    plst->firstItem = pItem->next;
  if( pItem==plst->lastItem )
    plst->lastItem = pItem->prev;
          
/*------------------------------------------------------------------------*\
    Adjust list count and cursor, invlidate cursor index
\*------------------------------------------------------------------------*/
  plst->_numberOfItems --;  
  plst->_cursorPos = -1;
  if( plst->_cursorItem==pItem )
    plst->_cursorItem = pItem->next;
    
/*------------------------------------------------------------------------*\
    Adjust timestamp
\*------------------------------------------------------------------------*/
  plst->lastChange = srvtime();    

/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
