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
struct _playlistItem {
  struct _playlistItem *next;
  struct _playlistItem *prev;
  json_t               *jItem;
  const char           *id;               // weak
  const char           *text;             // weak
  json_t               *jStreamingRefs;   // weak
};

struct _playlist {
  char             *id;
  char             *name;
  double            lastChange;
  int               _numberOfItems;
  int               _cursorPos;
  PlaylistItem     *_cursorItem;
  PlaylistItem     *firstItem;
  PlaylistItem     *lastItem;
  pthread_mutex_t   mutex;
};


/*=========================================================================*\
        Private prototypes
\*=========================================================================*/
static int           _playlistGetCursorPos( Playlist *plst );
static void          _playlistReset( Playlist *plst );
static void          _playlistAddItemBefore( Playlist *plst, PlaylistItem *anchorItem, PlaylistItem *newItem );
static void          _playlistAddItemAfter( Playlist *plst, PlaylistItem *anchorItem, PlaylistItem *newItem );
static void          _playlistUnlinkItem( Playlist *plst, PlaylistItem *pItem );
static void          _playlistTranspose( Playlist *plst, PlaylistItem *pItem1, PlaylistItem *pItem2 );
static PlaylistItem *_playlistGetItem( Playlist *plst, int pos );
static PlaylistItem *_playlistGetItemById( Playlist *plst, const char *id );
// static int           _playlistCheckList( Playlist *plst );


/*=========================================================================*\
       Allocate and init new playlist 
\*=========================================================================*/
Playlist *playlistNew( void )
{
  Playlist *plst = calloc( 1, sizeof(Playlist) );
  if( !plst )
    logerr( "playlistNew: out of memeory!" );
    
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
  	logerr( "Playlist: missing field \"playlistName\"" );
    
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
        logerr( "Playlist: could not parse item #%d", i+1 );
    } 
  }
  else
    logerr( "Playlist: missing field \"items\"" );

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
  DBGMSG( "playlistDelete: %p", plst );
  
/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Free all items
\*------------------------------------------------------------------------*/
  _playlistReset( plst );
  
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
       Get cursor item (aka current track) 
         default is the first item
         returns NULL on empty list
\*=========================================================================*/
PlaylistItem *playlistGetCursorItem( Playlist *plst )
{

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );
    
/*------------------------------------------------------------------------*\
    Set first item as default
\*------------------------------------------------------------------------*/
  if( !plst->_cursorItem )
    plst->_cursorItem = plst->firstItem;
      
/*------------------------------------------------------------------------*\
    Unlock mutex and return item at cursor
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistGetCursorItem %p:  %p", plst, plst->_cursorItem );
  pthread_mutex_unlock( &plst->mutex );
  return plst->_cursorItem;
}


/*=========================================================================*\
       Set cursor position (aka current track)
         count starts with 0 
         returns pointer to queue item at pos or NULL on error
\*=========================================================================*/
PlaylistItem *playlistSetCursorPos( Playlist *plst, int pos )
{
  PlaylistItem *item;

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );
     
/*------------------------------------------------------------------------*\
    Get item at pos
\*------------------------------------------------------------------------*/
  item = _playlistGetItem( plst, pos );

/*------------------------------------------------------------------------*\
    If found: store at pointer and invalidate index
\*------------------------------------------------------------------------*/
  if( item ) {
    plst->_cursorItem = item;
    plst->_cursorPos  = -1;
  }

/*------------------------------------------------------------------------*\
   Unlock and return 
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  DBGMSG( "playlistSetCursor %p: pos=%d -> %p", plst, pos, item );            
  return item;
}


/*=========================================================================*\
       Set cursor position (aka current track) to next entry
         return new item (clipped) or NULL (empty list or end of list)
\*=========================================================================*/
PlaylistItem *playlistIncrCursorItem( Playlist *plst )
{
  PlaylistItem *item = playlistGetCursorItem( plst );

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
       Shuffle playlist
         Shuffles range between startPos and endPos (included)
         If cursor is in that range, it will be set to startPos 
         If cursor is in that range and moveCursorToStart is true, 
           the cursor item will be moved to startPos
         returns pointer to queue item at startpos or NULL on error
\*=========================================================================*/
PlaylistItem *playlistShuffle( Playlist *plst, int startPos, int endPos, bool moveCursorToStart )
{
  PlaylistItem *item1, *item2;
  int           pos = startPos;

  DBGMSG( "playlistShuffle (%p): %d-%d/%d cursorToStart:%s", 
           plst, startPos, endPos, plst->_numberOfItems, moveCursorToStart?"Yes":"No"  );  

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Check end position
\*------------------------------------------------------------------------*/
  if( endPos>=plst->_numberOfItems ) {
    logerr( "playlistShuffle (%p): invalid end position %d/%d", 
            plst, endPos, plst->_numberOfItems );
    pthread_mutex_unlock( &plst->mutex );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Get first element and swap with cursor if requested
\*------------------------------------------------------------------------*/
  item1 = _playlistGetItem( plst, startPos );
  if( !item1 ) {
    logerr( "playlistShuffle (%p): invalid start position %d", plst, startPos );
    pthread_mutex_unlock( &plst->mutex );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Swap with cursor if requested and increment to next element
\*------------------------------------------------------------------------*/
  if( moveCursorToStart ) {
    _playlistTranspose( plst, item1, plst->_cursorItem );
    item1 = plst->_cursorItem->next;
    pos++;
  }

/*------------------------------------------------------------------------*\
    Shuffle: Select the first item from remaining set and step to next one
\*------------------------------------------------------------------------*/
  while( pos<endPos ) {
    int rnd = (int)rndInteger( pos, endPos );
    item2   = _playlistGetItem( plst, rnd );
    if( !item1 || !item2 ) {
      logerr( "playlistShuffle (%p): corrupt linked list @%d(%p)<->%d(%p)/%d", 
               plst, pos, item1, rnd, item2, endPos );
      break;
    }
    DBGMSG( "playlistShuffle (%p): swap %d<->%d/%d ", plst, pos, rnd, endPos  );  
    _playlistTranspose( plst, item1, item2 );
    item1 = item2->next;
    pos++;
  }

/*------------------------------------------------------------------------*\
    Set cursor to start of range
\*------------------------------------------------------------------------*/
  plst->_cursorItem = _playlistGetItem( plst, startPos );
  plst->_cursorPos  = -1;

/*------------------------------------------------------------------------*\
    Unlock mutex and return item under cursor
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
  PlaylistItem *pItem;
  
  DBGMSG( "playlistGetJSON (%p): offset:%d count:%d", plst, offset, count );  

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
  pItem = _playlistGetItem( plst, offset );
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
  PlaylistItem *item;

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );
  
/*------------------------------------------------------------------------*\
    use internal function
\*------------------------------------------------------------------------*/
  item = _playlistGetItem( plst, pos );
     
/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
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
    use internal function
\*------------------------------------------------------------------------*/
  item = _playlistGetItemById( plst, id );
  
/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
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
    logerr( "playlistItemFromJSON: out of memeory!" );
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
    logerr( "playlistItemFromJSON: Missing field \"id\"!" );
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
    logerr( "playlistItemFromJSON: Missing field \"text\"!" );
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
    logerr( "playlistItemFromJSON: Missing field \"streamingRefs\"!" );
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
       Get text for playlist item
\*=========================================================================*/
const char *playlistItemGetText( PlaylistItem *pItem )
{
  return pItem->text;
}


/*=========================================================================*\
       Get id for playlist item
\*=========================================================================*/
const char *playlistItemGetId( PlaylistItem *pItem )
{
  return pItem->id;
}

/*=========================================================================*\
       Get JSON representation of playlist item
\*=========================================================================*/
json_t *playlistItemGetJSON( PlaylistItem *pItem )
{
  return pItem->jItem;
}

/*=========================================================================*\
       Get stream reference list for playlist item
\*=========================================================================*/
json_t *playlistItemGetStreamingRefs( PlaylistItem *pItem )
{
  return pItem->jStreamingRefs;
}


/*=========================================================================*\
       Add track to playlist or replace playlist
         pos        - the position to add the tracks before (if <0: append to end)
         resetFlag  - replace list (pos ignored)
\*=========================================================================*/
int playlistAddItems( Playlist *plst, int pos, json_t *jItems, bool resetFlag )
{
  int           i;
  int           rc = 0;
  PlaylistItem *anchorItem = NULL;  // Item to add list before

  DBGMSG( "playlistAddItems (%p): before:%d reset:%d", plst, pos, resetFlag ); 

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Get anchor item (if any)
\*------------------------------------------------------------------------*/
  if( pos>=0 )
    anchorItem = _playlistGetItem( plst, pos );

/*------------------------------------------------------------------------*\
    Reset playlist?
\*------------------------------------------------------------------------*/
  if( resetFlag )
    _playlistReset( plst );
    
/*------------------------------------------------------------------------*\
    Loop over all items to add
\*------------------------------------------------------------------------*/
  for( i=0; i<json_array_size(jItems); i++ ) {
    json_t  *jItem = json_array_get( jItems, i );

    // Create playlist entry from json payload
    PlaylistItem *pItem = playlistItemFromJSON( jItem );
    if( !pItem ) {
      logerr( "playlistAddItems (%p): could not parse item #%d.", i );
      rc = -1;
    }

    // Add new item to playlist before anchor 
    else if( anchorItem )
      _playlistAddItemBefore( plst, anchorItem, pItem );
      
    // Add new item to end of list
    else
      _playlistAddItemAfter( plst, NULL, pItem );

  }  // next item from list

/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  return rc;
}


/*=========================================================================*\
       Remove items from playlist
\*=========================================================================*/
int playlistDeleteItems( Playlist *plst, json_t *jItems )
{
  int i;
  int rc = 0;

  DBGMSG( "playlistdeteletItems (%p)", plst ); 

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Loop over all items to remove
\*------------------------------------------------------------------------*/
  for( i=0; i<json_array_size(jItems); i++ ) {
    json_t     *jItem = json_array_get( jItems, i );
    json_t     *jObj;
    const char *id    = NULL;
      
    // Get item ID
    jObj = json_object_get( jItem, "id" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "playlistDeleteItems (%p): item #%d lacks field \"id\"", plst, i ); 
      rc = -1;
      continue;
    }              
    id = json_string_value( jObj );
        
    // Get item by explicit position 
    jObj = json_object_get( jItem, "playlistPos" );
    if( jObj && json_is_integer(jObj) ) {
      int           pos   = json_integer_value( jObj );
      PlaylistItem *pItem = _playlistGetItem( plst, pos );
      if( pItem && strcmp(id,pItem->id) ) {
        logwarn( "playlistDeleteItems (%p): item #%d id \"%s\" differs from \"%s\" at explicite position %d.", 
                  plst, i, id, pItem->id, pos );
        rc = -1;
      }
      if( pItem ) { 
        _playlistUnlinkItem( plst, pItem );
        playlistItemDelete( pItem );
      }
      else
        logwarn( "playlistDeleteItems (%p): item #%d with invalid explicite position %d.", 
                  plst, i, pos );        
    }
      
    // Remove all items with this ID
    else for(;;) {
      PlaylistItem *pItem = _playlistGetItemById( plst, id );
      if( !pItem )
        break;
      _playlistUnlinkItem( plst, pItem );
      playlistItemDelete( pItem );
    }

  }  // next item from list

/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  return rc;
}


/*=========================================================================*\
       Move tracks within playlist
         pos - the position to add the tracks before (if <0 append to end)
\*=========================================================================*/
int playlistMoveItems( Playlist *plst, int pos, json_t *jItems )
{
  PlaylistItem **pItems;             // Array of items to move
  int            pItemCnt;           // Elements in pItems 
  PlaylistItem  *anchorItem = NULL;  // Item to add list before
  int            i;
  int            rc = 0;

  DBGMSG( "playlistMoveItems (%p): before:%d", plst, pos ); 

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Get anchor item (if any)
\*------------------------------------------------------------------------*/
  if( pos>=0 )
    anchorItem = _playlistGetItem( plst, pos );

/*------------------------------------------------------------------------*\
    Allocate temporary array of items to move
\*------------------------------------------------------------------------*/
  pItemCnt = 0;
  pItems   = calloc( json_array_size(jItems), sizeof(PlaylistItem *) );
  if( !pItems ) {
    logerr( "playlistMoveItems (%p): out of memory", plst );
    pthread_mutex_unlock( &plst->mutex );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Collect all items to move
\*------------------------------------------------------------------------*/
  for( i=0; i<json_array_size(jItems); i++ ) {
    json_t       *jItem = json_array_get( jItems, i );
    json_t       *jObj;
    const char   *id;
    int           pos;
    PlaylistItem *pItem;
              
    // Get explicit position 
    jObj = json_object_get( jItem, "playlistPos" );
    if( !jObj || !json_is_integer(jObj) ) {
      logerr( "playlistMoveItems (%p): item #%d lacks field \"playlistPos\"", 
               plst, i );
      rc = -1;
      continue;
    } 
    pos   = json_integer_value( jObj );
      
    // Get item 
    pItem = _playlistGetItem( plst, pos );
    if( !pItem ) {
      logwarn( "playlistMoveItems (%p): item #%d has invalid explicite position %d.", 
                plst, i, pos );
      rc = -1;
      continue;
    }

    // Get item ID
    jObj = json_object_get( jItem, "id" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "playlistMoveItems (%p): item #%d lacks field \"id\".", 
               plst, i );
      rc = -1;
      continue;
    }              
    id = json_string_value( jObj );
      
    // Be defensive
    if( strcmp(id,pItem->id) ) {
      logwarn( "playlistMoveItems (%p): item #%d id \"%s\" differs from \"%s\" at explicite position %d.", 
                plst, i, id, pItem->id, pos );
      rc = -1;
    }

    // Be pranoid: check for doubles since unlinking is not stable against double calls
    bool found = 0;
    int j;
    for( j=0; j<pItemCnt && !found; j++ ) {
      found = ( pItems[j]==pItem );
    }
    if( found ) {
      logwarn( "playlistMoveItems (%p): item #%d is double in list (previous instance: %d).", 
                plst, i, j );
      rc = -1;
      continue;                      
    }
      
    // add Item to temporary list 
    pItems[pItemCnt++] = pItem;
      
  } /* for( i=0; i<json_array_size(jItems); i++ ) */
    
/*------------------------------------------------------------------------*\
    Unlink all identified items, but do not delete them
\*------------------------------------------------------------------------*/
  for( i=0; i<pItemCnt; i++ )
    _playlistUnlinkItem( plst, pItems[i] ); 
    
/*------------------------------------------------------------------------*\
    Reinsert the items at new position
\*------------------------------------------------------------------------*/
  for( i=0; i<pItemCnt; i++ ) {
    if( anchorItem )
      _playlistAddItemBefore( plst, anchorItem, pItems[i] );
    else 
      _playlistAddItemAfter( plst, NULL, pItems[i] );
  }
  
/*------------------------------------------------------------------------*\
    Free temporary list of items to move, unlock mutex and return
\*------------------------------------------------------------------------*/
  Sfree( pItems );
  pthread_mutex_unlock( &plst->mutex );
  return rc;
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
    Use internal function
\*------------------------------------------------------------------------*/
  _playlistAddItemBefore( plst, anchorItem, newItem );

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
    Use internal function
\*------------------------------------------------------------------------*/
  _playlistAddItemAfter( plst, anchorItem, newItem );

/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
}


/*=========================================================================*\
       Unlink an item from list
         does not check if item is actually a member of this playlist!
         item needs to be deleted afterwards
         links of item will not be changed and can be used after unlinking
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
    Use internal function
\*------------------------------------------------------------------------*/
  _playlistUnlinkItem( plst, pItem );

/*------------------------------------------------------------------------*\
    Unlock mutex and return
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
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
       Reset a playlist: remove all entries
\*=========================================================================*/
static void _playlistReset( Playlist *plst )
{
  PlaylistItem *item, *next;

/*------------------------------------------------------------------------*\
    Free all items (unlinking not necessary)
\*------------------------------------------------------------------------*/
  for( item=plst->firstItem; item; item=next ) {
    next = item->next;
    playlistItemDelete( item );
  }

/*------------------------------------------------------------------------*\
    Reset pointers
\*------------------------------------------------------------------------*/
  plst->firstItem      = NULL;
  plst->lastItem       = NULL;
  plst->_cursorItem    = NULL;
  plst->_numberOfItems = 0;
  plst->_cursorPos     = 0;
}


/*=========================================================================*\
       Add an item before another one
          if anchor is NULL, the item is added to the beginning of the list
\*=========================================================================*/
static void _playlistAddItemBefore( Playlist *plst, PlaylistItem *anchorItem, 
                                             PlaylistItem *newItem )
{
  DBGMSG( "_playlistAddItemBefore (%p): anchor:%p new:%p", 
           plst, anchorItem, newItem ); 

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
static void _playlistAddItemAfter( Playlist *plst, PlaylistItem *anchorItem, 
                                            PlaylistItem *newItem )
{
   DBGMSG( "_playlistAddItemAfter (%p): anchor:%p new:%p", 
            plst, anchorItem, newItem ); 

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
static void _playlistUnlinkItem( Playlist *plst, PlaylistItem *pItem )
{
  DBGMSG( "_playlistUnlinkItem (%p): %p", plst, pItem ); 

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
       Transpose two items
\*=========================================================================*/
static void _playlistTranspose( Playlist *plst, PlaylistItem *pItem1, PlaylistItem *pItem2 )
{
  PlaylistItem *item;

  DBGMSG( "_playlistTranspose (%p): %p <-> %p", plst, pItem1, pItem2 ); 

/*------------------------------------------------------------------------*\
    Nothing to do?
\*------------------------------------------------------------------------*/
  if( pItem1==pItem2 )
    return;

/*------------------------------------------------------------------------*\
    Swap forward links and adjust backward pointers of following elements
\*------------------------------------------------------------------------*/
  item = pItem1->next;
  pItem1->next = pItem2->next;
  pItem2->next = item;
  if( pItem1->next )
    pItem1->next->prev = pItem1;
  if( pItem2->next )
    pItem2->next->prev = pItem2;

/*------------------------------------------------------------------------*\
    Swap backward links and adjust forward pointers of previous elements
\*------------------------------------------------------------------------*/
  item = pItem1->prev;
  pItem1->prev = pItem2->prev;
  pItem2->prev = item;
  if( pItem1->prev )
    pItem1->prev->next = pItem1;
  if( pItem2->prev )
    pItem2->prev->next = pItem2;

/*------------------------------------------------------------------------*\
    Adjust root pointers
\*------------------------------------------------------------------------*/
  if( !pItem2->prev )
    plst->firstItem = pItem2;
  else if( !pItem1->prev )
    plst->firstItem = pItem1;
  if( !pItem2->next )
    plst->lastItem = pItem2;
  else if( !pItem1->next )
    plst->lastItem = pItem1;

/*------------------------------------------------------------------------*\
    Invlidate cursor index if cursor is part of the exchange
\*------------------------------------------------------------------------*/
  if( plst->_cursorItem==pItem1 || plst->_cursorItem==pItem2  )
    plst->_cursorPos = -1;
    
/*------------------------------------------------------------------------*\
    Adjust timestamp
\*------------------------------------------------------------------------*/
  plst->lastChange = srvtime();
}


/*=========================================================================*\
       Get item at a given position
         count starts with 0
         returns weak pointer to item or NULL (empty list, pos out of bounds)
\*=========================================================================*/
static PlaylistItem *_playlistGetItem( Playlist *plst, int pos )
{
  PlaylistItem *item = plst->firstItem;
#ifdef DEBUG
  int posbuf = pos;
#endif

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
    Return
\*------------------------------------------------------------------------*/
  DBGMSG( "_playlistGetItem (%p): pos=%d -> %p", plst, posbuf, item );         
  return item;         
}


/*=========================================================================*\
       Get first item with a given ID
         returns weak pointer to item or NULL if not found
\*=========================================================================*/
static PlaylistItem *_playlistGetItemById( Playlist *plst, const char *id )
{
  PlaylistItem *item;

/*------------------------------------------------------------------------*\
    Loop over list and check Id
\*------------------------------------------------------------------------*/
  for( item=plst->firstItem; item; item=item->next )
    if( !strcmp(item->id,id) )
      break;
  
/*------------------------------------------------------------------------*\
    Return
\*------------------------------------------------------------------------*/
  DBGMSG( "_playlistGetItemById(%p): id=\"%s\" -> %p", plst, id, item );         
  return item;         
}

/*=========================================================================*\
       Check consistency of playlist
\*=========================================================================*/
#if 0
static int _playlistCheckList( Playlist *plst )
{
  PlaylistItem *item, *last = NULL;
  int           i, rc = 0;

/*------------------------------------------------------------------------*\
    Loop over list and check bakward links
\*------------------------------------------------------------------------*/
  for( i=0,item=plst->firstItem; item; i++,item=item->next ) {
    DBGMSG( "item #%d: %p <%p,%p> (%s)", i, item, item->prev, item->next, item->text );
    if( item->prev!=last ) {
      logerr( "item #%d (%p, %s): prevpointer %p corrupt (should be %p)",
              i, item, item->text, item->prev, last );
      rc = -1;
    }
    last = item;
  }

/*------------------------------------------------------------------------*\
    Loop over list and check bakward links
\*------------------------------------------------------------------------*/
  last = NULL;
  for( i=plst->_numberOfItems-1,item=plst->lastItem; item; i--,item=item->prev ) {
    DBGMSG( "item #%d: %p <%p,%p> (%s)", i, item, item->prev, item->next, item->text );
    if( item->next!=last ) {
      logerr( "item #%d (%p, %s): nextpointer %p corrupt (should be %p)",
              i, item, item->text, item->next, last );
      rc = -1;
    }
    last = item;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return rc;
}
#endif
/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/

