/*$*********************************************************************\

Name            : -

Source File     : playlist.c

Description     : handle playlist (used as player queue) 

Comments        : -

Called by       : player, ickstream wrapper 

Calls           : 

Error Messages  : -
  
Date            : 22.02.2013

Updates         : 14.03.2013 protect list modifications by mutex //MAF
                  21.07.2013 introduced list mapping             //MAF
                  
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
#include <pthread.h>
#include <jansson.h>
#include <ickDiscovery.h>

#include "ickutils.h"
#include "playlist.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
	Private definitions and symbols
\*=========================================================================*/
struct _playlistItem {
  struct _playlistItem *nextOriginal;     // The original order
  struct _playlistItem *prevOriginal;
  struct _playlistItem *nextMapped;       // Order as played (when shuffled)
  struct _playlistItem *prevMapped;
  json_t               *jItem;
  const char           *id;               // weak
  const char           *text;             // weak
  PlaylistItemType      type;
  json_t               *jStreamingRefs;   // weak
  pthread_mutex_t       mutex;
};

struct _playlist {
  char             *id;
  char             *name;
  double            lastChange;
  int               _numberOfItems;
  int               _cursorPos;
  PlaylistItem     *_cursorItem;
  PlaylistItem     *firstItemOriginal;    // Root for original list
  PlaylistItem     *lastItemOriginal;
  PlaylistItem     *firstItemMapped;      // Root of mapped list
  PlaylistItem     *lastItemMapped;
  pthread_mutex_t   mutex;
};

// Enable or disable consistency checks (performance)
#ifdef ICK_DEBUG
#define CONSISTENCYCHECKING
#endif


/*=========================================================================*\
        Private prototypes
\*=========================================================================*/
static int  _playlistItemFillHeader( PlaylistItem *pItem );
static void _playlistAddItemBefore( Playlist *plst, PlaylistItem *anchorItem, PlaylistSortType order, PlaylistItem *newItem );
static void _playlistAddItemAfter( Playlist *plst, PlaylistItem *anchorItem, PlaylistSortType order, PlaylistItem *newItem );
static void _playlistUnlinkItem( Playlist *plst, PlaylistItem *pItem, PlaylistSortType order );

#ifdef CONSISTENCYCHECKING
#define CHKLIST( p ) _playlistCheckList( __FILE__, __LINE__, (p) );
static int _playlistCheckList( const char *file, int line, Playlist *plst );
#else
#define CHKLIST( p ) {}
#endif
#define GETITEMTXT(item) ((item)?(item)->text:"none")


/*=========================================================================*\
       Allocate and init new playlist 
\*=========================================================================*/
Playlist *playlistNew( void )
{
  Playlist *plst;

/*------------------------------------------------------------------------*\
    Create header
\*------------------------------------------------------------------------*/
  plst = calloc( 1, sizeof(Playlist) );
  if( !plst )
    logerr( "playlistNew: out of memeory!" );

/*------------------------------------------------------------------------*\
    Init header fields
\*------------------------------------------------------------------------*/
  // plst->id   = strdup( "<undefined>" );
  // plst->name = strdup( "ickpd player queue" );
 
/*------------------------------------------------------------------------*\
    Init mutex in errr check mode
\*------------------------------------------------------------------------*/
  ickMutexInit( &plst->mutex );
 
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistNew: %p", plst );
  return plst; 
}


/*=========================================================================*\
       Get playlist from JSON buffer
         if jQueue is NULL, an empty playlist is created

\*=========================================================================*/
Playlist *playlistFromJSON( json_t *jQueue )
{
  Playlist *plst;
  json_t   *jObj;
  int       i;

  DBGMSG( "playlistFromJSON: %p", jQueue );

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
  if( jObj && !json_is_string(jObj) )
    logwarn( "Playlist: \"playlistId\" is no string." );
  else if( jObj )
    playlistSetId( plst, json_string_value(jObj) );

  // Name (optional)
  jObj = json_object_get( jQueue, "playlistName" );
  if( jObj && !json_is_string(jObj) )
    logwarn( "Playlist: \"playlistName\" is no string." );
  else if( jObj )
    playlistSetName( plst, json_string_value(jObj) );
    
  // Last Modification
  jObj = json_object_get( jQueue, "lastChanged" );
  if( jObj && !json_is_real(jObj) )
    logwarn( "Playlist: \"lastChanged\" is no real." );
  else if( jObj )
    plst->lastChange = json_real_value( jObj );

  // Get list of items
  jObj = json_object_get( jQueue, "items" );
  if( jObj && json_is_array(jObj) ) {
  
    // Loop over all items and add them to list
    for( i=0; i<json_array_size(jObj); i++ ) {
      json_t       *jItem = json_array_get( jObj, i );
      PlaylistItem *pItem = playlistItemFromJSON( jItem );
      if( pItem )
        _playlistAddItemAfter( plst, NULL, PlaylistOriginal, pItem );
      else
        logerr( "Playlist: could not parse playlist item #%d", i+1 );
    } 

    // Store number of items
    plst->_numberOfItems = json_array_size(jObj);
  }
  else
    logerr( "Playlist: missing field \"items\"" );

  // Get mapping
  jObj = json_object_get( jQueue, "mapping" );
  if( jObj && !json_is_array(jObj) ) {
    logerr( "Playlist: \"mapping\" is no array" );
    jObj = NULL;
  } else if( jObj && json_array_size(jObj)!=plst->_numberOfItems ) {
    logerr( "Playlist: mapping list length differs from playlist items" );
    jObj = NULL;
  }

  // Apply mapping
  if( !jObj ) {
    DBGMSG( "playlistFromJSON: found no mapping, using id.");
    playlistResetMapping( plst );
  }
  else for( i=0; i<json_array_size(jObj); i++ ) {
    json_t       *jPos = json_array_get( jObj, i );
    PlaylistItem *pItem;
    if( !json_is_integer(jPos) ) {
      logwarn( "Playlist: mapping item #%d is not an integer" );
      continue;
    }
    pItem = playlistGetItem( plst, PlaylistOriginal, json_integer_value(jPos) );
    if( pItem )
      _playlistAddItemAfter( plst, NULL, PlaylistMapped, pItem );
    else
      logerr( "Playlist: could not find mapped item #%d (position %s)",
              i+1, json_integer_value(jPos) );
  }

/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
  CHKLIST( plst );
  return plst;
}


/*=========================================================================*\
       Delete and free playlist 
\*=========================================================================*/
void playlistDelete( Playlist *plst )
{
  DBGMSG( "playlistDelete: %p", plst );
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   pthread_mutex_lock( &plst->mutex );

/*------------------------------------------------------------------------*\
    Free all items and reset header
\*------------------------------------------------------------------------*/
  playlistReset( plst, true );
  
/*------------------------------------------------------------------------*\
    Unlock and free header
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &plst->mutex );
  Sfree( plst );
}


/*=========================================================================*\
       Lock playlist
\*=========================================================================*/
void playlistLock( Playlist *plst )
{
  DBGMSG( "playlist (%p): lock", plst );
  pthread_mutex_lock( &plst->mutex );
}

/*=========================================================================*\
       Unlock playlist
\*=========================================================================*/
void playlistUnlock( Playlist *plst )
{
  DBGMSG( "playlist (%p): unlock", plst );
  pthread_mutex_unlock( &plst->mutex );
}


/*=========================================================================*\
       Reset a playlist: remove all entries
         resetHeader - also reset name and ID
\*=========================================================================*/
void playlistReset( Playlist *plst, bool resetHeader )
{
  PlaylistItem *item, *next;
  DBGMSG( "playlistReset (%p)", plst );
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Free all items (unlinking not necessary)
\*------------------------------------------------------------------------*/
  for( item=plst->firstItemOriginal; item; item=next ) {
    next = item->nextOriginal;
    playlistItemDelete( item );
  }

/*------------------------------------------------------------------------*\
    Reset pointers
\*------------------------------------------------------------------------*/
  plst->firstItemOriginal = NULL;
  plst->lastItemOriginal  = NULL;
  plst->firstItemMapped   = NULL;
  plst->lastItemMapped    = NULL;
  plst->_cursorItem       = NULL;
  plst->_numberOfItems    = 0;
  plst->_cursorPos        = 0;

/*------------------------------------------------------------------------*\
    Free all header features
\*------------------------------------------------------------------------*/
  if( resetHeader ) {
    Sfree( plst->id );
    Sfree( plst->name );
  }

/*------------------------------------------------------------------------*\
    Set timestamp
\*------------------------------------------------------------------------*/
  plst->lastChange = srvtime();

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
       Init or reset mapping to id
\*=========================================================================*/
void playlistResetMapping( Playlist *plst )
{
  PlaylistItem *walk;

  DBGMSG( "playlistResetMapping: %p", plst );

/*------------------------------------------------------------------------*\
    Reset root pointers
\*------------------------------------------------------------------------*/
  plst->firstItemMapped = plst->firstItemOriginal;
  plst->lastItemMapped  = plst->lastItemOriginal;

/*------------------------------------------------------------------------*\
    Copy pointers off all elements
\*------------------------------------------------------------------------*/
  for( walk=plst->firstItemOriginal; walk; walk=walk->nextOriginal ) {
    walk->nextMapped = walk->nextOriginal;
    walk->prevMapped = walk->prevOriginal;
  }

/*------------------------------------------------------------------------*\
    Invalidate cursor index and set timestamp
\*------------------------------------------------------------------------*/
  plst->_cursorPos = -1;
  plst->lastChange = srvtime();

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  CHKLIST( plst );
}


/*=========================================================================*\
       Set ID for playlist 
\*=========================================================================*/
void playlistSetId( Playlist *plst, const char *id )
{
  DBGMSG( "playlistSetID %p: %s", plst, id?id:"NULL" ); 
  CHKLIST( plst );

  Sfree( plst->id );
  plst->id = strdup( id );
}


/*=========================================================================*\
       Set Name for playlist 
\*=========================================================================*/
void playlistSetName( Playlist *plst, const char *name )
{
  DBGMSG( "playlistSetName %p: %s", plst, name?name:"NULL" ); 
  CHKLIST( plst );

  Sfree( plst->name );
  plst->name = strdup( name );
}


/*=========================================================================*\
       Get ID for playlist 
\*=========================================================================*/
const char *playlistGetId( Playlist *plst )
{
  DBGMSG( "playlistGetID %p: %s", plst, plst->id?plst->id:"NULL" );
  CHKLIST( plst );
 
  return plst->id;
}


/*=========================================================================*\
       Get Name for playlist 
\*=========================================================================*/
const char *playlistGetName( Playlist *plst )
{
  DBGMSG( "playlistGetName %p: %s", plst, plst->name?plst->name:"NULL" );
  CHKLIST( plst );

  return plst->name;
}


/*=========================================================================*\
       Get length of playlist 
\*=========================================================================*/
int playlistGetLength( Playlist *plst )
{
  DBGMSG( "playlistGetLength %p: %d", plst, plst->_numberOfItems );
  CHKLIST( plst );

  return plst->_numberOfItems;
}


/*=========================================================================*\
       Get timestamp of last change 
\*=========================================================================*/
double playlistGetLastChange( Playlist *plst )
{
  DBGMSG( "playlistGetLastChange %p: %lf", plst, plst->lastChange ); 
  CHKLIST( plst );

  return plst->lastChange;
}


/*=========================================================================*\
       Get cursor position (aka current track) in mapped list
         counting starts with 0 (which is also the default)
         return 0 on empty list
\*=========================================================================*/
int playlistGetCursorPos( Playlist *plst )
{
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Find position of current item if index is invalid
\*------------------------------------------------------------------------*/
  if( plst->_cursorPos<0 ) {
    int pos = 0;
    PlaylistItem *item = plst->firstItemMapped;
    while( item && item!=plst->_cursorItem && plst->_cursorItem ) {
      item = item->nextMapped;
      pos++;
    }
    plst->_cursorPos = pos;
  }

/*------------------------------------------------------------------------*\
    Return position
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistGetCursorPos %p: %d", plst, plst->_cursorPos );
  return plst->_cursorPos;
}


/*=========================================================================*\
       Get cursor item (aka current track) 
         default is the first item
         returns NULL on empty list
\*=========================================================================*/
PlaylistItem *playlistGetCursorItem( Playlist *plst )
{
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Set first item from mapped list as default
\*------------------------------------------------------------------------*/
  if( !plst->_cursorItem )
    plst->_cursorItem = plst->firstItemMapped;
      
/*------------------------------------------------------------------------*\
    Return item at cursor
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistGetCursorItem %p: %p (%s)", plst, plst->_cursorItem,
          GETITEMTXT(plst->_cursorItem) );
  return plst->_cursorItem;
}


/*=========================================================================*\
       Set cursor position (aka current track) to position in mapped list
         count starts with 0 
         returns pointer to queue item at pos or NULL on error
\*=========================================================================*/
PlaylistItem *playlistSetCursorPos( Playlist *plst, int pos )
{
  PlaylistItem *item;

/*------------------------------------------------------------------------*\
    Get item at pos, use mapping
\*------------------------------------------------------------------------*/
  item = playlistGetItem( plst, PlaylistMapped, pos );

/*------------------------------------------------------------------------*\
    If found: store at pointer and invalidate index
\*------------------------------------------------------------------------*/
  if( item ) {
    plst->_cursorItem = item;
    plst->_cursorPos  = -1;
  }

/*------------------------------------------------------------------------*\
   That's all
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistSetCursor %p: pos=%d -> %p (%s)",
          plst, pos, item, GETITEMTXT(item) );
  return item;
}


/*=========================================================================*\
       Set cursor position (aka current track) to next entry in mapped list
         return new item (clipped) or NULL (empty list or end of list)
\*=========================================================================*/
PlaylistItem *playlistIncrCursorItem( Playlist *plst )
{
  PlaylistItem *item = playlistGetCursorItem( plst );

  DBGMSG( "playlistIncrCursorPos %p: items %p->%p",
           plst, item, item?item->nextOriginal:NULL );
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    No successor?
\*------------------------------------------------------------------------*/
  if( !item || !item->nextMapped )
    return NULL;

/*------------------------------------------------------------------------*\
    Set successor
\*------------------------------------------------------------------------*/
  plst->_cursorItem = item->nextMapped;

/*------------------------------------------------------------------------*\
    Return position in list (increment only if valid index is stored)
\*------------------------------------------------------------------------*/
  if( plst->_cursorPos>=0 )
    plst->_cursorPos++;

/*------------------------------------------------------------------------*\
    Return current item
\*------------------------------------------------------------------------*/
  return plst->_cursorItem;
}


/*=========================================================================*\
       Get playlist in ickstream JSON format for method "getPlaylist"
         order selects the original or mapped order or an hybrid format
         offset is the offset of the first element to be included
         count is the (max.) number of elements included
\*=========================================================================*/
json_t *playlistGetJSON( Playlist *plst, PlaylistSortType order, int offset, int count )
{
  json_t       *jResult  = json_array();
  json_t       *jMapping = NULL;
  PlaylistItem *pItem;
  int           i;

  DBGMSG( "playlistGetJSON (%p): order: %d offset:%d count:%d",
          order, plst, offset, count );

  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    A zero count argument means all
\*------------------------------------------------------------------------*/
  if( !count )
    count = playlistGetLength(plst);

/*------------------------------------------------------------------------*\
    Hybrid order means original list with mapping
\*------------------------------------------------------------------------*/
  if( order==PlaylistHybrid ) {
    order    = PlaylistOriginal;
    jMapping = json_array();
  }

/*------------------------------------------------------------------------*\
    Collect all requested items
\*------------------------------------------------------------------------*/
  pItem = playlistGetItem( plst, order, offset );
  for( i=count; pItem && i; i-- ) {
    json_array_append( jResult, pItem->jItem );  // strong
    // DBGMSG( "playlistGetJSON: JSON refCnt=%d", pItem->jItem->refcount );
    pItem = playlistItemGetNext( pItem, order );
  }

/*------------------------------------------------------------------------*\
    Collect mapping
\*------------------------------------------------------------------------*/
  if( jMapping ) {
    pItem = playlistGetItem( plst, order, offset );
    for( i=count; pItem && i; i-- ) {
      int pos = playlistGetItemPos( plst, PlaylistMapped, pItem );
      json_array_append_new( jMapping, json_integer(pos) );  // steal reference
      pItem = playlistItemGetNext( pItem, PlaylistOriginal );
    }
  }

/*------------------------------------------------------------------------*\
    Build header
\*------------------------------------------------------------------------*/
  jResult = json_pack( "{ss sf si si si sb so}",
                         "jsonrpc",       "2.0",
                         "lastChanged",   plst->lastChange,
                         "count",         json_array_size(jResult),
                         "countAll",      playlistGetLength(plst),
                         "offset",        offset,
                         "originalOrder", order==PlaylistOriginal,
                         "items",         jResult );
  // Mapping, Name and ID are optional
  if( jMapping )
    json_object_set_new( jResult, "mapping", jMapping );
  if( plst->id )
    json_object_set_new( jResult, "playlistId", json_string(plst->id) );
  if( plst->name )
    json_object_set_new( jResult, "playlistName", json_string(plst->name) );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistGetJSON: result %p", jResult ); 
  return jResult;                       
}


/*=========================================================================*\
       Get item at a given position
         order selects if the original or shuffled order is used
         count starts with 0
         returns weak pointer to item or NULL (empty list, pos out of bounds)
\*=========================================================================*/
PlaylistItem *playlistGetItem( Playlist *plst, PlaylistSortType order, int pos )
{
  PlaylistItem *item = order==PlaylistOriginal ? plst->firstItemOriginal :
                                                 plst->firstItemMapped;

#ifdef ICK_DEBUG
  int posbuf = pos;
#endif
  //CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Check for lower bound
\*------------------------------------------------------------------------*/
  if( pos<0 )
    item = NULL;

/*------------------------------------------------------------------------*\
    Loop over list
\*------------------------------------------------------------------------*/
  while( pos-- && item )
   item = playlistItemGetNext( item, order );

/*------------------------------------------------------------------------*\
    Return
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistGetItem (%p): order=%s pos=%d -> %p (%s)",
          plst, order==PlaylistOriginal ? "original" : "mapped",
          posbuf, item, GETITEMTXT(item) );
  return item;         
}


/*=========================================================================*\
       Get the position of an item in a sorting
         order selects if the original or shuffled order is used
         position starts with 0, -1 means item is not found in list
\*=========================================================================*/
int playlistGetItemPos( Playlist *plst, PlaylistSortType order, PlaylistItem *item )
{
  int           pos  = 0;
  PlaylistItem *walk = order==PlaylistOriginal ? plst->firstItemOriginal :
                                                 plst->firstItemMapped;
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Loop over list and search for item
\*------------------------------------------------------------------------*/
  while( walk && walk!=item ) {
    pos++;
    walk = playlistItemGetNext( walk, order );
  }
  if( !walk )
    pos = -1;

/*------------------------------------------------------------------------*\
    Return
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistGetItemPos (%p-%s): item=%p (%s) -> %d ",
          plst, order==PlaylistOriginal ? "original" : "mapped",
          item, GETITEMTXT(item), pos );
  return pos;
}


/*=========================================================================*\
       Get first item with a given ID
         returns weak pointer to item or NULL if nor found
\*=========================================================================*/
PlaylistItem *playlistGetItemById( Playlist *plst, const char *id )
{
  PlaylistItem *item;
  CHKLIST( plst );

 /*------------------------------------------------------------------------*\
     Loop over list and check Id
 \*------------------------------------------------------------------------*/
   for( item=plst->firstItemOriginal; item; item=item->nextOriginal )
     if( !strcmp(item->id,id) )
       break;

 /*------------------------------------------------------------------------*\
     Return
 \*------------------------------------------------------------------------*/
   DBGMSG( "playlistGetItemById(%p): id=\"%s\" -> %p (%s)",
           plst, id, item, GETITEMTXT(item)  );
   return item;
}


/*=========================================================================*\
       Add items to playlist or replace playlist
         oPos       - the position in the original list to add the items
                      before (if <0: append to end)
         mPos       - the position in the mapped list to add the items
                      before (if <0: append to end)
         resetFlag  - replace list (oPos and mPos are ignored)
         jItems     - array with items, might be NULL
       returns 0 on success
\*=========================================================================*/
int playlistAddItems( Playlist *plst, int oPos, int mPos, json_t *jItems, bool resetFlag )
{
  int           i;
  int           rc = 0;
  PlaylistItem *oAnchor = NULL;  // Itam in original list to add list before
  PlaylistItem *mAnchor = NULL;  // Item in mapped list to add list before

  DBGMSG( "playlistAddItems (%p): oPos:%d mPos:%d reset:%d",
           plst, oPos, mPos, resetFlag );
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Reset playlist?
\*------------------------------------------------------------------------*/
  if( resetFlag )
    playlistReset( plst, false );

/*------------------------------------------------------------------------*\
    Nothing to do?
\*------------------------------------------------------------------------*/
  if( !jItems || !json_array_size(jItems) )
    return rc;

/*------------------------------------------------------------------------*\
    Get anchor items (if any and not in reset mode)
\*------------------------------------------------------------------------*/
  if( oPos>=0 && !resetFlag )
    oAnchor = playlistGetItem( plst, PlaylistOriginal, oPos );
  if( mPos>=0 && !resetFlag )
    mAnchor = playlistGetItem( plst, PlaylistMapped, mPos );

/*------------------------------------------------------------------------*\
    Loop over all items to add
\*------------------------------------------------------------------------*/
  for( i=0; i<json_array_size(jItems); i++ ) {
    json_t  *jItem = json_array_get( jItems, i );

    // Create an playlist item from json payload
    PlaylistItem *pItem = playlistItemFromJSON( jItem );
    if( !pItem ) {
      logerr( "playlistAddItems (%p): could not parse item #%d.", i );
      rc = -1;
      continue;
    }

    // Add new item to original list before anchor if given or to end otherwise
    if( oAnchor )
      _playlistAddItemBefore( plst, oAnchor, PlaylistOriginal, pItem );
    else
      _playlistAddItemAfter( plst, NULL, PlaylistOriginal, pItem );

    // Add new item to mapped list before anchor if given or to end otherwise
    if( mAnchor )
      _playlistAddItemBefore( plst, mAnchor, PlaylistMapped, pItem );
    else
      _playlistAddItemAfter( plst, NULL, PlaylistMapped, pItem );

    // Adjust list count
    plst->_numberOfItems ++;

  }  // next item from list

/*------------------------------------------------------------------------*\
    Invalidate cursor index and set timestamp
\*------------------------------------------------------------------------*/
  plst->_cursorPos = -1;
  plst->lastChange = srvtime();

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  CHKLIST( plst );
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
  CHKLIST( plst );

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
    jObj = json_object_get( jItem, "playbackQueuePos" );
    if( jObj && json_is_integer(jObj) ) {
      int           pos   = json_integer_value( jObj );
      PlaylistItem *pItem = playlistGetItem( plst, PlaylistMapped, pos );
      if( pItem && strcmp(id,pItem->id) ) {
        logwarn( "playlistDeleteItems (%p): item #%d id \"%s\" differs from \"%s\" at explicit position %d.",
                  plst, i, id, pItem->id, pos );
        rc = -1;
      }
      if( pItem ) { 
        _playlistUnlinkItem( plst, pItem, PlaylistBoth );

        // Adjust list length
        plst->_numberOfItems--;

        // Invalidate cursor index, adjust cursor if necessary
        plst->_cursorPos = -1;
        if( plst->_cursorItem==pItem )
          plst->_cursorItem = pItem->nextMapped;

        // Delete item
        playlistItemDelete( pItem );
      }
      else
        logwarn( "playlistDeleteItems (%p): item #%d with invalid explicit position %d.",
                  plst, i, pos );        
    }
      
    // Remove all items with this ID
    else for(;;) {
      PlaylistItem *pItem = playlistGetItemById( plst, id );
      if( !pItem )
        break;
      _playlistUnlinkItem( plst, pItem, PlaylistBoth );

      // Adjust list length
      plst->_numberOfItems--;

      // Invalidate cursor index, adjust cursor if necessary
      plst->_cursorPos = -1;
      if( plst->_cursorItem==pItem )
        plst->_cursorItem = pItem->nextMapped;

      // Delete item
      playlistItemDelete( pItem );
    }

  }  // next item from list

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  CHKLIST( plst );
  return rc;
}


/*=========================================================================*\
       Move tracks within playlist
         order - the list (original or mapping) that should be modified
         pos   - the position to add the tracks before (if <0 append to end)
\*=========================================================================*/
int playlistMoveItems( Playlist *plst, PlaylistSortType order, int pos, json_t *jItems )
{
  PlaylistItem **pItems;             // Array of items to move
  int            pItemCnt;           // Elements in pItems 
  PlaylistItem  *cItem;
  PlaylistItem  *anchorItem = NULL;  // Item to add list before
  int            i;
  int            rc = 0;

  DBGMSG( "playlistMoveItems (%p-%s): before:%d",
           plst, order==PlaylistOriginal ? "original" : "mapped", pos );
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Get anchor item (if any)
\*------------------------------------------------------------------------*/
  if( pos>=0 )
    anchorItem = playlistGetItem( plst, order, pos );

/*------------------------------------------------------------------------*\
    Allocate temporary array of items to move
\*------------------------------------------------------------------------*/
  pItemCnt = 0;
  pItems   = calloc( json_array_size(jItems), sizeof(PlaylistItem *) );
  if( !pItems ) {
    logerr( "playlistMoveItems: out of memory" );
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
    jObj = json_object_get( jItem, "playbackQueuePos" );
    if( !jObj || !json_is_integer(jObj) ) {
      logerr( "playlistMoveItems: item #%d lacks field \"playbackQueuePos\"", i );
      rc = -1;
      continue;
    } 
    pos   = json_integer_value( jObj );
      
    // Get item 
    pItem = playlistGetItem( plst, order, pos );
    if( !pItem ) {
      logwarn( "playlistMoveItems: item #%d has invalid explicit position %d.", i, pos );
      rc = -1;
      continue;
    }

    // Get item ID
    jObj = json_object_get( jItem, "id" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "playlistMoveItems: item #%d lacks field \"id\".", i );
      rc = -1;
      continue;
    }              
    id = json_string_value( jObj );
      
    // Be defensive
    if( strcmp(id,pItem->id) ) {
      logwarn( "playlistMoveItems: item #%d id \"%s\" differs from \"%s\" at explicit position %d.",
                i, id, pItem->id, pos );
      rc = -1;
    }

    // Be paranoid: check for doubles since unlinking is not stable against double calls
    bool found = 0;
    int j;
    for( j=0; j<pItemCnt && !found; j++ ) {
      found = ( pItems[j]==pItem );
    }
    if( found ) {
      logwarn( "playlistMoveItems: item #%d is double in list (previous instance: %d).", i, j );
      rc = -1;
      continue;
    }
      
    // add Item to temporary list 
    pItems[pItemCnt++] = pItem;
      
  } /* for( i=0; i<json_array_size(jItems); i++ ) */
    
/*------------------------------------------------------------------------*\
    Save item under cursor, since unlinking it would forward the cursor
\*------------------------------------------------------------------------*/
  cItem = plst->_cursorItem;

/*------------------------------------------------------------------------*\
    Unlink all identified items, but do not delete them
\*------------------------------------------------------------------------*/
  for( i=0; i<pItemCnt; i++ )
    _playlistUnlinkItem( plst, pItems[i], order );

/*------------------------------------------------------------------------*\
    Reinsert the items at new position (also invalidates cursor) and restore cursor item
\*------------------------------------------------------------------------*/
  for( i=0; i<pItemCnt; i++ ) {
    if( anchorItem )
      _playlistAddItemBefore( plst, anchorItem, order, pItems[i] );
    else 
      _playlistAddItemAfter( plst, NULL, order, pItems[i] );
  }
  plst->_cursorItem = cItem;
  
/*------------------------------------------------------------------------*\
    Free temporary list of items to move and return
\*------------------------------------------------------------------------*/
  Sfree( pItems );
  CHKLIST( plst );
  return rc;
}


/*=========================================================================*\
       Shuffle playlist (mapping)
         Shuffles range between startPos and endPos (included)
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
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Check end position
\*------------------------------------------------------------------------*/
  if( endPos>=plst->_numberOfItems ) {
    logerr( "playlistShuffle (%p): invalid end position %d/%d",
            plst, endPos, plst->_numberOfItems );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Get first element
\*------------------------------------------------------------------------*/
  item1 = playlistGetItem( plst, PlaylistMapped, startPos );
  if( !item1 ) {
    logerr( "playlistShuffle (%p): invalid start position %d", plst, startPos );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Swap with cursor if requested and increment to next element
\*------------------------------------------------------------------------*/
  if( moveCursorToStart ) {
    playlistTranspose( plst, item1, plst->_cursorItem );
    item1 = plst->_cursorItem->nextMapped;
    pos++;
  }

/*------------------------------------------------------------------------*\
    Shuffle: Select the first item from remaining set and step to next one
\*------------------------------------------------------------------------*/
  while( pos<endPos ) {
    int rnd = (int)rndInteger( pos, endPos );
    item2   = playlistGetItem( plst, PlaylistMapped, rnd );
    if( !item1 || !item2 ) {
      logerr( "playlistShuffle (%p): corrupt linked list @%d(%p)<->%d(%p)/%d",
               plst, pos, item1, rnd, item2, endPos );
      break;
    }
    DBGMSG( "playlistShuffle (%p): swap %d<->%d/%d ", plst, pos, rnd, endPos  );
    playlistTranspose( plst, item1, item2 );
    item1 = item2->nextMapped;
    pos++;
  }

/*------------------------------------------------------------------------*\
    Invalidate cursor position and return item under cursor
\*------------------------------------------------------------------------*/
  plst->_cursorPos  = -1;
  CHKLIST( plst );
  return plst->_cursorItem;
}


/*=========================================================================*\
       Transpose two items in the mapped list
          returns true, if items are different, false else
\*=========================================================================*/
bool playlistTranspose( Playlist *plst, PlaylistItem *pItem1, PlaylistItem *pItem2 )
{
  PlaylistItem *item;

  DBGMSG( "playlistTranspose (%p): %p <-> %p", plst, pItem1, pItem2 );
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Nothing to do?
\*------------------------------------------------------------------------*/
  if( pItem1==pItem2 )
    return 0;

/*------------------------------------------------------------------------*\
    Swap forward links and adjust backward pointers of following elements
\*------------------------------------------------------------------------*/
  item = pItem1->nextMapped;
  pItem1->nextMapped = pItem2->nextMapped;
  pItem2->nextMapped = item;
  if( pItem1->nextMapped )
    pItem1->nextMapped->prevMapped = pItem1;
  if( pItem2->nextMapped )
    pItem2->nextMapped->prevMapped = pItem2;

/*------------------------------------------------------------------------*\
    Swap backward links and adjust forward pointers of previous elements
\*------------------------------------------------------------------------*/
  item = pItem1->prevMapped;
  pItem1->prevMapped = pItem2->prevMapped;
  pItem2->prevMapped = item;
  if( pItem1->prevMapped )
    pItem1->prevMapped->nextMapped = pItem1;
  if( pItem2->prevMapped )
    pItem2->prevMapped->nextMapped = pItem2;

/*------------------------------------------------------------------------*\
    Adjust root pointers
\*------------------------------------------------------------------------*/
  if( !pItem2->prevMapped )
    plst->firstItemMapped = pItem2;
  else if( !pItem1->prevMapped )
    plst->firstItemMapped = pItem1;
  if( !pItem2->nextMapped )
    plst->lastItemMapped = pItem2;
  else if( !pItem1->nextMapped )
    plst->lastItemMapped = pItem1;

/*------------------------------------------------------------------------*\
    Invalidate cursor index if cursor is part of the exchange
\*------------------------------------------------------------------------*/
  if( plst->_cursorItem==pItem1 || plst->_cursorItem==pItem2  )
    plst->_cursorPos = -1;

/*------------------------------------------------------------------------*\
    Adjust timestamp and return
\*------------------------------------------------------------------------*/
  plst->lastChange = srvtime();
  CHKLIST( plst );
  return true;
}


/*=========================================================================*\
       Add an item to the list before a given anchor
          oder defines the list the item is added to
          if anchor is NULL, the item is added to the beginning of the list
       Note that the caller is responsible for synchronizing the lists and
       for adjusting the meta data (cursorPos and muberOfItems)
\*=========================================================================*/
static void _playlistAddItemBefore( Playlist *plst, PlaylistItem *anchorItem,
                                    PlaylistSortType order, PlaylistItem *newItem )
{
  DBGMSG( "_playlistAddItemBefore(%p-%s): anchor:%p new:%p",
           plst, order==PlaylistOriginal ? "original" : "mapped",
           anchorItem, newItem );

/*------------------------------------------------------------------------*\
    Defaults
\*------------------------------------------------------------------------*/
  if( !anchorItem && order==PlaylistOriginal)
    anchorItem = plst->firstItemOriginal;
  else if( !anchorItem )
    anchorItem = plst->firstItemMapped;

/*------------------------------------------------------------------------*\
    Process original list
\*------------------------------------------------------------------------*/
  if( order==PlaylistOriginal ) {

    // Default anchor is first item
    if( !anchorItem )
      anchorItem = plst->firstItemOriginal;

    // Link item to list
    newItem->nextOriginal = anchorItem;
    newItem->prevOriginal = NULL;
    if( anchorItem ) {
      newItem->prevOriginal = anchorItem->prevOriginal;
      if( anchorItem->prevOriginal )
        anchorItem->prevOriginal->nextOriginal = newItem;
      anchorItem->prevOriginal = newItem;
    }

    // Adjust root pointers
    if( !plst->firstItemOriginal || plst->firstItemOriginal==anchorItem )
      plst->firstItemOriginal = newItem;
    if( !plst->lastItemOriginal )
      plst->lastItemOriginal = newItem;
  }

/*------------------------------------------------------------------------*\
    Process mapped list
\*------------------------------------------------------------------------*/
  else {

    // Default anchor is first item
    if( !anchorItem )
      anchorItem = plst->firstItemMapped;

    // Link item to mapped list
    newItem->nextMapped = anchorItem;
    newItem->prevMapped = NULL;
    if( anchorItem ) {
      newItem->prevMapped = anchorItem->prevMapped;
      if( anchorItem->prevMapped )
        anchorItem->prevMapped->nextMapped = newItem;
      anchorItem->prevMapped = newItem;
    }

    // Adjust root pointers
    if( !plst->firstItemMapped || plst->firstItemMapped==anchorItem )
      plst->firstItemMapped = newItem;
    if( !plst->lastItemMapped )
      plst->lastItemMapped = newItem;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
       Add an item to the list after a given anchor
          oder defines the list the item is added to
          if anchor is NULL, the item is added to the end of the list
       Note that the caller is responsible for synchronizing the lists and
       for adjusting the meta data (cursorPos and muberOfItems)
\*=========================================================================*/
static void _playlistAddItemAfter( Playlist *plst, PlaylistItem *anchorItem,
                                    PlaylistSortType order, PlaylistItem *newItem )
{
  DBGMSG( "_playlistAddItemAfter(%p-%s): anchor:%p new:%p",
           plst, order==PlaylistOriginal ? "original" : "mapped",
           anchorItem, newItem );

 /*------------------------------------------------------------------------*\
     Process original list
 \*------------------------------------------------------------------------*/
  if( order==PlaylistOriginal ) {

    // Default anchor is last item
    if( !anchorItem )
      anchorItem = plst->lastItemOriginal;

    // Link item to original list
    newItem->nextOriginal = NULL;
    newItem->prevOriginal = anchorItem;
    if( anchorItem ) {
      newItem->nextOriginal = anchorItem->nextOriginal;
      if( anchorItem->nextOriginal )
        anchorItem->nextOriginal->prevOriginal = newItem;
      anchorItem->nextOriginal = newItem;
    }

    // Adjust root pointers
    if( !plst->firstItemOriginal )
      plst->firstItemOriginal = newItem;
    if( !plst->lastItemOriginal || plst->lastItemOriginal==anchorItem )
      plst->lastItemOriginal = newItem;
  }

/*------------------------------------------------------------------------*\
    Process mapped list
\*------------------------------------------------------------------------*/
  else {

    // Default anchor is last item
    if( !anchorItem )
      anchorItem = plst->lastItemMapped;

    // Link item to mapped list
    newItem->nextMapped = NULL;
    newItem->prevMapped = anchorItem;
    if( anchorItem ) {
      newItem->nextMapped = anchorItem->nextMapped;
      if( anchorItem->nextMapped )
        anchorItem->nextMapped->prevMapped = newItem;
      anchorItem->nextMapped = newItem;
    }

    // Adjust root pointers
    if( !plst->firstItemMapped )
      plst->firstItemMapped = newItem;
    if( !plst->lastItemMapped || plst->lastItemMapped==anchorItem )
      plst->lastItemMapped = newItem;
  }

  /*------------------------------------------------------------------------*\
      That's it
  \*------------------------------------------------------------------------*/
}


/*=========================================================================*\
       Unlink an item from list(s)
         order defines the list(s) - PlaylistOriginal, PlaylistMapped or PlaylistBoth
         does not check if item is actually a member of this playlist!
         item needs to be deleted afterwards
         links of item will not be changed and can be used after unlinking
         Cursor and playlist item count are not adjusted!
\*=========================================================================*/
static void _playlistUnlinkItem( Playlist *plst, PlaylistItem *pItem, PlaylistSortType order )
{
  DBGMSG( "_playlistUnlinkItem: %p", pItem );

/*------------------------------------------------------------------------*\
    Unlink item from original list
\*------------------------------------------------------------------------*/
  if( order==PlaylistOriginal || order==PlaylistBoth ) {
    if( pItem->nextOriginal )
      pItem->nextOriginal->prevOriginal = pItem->prevOriginal;
    if( pItem->prevOriginal )
      pItem->prevOriginal->nextOriginal = pItem->nextOriginal;
    if( pItem==plst->firstItemOriginal )
      plst->firstItemOriginal = pItem->nextOriginal;
    if( pItem==plst->lastItemOriginal )
      plst->lastItemOriginal = pItem->prevOriginal;
  }

/*------------------------------------------------------------------------*\
    Unlink item from mapped list
\*------------------------------------------------------------------------*/
  if( order==PlaylistMapped || order==PlaylistBoth ) {
    if( pItem->nextMapped )
      pItem->nextMapped->prevMapped = pItem->prevMapped;
    if( pItem->prevMapped )
      pItem->prevMapped->nextMapped = pItem->nextMapped;
    if( pItem==plst->firstItemMapped )
      plst->firstItemMapped = pItem->nextMapped;
    if( pItem==plst->lastItemMapped )
      plst->lastItemMapped = pItem->prevMapped;
  }
}


/*=========================================================================*\
       Create (allocate and init) an item from ickstream JSON object
          return NULL on error
\*=========================================================================*/
PlaylistItem *playlistItemFromJSON( json_t *jItem )
{
  PlaylistItem        *item;

/*------------------------------------------------------------------------*\
    Allocate header
\*------------------------------------------------------------------------*/
  item = calloc( 1, sizeof(PlaylistItem) );
  if( !item ) {
    logerr( "playlistItemFromJSON: out of memeory!" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Init mutex
\*------------------------------------------------------------------------*/
  ickMutexInit( &item->mutex );

/*------------------------------------------------------------------------*\
    Keep instance of JSON object
\*------------------------------------------------------------------------*/
  item->jItem = json_incref( jItem );
  // DBGMSG( "playlistItemFromJSON: JSON refCnt=%d", jItem->refcount );

/*------------------------------------------------------------------------*\
    Extract header info
\*------------------------------------------------------------------------*/
  if( _playlistItemFillHeader(item) ) {
    json_incref( jItem );
    Sfree( item );
    return NULL;
  }

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
       Lock playlist item
\*=========================================================================*/
void playlistItemLock( PlaylistItem *item )
{
  DBGMSG( "playlistItem(%p): lock", item );
  pthread_mutex_lock( &item->mutex );
}


/*=========================================================================*\
       Unlock playlist item
\*=========================================================================*/
void playlistItemUnlock( PlaylistItem *item )
{
  DBGMSG( "playlistItem(%p): unlock", item );
  pthread_mutex_unlock( &item->mutex );
}


/*=========================================================================*\
       Get next playlist item, respect ordering
\*=========================================================================*/
PlaylistItem *playlistItemGetNext( PlaylistItem *item, PlaylistSortType order )
{
  DBGMSG( "playlistItem(%p): getNext(%s)",
          item, order==PlaylistOriginal ? "original" : "mapped" );

/*------------------------------------------------------------------------*\
    Need valid input
\*------------------------------------------------------------------------*/
  if( !item ) {
    logerr( "playlistItemGetNext: called with NULL" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Select pointer
\*------------------------------------------------------------------------*/
  switch( order ) {
    case PlaylistOriginal:
      return item->nextOriginal;
    case PlaylistMapped:
      return item->nextMapped;
    case PlaylistHybrid:
    case PlaylistBoth:
      break;
  }

/*------------------------------------------------------------------------*\
    Unknown sorting
\*------------------------------------------------------------------------*/
  logerr( "playlistItemGetNext: called with unknown ordering %d", order );
  return NULL;
}


/*=========================================================================*\
       Get previous playlist item, respect ordering
\*=========================================================================*/
PlaylistItem *playlistItemGetPrevious( PlaylistItem *item, PlaylistSortType order )
{
  DBGMSG( "playlistItem(%p): getPrevious(%s)",
          item, order==PlaylistOriginal ? "original" : "mapped" );

/*------------------------------------------------------------------------*\
    Need valid input
\*------------------------------------------------------------------------*/
  if( !item ) {
    logerr( "playlistItemGetPrevious: called with NULL" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Select pointer
\*------------------------------------------------------------------------*/
  switch( order ) {
    case PlaylistOriginal:
      return item->prevOriginal;
    case PlaylistMapped:
      return item->prevMapped;
    case PlaylistHybrid:
    case PlaylistBoth:
      break;
  }

/*------------------------------------------------------------------------*\
    Unknown sorting
\*------------------------------------------------------------------------*/
  logerr( "playlistItemGetPrevious: called with unknown ordering %d", order );
  return NULL;
}


/*=========================================================================*\
       Get text for playlist item
\*=========================================================================*/
const char *playlistItemGetText( PlaylistItem *pItem )
{
  return pItem->text;
}


/*=========================================================================*\
       Get id of playlist item
\*=========================================================================*/
const char *playlistItemGetId( PlaylistItem *pItem )
{
  return pItem->id;
}


/*=========================================================================*\
       Get type of playlist item
\*=========================================================================*/
PlaylistItemType  playlistItemGetType( PlaylistItem *pItem )
{
  return pItem->type;
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
       Get an attribute
         return NULL if not available
\*=========================================================================*/
json_t *playlistItemGetAttribute( PlaylistItem *pItem, const char *attribute )
{
  DBGMSG( "playlistItemGetAttribute (%p,%s): \"%s\".",
           pItem, pItem->text, attribute );

  // Get an return attribute
  return json_object_get( pItem->jItem, attribute );
}


/*=========================================================================*\
       Get a model attribute
         return NULL if not available
\*=========================================================================*/
json_t *playlistItemGetModelAttribute( PlaylistItem *pItem, const char *attribute )
{
  json_t *jObj;

  DBGMSG( "playlistItemGetModelAttribute (%p,%s): \"%s\".",
           pItem, pItem->text, attribute );

  // Get attribute container
  jObj = json_object_get( pItem->jItem, "itemAttributes" );
  if( !jObj  ) {
    logwarn( "playlistItemGetModelAttribute (%p,%s): Field \"itemAttributes\" not found.",
              pItem, pItem->text );
    return NULL;
  }

  // Get an return attribute
  return json_object_get( jObj, attribute );
}


/*=========================================================================*\
       Get duration of an item (in seconds)
         return 0 if not available
\*=========================================================================*/
double playlistItemGetDuration( PlaylistItem *pItem )
{
  json_t *jObj;
  double  duration = 0;

/*------------------------------------------------------------------------*\
    Find and parse attribute
\*------------------------------------------------------------------------*/
  jObj = playlistItemGetModelAttribute( pItem, "duration" );
  if( json_getreal(jObj,&duration) )
      logwarn( "playlistItemGetDuration (%s): Cannot interpret attribute \"duration\".",
               pItem->text );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistItemGetDuration (%p,%s): %lfs.", pItem, pItem->text, duration );
  return duration;
}


/*=========================================================================*\
       Get image URI
         return NULL if not available
\*=========================================================================*/
const char *playlistItemGetImageUri( PlaylistItem *pItem )
{
  json_t *jObj;

  DBGMSG( "playlistItemGetImageUri (%p,%s).", pItem, pItem->text );

  jObj = json_object_get( pItem->jItem, "image" );
  if( jObj && json_is_string(jObj) )
     return json_string_value( jObj );

  if( jObj )
    logwarn( "playlistItemGetImageUri (%p,%s): Field \"image\" is not a string.",
             pItem, pItem->text );

  return NULL;
}


/*=========================================================================*\
       Manipulate meta data of the playlist item
\*=========================================================================*/
int playlistItemSetMetaData( PlaylistItem *pItem, json_t *metaObj, bool replace )
{
  DBGMSG( "playlistItemSetMetaData (%p,%s): %p replace:%s",
           pItem, pItem->text, metaObj, replace?"On":"Off" );

/*------------------------------------------------------------------------*\
    Replace mode: exchange object with new one
\*------------------------------------------------------------------------*/
  if( replace ) {
    json_decref( pItem->jItem );
    pItem->jItem = json_incref( metaObj );
  }

/*------------------------------------------------------------------------*\
    Merge mode: try to merge new Object into existing one
\*------------------------------------------------------------------------*/
  else if( json_object_merge(pItem->jItem,metaObj) ) {
    logerr( "playlistItemSetMetaData (%p,%s): %p could not merge.",
            pItem, pItem->text, metaObj );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Adjust header info
\*------------------------------------------------------------------------*/
  if( _playlistItemFillHeader(pItem) ) {
    logerr( "playlistItemSetMetaData (%p,%s): %p invalid header data",
            pItem, pItem->text, metaObj );
    return -1;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
       Extract some header info from playlist JSON item for fast access
\*=========================================================================*/
static int _playlistItemFillHeader( PlaylistItem *pItem )
{
  json_t *jObj;

/*------------------------------------------------------------------------*\
    Extract id for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( pItem->jItem, "id" );
  if( !jObj || !json_is_string(jObj) ) {
    logerr( "_playlistItemFillHeader: Missing field \"id\"!" );
    return -1;
  }
  pItem->id = json_string_value( jObj );

/*------------------------------------------------------------------------*\
    Extract text for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( pItem->jItem, "text" );
  if( !jObj || !json_is_string(jObj) ) {
    logerr( "_playlistItemFillHeader: Missing field \"text\"!" );
    return -1;
  }
  pItem->text = json_string_value( jObj );

/*------------------------------------------------------------------------*\
    Extract item type (Fixme: remove default to track)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( pItem->jItem, "type" );
  if( !jObj || !json_is_string(jObj) ) {
    logwarn( "_playlistItemFillHeader: Missing field \"type\"!" );
    pItem->type = PlaylistItemTrack;
  }
  else {
    const char *typeStr = json_string_value( jObj );
    if( !strcmp(typeStr,"track") )
      pItem->type = PlaylistItemTrack;
    else if( !strcmp(typeStr,"stream") )
      pItem->type = PlaylistItemStream;
    else {
      logerr( "_playlistItemFillHeader: Unknown type \"%s\"", typeStr );
      return -1;
    }
  }

/*------------------------------------------------------------------------*\
    Extract streaming data list for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( pItem->jItem, "streamingRefs" );
  if( !jObj || !json_is_array(jObj) ) {
    logerr( "_playlistItemFillHeader: Missing field \"streamingRefs\"!" );
    return -1;
  }
  pItem->jStreamingRefs = jObj;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
       Check consistency of playlist
\*=========================================================================*/
#ifdef CONSISTENCYCHECKING

static int _playlistCheckList( const char *file, int line, Playlist *plst )
{
  PlaylistItem *item,
               *last;
  int           i,
                rc = 0;

/*------------------------------------------------------------------------*\
    Loop over original list and check backward links
\*------------------------------------------------------------------------*/
  last = NULL;
  for( i=0,item=plst->firstItemOriginal; item; i++,item=item->nextOriginal ) {
    // DBGMSG( "item #%d: %p <%p,%p> (%s)", i, item, item->prevOriginal, item->nextOriginal, item->text );
    if( item->prevOriginal!=last ) {
      _mylog( file, line, LOG_ERR, "item #%d (%p, %s): prevOriginal %p corrupt (should be %p)",
              i, item, item->text, item->prevOriginal, last );
      rc = -1;
    }
    last = item;
  }

  // Check list length
  if( i!=plst->_numberOfItems ) {
    _mylog( file, line, LOG_ERR, "original forward linked list length (%d) does not match number of items (%d)",
            i, plst->_numberOfItems );
    rc = -1;
  }

  // Check terminating pointer
  if( last!=plst->lastItemOriginal ) {
    _mylog( file, line, LOG_ERR, "last original item #%d (%p) does not match last pointer (%p)",
            i-1, last, plst->lastItemOriginal );
    rc = -1;
  }


/*------------------------------------------------------------------------*\
    Loop over original list and check forward links
\*------------------------------------------------------------------------*/
  last = NULL;
  for( i=0,item=plst->lastItemOriginal; item; i++,item=item->prevOriginal ) {
    // DBGMSG( "item #%d: %p <%p,%p> (%s)", i, item, item->prevOriginal, item->nextOriginal, item->text );
    if( item->nextOriginal!=last ) {
      _mylog( file, line, LOG_ERR, "item #%d (%p, %s): nextOriginal %p corrupt (should be %p)",
          plst->_numberOfItems-i-1, item, item->text, item->nextOriginal, last );
      rc = -1;
    }
    last = item;
  }

  // Check list length
  if( i!=plst->_numberOfItems ) {
    _mylog( file, line, LOG_ERR, "original backward linked list length (%d) does not match number of items (%d)",
            i, plst->_numberOfItems );
    rc = -1;
  }

  // Check terminating pointer
  if( last!=plst->firstItemOriginal ) {
    _mylog( file, line, LOG_ERR, "first original item #%d (%p) does not match first pointer (%p)",
            i, last, plst->firstItemOriginal );
    rc = -1;
  }


/*------------------------------------------------------------------------*\
    Loop over mapped list and check backward links
\*------------------------------------------------------------------------*/
  last = NULL;
  for( i=0,item=plst->firstItemMapped; item; i++,item=item->nextMapped ) {
    // DBGMSG( "item #%d: %p <%p,%p> (%s)", i, item, item->prevOriginal, item->nextOriginal, item->text );
    if( item->prevMapped!=last ) {
      _mylog( file, line, LOG_ERR, "item #%d (%p, %s): prevpointer %p corrupt (should be %p)",
              i, item, item->text, item->prevMapped, last );
      rc = -1;
    }
    last = item;
  }

  // Check list length
  if( i!=plst->_numberOfItems ) {
    _mylog( file, line, LOG_ERR, "mapped forward linked list length (%d) does not match number of items (%d)",
            i, plst->_numberOfItems );
    rc = -1;
  }

  // Check terminating pointer
  if( last!=plst->lastItemMapped ) {
    _mylog( file, line, LOG_ERR, "last mapped item #%d (%p) does not match last pointer (%p)",
            i-1, last, plst->lastItemMapped );
    rc = -1;
  }


/*------------------------------------------------------------------------*\
    Loop over mapped list and check forward links
\*------------------------------------------------------------------------*/
  last = NULL;
  for( i=0,item=plst->lastItemMapped; item; i++,item=item->prevMapped ) {
    // DBGMSG( "item #%d: %p <%p,%p> (%s)", i, item, item->prevOriginal, item->nextOriginal, item->text );
    if( item->nextMapped!=last ) {
      _mylog( file, line, LOG_ERR, "item #%d (%p, %s): nextpointer %p corrupt (should be %p)",
          plst->_numberOfItems-i-1, item, item->text, item->nextMapped, last );
      rc = -1;
    }
    last = item;
  }

  // Check list length
  if( i!=plst->_numberOfItems  ) {
    _mylog( file, line, LOG_ERR, "mapped backward linked list length (%d) does not match number of items (%d)",
            i, plst->_numberOfItems );
    rc = -1;
  }

  // Check terminating pointer
  if( last!=plst->firstItemMapped ) {
    _mylog( file, line, LOG_ERR, "first mapped item #%d (%p) does not match first pointer (%p)",
            i, last, plst->firstItemMapped );
    rc = -1;
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

