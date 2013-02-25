/*$*********************************************************************\

Name            : -

Source File     : playlist.c

Description     : handle playlist (used as player queue) 

Comments        : -

Called by       : player, ickstream wrapper 

Calls           : 

Error Messages  : -
  
Date            : 22.02.2013

Updates         : 14.02.2013 make queue persistent // MAF
                  
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
#include <ickDiscovery.h>

#include "ickpd.h"
#include "persist.h"
#include "playlist.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	private symbols
\*=========================================================================*/
static Playlist *playerQueue;


/*=========================================================================*\
       Get player queue 
\*=========================================================================*/
Playlist *playlistGetPlayerQueue( void )
{
  json_t   *jQueue;
  json_t   *jObj;
  int       i;
  
/*------------------------------------------------------------------------*\
    Return queue if alredy initialized
\*------------------------------------------------------------------------*/
  if( playerQueue )
    return playerQueue;
  
/*------------------------------------------------------------------------*\
    Lazy init: Create queue
\*------------------------------------------------------------------------*/
  playerQueue = playlistNew();   
      
/*------------------------------------------------------------------------*\
    Lazy init: If not in persistent repository return empty list
\*------------------------------------------------------------------------*/
  jQueue = persistGetJSON( "PlayerQueue" );
  if( !jQueue )
    return playerQueue; 
 
/*------------------------------------------------------------------------*\
    Lazy init: Create playlist header from JSON 
\*------------------------------------------------------------------------*/

  // ID
  jObj = json_object_get( jQueue, "playlistId" );
  if( jObj && json_is_string(jObj) )
    playlistSetId( playerQueue, json_string_value(jObj) );
  else
  	srvmsg( LOG_ERR, "Playlist: missing field \"playlistId\"" );

  // Name
  jObj = json_object_get( jQueue, "playlistName" );
  if( jObj && json_is_string(jObj) )
    playlistSetName( playerQueue, json_string_value(jObj) );
  else
  	srvmsg( LOG_ERR, "Playlist: missing field \"playlistName\"" );

  // Position (optional)
  jObj = json_object_get( jQueue, "playlistPos" );
  if( jObj && json_is_string(jObj) )
    playlistSetCursorPos( playerQueue, json_integer_value(jObj) );
    
  // Last Modification
  jObj = json_object_get( jQueue, "lastChanged" );
  if( jObj && json_is_real(jObj) )
    playerQueue->lastChange = json_real_value( jObj );
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
        playlistAddItemAfter( playerQueue, NULL, pItem );
      else
        srvmsg( LOG_ERR, "Playlist: could not parse item #%d", i+1 );
    } 
  }
  else
    srvmsg( LOG_ERR, "Playlist: missing field \"items\"" );

/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
  return playerQueue;
}

/*=========================================================================*\
       Free player queue 
\*=========================================================================*/
void playlistFreePlayerQueue( bool usePersistence )
{
  if( !playerQueue )
    return;
  
/*------------------------------------------------------------------------*\
    Write to persistent storage
\*------------------------------------------------------------------------*/
  if( usePersistence )
    persistSetJSON_new( "PlayerQueue", playlistGetJSON(playerQueue,0,0) ); 
  else
    persistRemove( "PlayerQueue" );
      
/*------------------------------------------------------------------------*\
    Free playlist in memory
\*------------------------------------------------------------------------*/
  playlistDelete( playerQueue );
  playerQueue = NULL;
}



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
  plst->id   = strdup( "<undefined>" );
  plst->name = strdup( "ickpd player queue" );
  
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  srvmsg( LOG_DEBUG, "playlistNew: %p", plst );
  return plst; 
}

/*=========================================================================*\
       Delete and free playlist 
\*=========================================================================*/
void playlistDelete( Playlist *plst )
{
  PlaylistItem *item, *next;

  srvmsg( LOG_DEBUG, "playlistDelete: %p", plst );
  
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
    Free header
\*------------------------------------------------------------------------*/
  Sfree( plst );
}


/*=========================================================================*\
       Set ID for playlist 
\*=========================================================================*/
void playlistSetId( Playlist *plst, const char *id )
{
  Sfree( plst->id );
  plst->id = strdup( id );    
  srvmsg( LOG_DEBUG, "playlistSetID: %s", id ); 
}


/*=========================================================================*\
       Set Name for playlist 
\*=========================================================================*/
void playlistSetName( Playlist *plst, const char *name )
{
  Sfree( plst->name );
  plst->name = strdup( name );    
  srvmsg( LOG_DEBUG, "playlistSetName: %s", name ); 
}


/*=========================================================================*\
       Get ID for playlist 
\*=========================================================================*/
const char *playlistGetId( Playlist *plst )
{
  srvmsg( LOG_DEBUG, "playlistGetID: %s", plst->id ); 
  return plst->id;
}


/*=========================================================================*\
       Get Name for playlist 
\*=========================================================================*/
const char *playlistGetName( Playlist *plst )
{
  srvmsg( LOG_DEBUG, "playlistGetName: %s", plst->name ); 
  return plst->name;
}


/*=========================================================================*\
       Get length of playlist 
\*=========================================================================*/
int playlistGetLength( Playlist *plst )
{
  srvmsg( LOG_DEBUG, "playlistGetLength: %d", plst->_numberOfItems ); 
  return plst->_numberOfItems;    
}

/*=========================================================================*\
       Get timestamp of last change 
\*=========================================================================*/
double playlistGetLastChange( Playlist *plst )
{
  srvmsg( LOG_DEBUG, "playlistGetLastChange: %g", plst->lastChange ); 
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
  srvmsg( LOG_DEBUG, "playlistGetCursorPos: %d", plst->_cursorPos );              
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
    Get first item as default
\*------------------------------------------------------------------------*/
  if( !plst->_cursorItem )
    plst->_cursorItem = plst->firstItem;
      
/*------------------------------------------------------------------------*\
    Return item at cursor
\*------------------------------------------------------------------------*/
  srvmsg( LOG_DEBUG, "playlistGetCursorItem: %p", plst->_cursorItem );       
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
  item = playlistGetItem( plst, pos );
  if( item ) {
    plst->_cursorPos  = pos;
    plst->_cursorItem = item;
  }
  
  // error handling
  else
    pos = -1;
    
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  srvmsg( LOG_DEBUG, "playlistSetCursor: %d", pos );            
  return pos;
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
  
  srvmsg( LOG_DEBUG, "playlistGetJSON: offset:%d count:%d", offset, count );  

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
    // srvmsg( LOG_DEBUG, "playlistGetJSON: JSON refCnt=%d", pItem->jItem->refcount );
    pItem = pItem->next;
  }  
  
/*------------------------------------------------------------------------*\
    Build header
\*------------------------------------------------------------------------*/
  jResult = json_pack( "{ss ss ss sf si si si so}",
                         "jsonrpc",      "2.0", 
                         "playlistId",   plst->id,
                         "playlistName", plst->name,
                         /* "playlistPos",  plst->_cursorPos, */
                         "lastChanged",  plst->lastChange,
                         "count",        json_array_size(jResult),
                         "countAll",     playlistGetLength(plst),
                         "offset",       offset,
                         "items",        jResult );   
                                                    
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  srvmsg( LOG_DEBUG, "playlistGetJSON: result %p", jResult ); 
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
    That's all
\*------------------------------------------------------------------------*/
  srvmsg( LOG_DEBUG, "playlistGetItem: %p", item );         
  return item;         
}

/*=========================================================================*\
       Get first item with a given ID
         returns weak pointer to item or NULL if nor found
\*=========================================================================*/
PlaylistItem *playlistGetItemById( Playlist *plst, const char *id )
{
  PlaylistItem *item = plst->firstItem;
  
/*------------------------------------------------------------------------*\
    Loop over list and check Id
\*------------------------------------------------------------------------*/
  while( item ) {
    if( !strcmp(item->id,id) )
      break;
    item = item->next;
  }
  
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  srvmsg( LOG_DEBUG, "playlistGetItemById(%s): %p", id, item );         
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
  // srvmsg( LOG_DEBUG, "playlistItemFromJSON: JSON refCnt=%d", jItem->refcount );

/*------------------------------------------------------------------------*\
    Extract id for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jItem, "id" );
  if( !jObj ) {
    srvmsg( LOG_ERR, "playlistItemFromJSON: Missing field \"id\"!" );
    Sfree( item );
    json_decref( jItem );
    return NULL; 
  }
  item->id = json_string_value( jObj );   
    
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  srvmsg( LOG_DEBUG, "playlistItemFromJSON: %p id:%s", item, item->id ); 
  return item;     
}


/*=========================================================================*\
       Delete playlist item (needs to be unliked before!!!)
\*=========================================================================*/
void playlistItemDelete( PlaylistItem *pItem )
{
  srvmsg( LOG_DEBUG, "playlistItemDelete: %p refcnt=%d", pItem, pItem->jItem->refcount ); 
  /* {  
    char *out = json_dumps( pItem->jItem, JSON_PRESERVE_ORDER | JSON_COMPACT | JSON_ENSURE_ASCII );
    srvmsg( LOG_DEBUG, "playlistItemDelete: %s", out );
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
  srvmsg( LOG_DEBUG, "playlistAddItemBefore: anchor:%p new:%p", anchorItem, newItem ); 
 
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
}


/*=========================================================================*\
       Add an item after another one
          if anchor is NULL, the item is added to the end of the list
\*=========================================================================*/
void playlistAddItemAfter( Playlist *plst, PlaylistItem *anchorItem, 
                                           PlaylistItem *newItem )
{
   srvmsg( LOG_DEBUG, "playlistAddItemAfter: anchor:%p new:%p", anchorItem, newItem ); 

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
  srvmsg( LOG_DEBUG, "playlistUnlinkItem: %p", pItem ); 

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
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
