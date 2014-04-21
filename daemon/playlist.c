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
  int                   refCounter;
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
//static void _playlistAddItemAfter( Playlist *plst, PlaylistItem *anchorItem, PlaylistSortType order, PlaylistItem *newItem );
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
  if( !plst ) {
    logerr( "playlistNew: out of memory!" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Init header fields
\*------------------------------------------------------------------------*/
  // plst->id   = strdup( "<undefined>" );
  // plst->name = strdup( "ickpd player queue" );
 
/*------------------------------------------------------------------------*\
    Init mutex and set timestamp
\*------------------------------------------------------------------------*/
  ickMutexInit( &plst->mutex );
  plst->lastChange = srvtime();

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
    No json data?
\*------------------------------------------------------------------------*/
  if( !jQueue )
    return plst;
    
/*------------------------------------------------------------------------*\
    Create playlist header from JSON
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
  if( !jObj )
    logerr( "Playlist: missing field \"items\"" );
  else if( !json_is_array(jObj) )
    logerr( "Playlist: field \"items\" is no array." );
  else {
  
    // Loop over all items and add them to list
    for( i=0; i<json_array_size(jObj); i++ ) {
      json_t       *jItem = json_array_get( jObj, i );
      PlaylistItem *pItem = playlistItemFromJSON( jItem );
      if( pItem )
        _playlistAddItemBefore( plst, NULL, PlaylistOriginal, pItem );
      else
        logerr( "Playlist: could not parse playlist item #%d", i+1 );
    } 

    // Store number of items
    plst->_numberOfItems = json_array_size(jObj);
  }

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
    playlistResetMapping( plst, false );
  }
  else for( i=0; i<json_array_size(jObj); i++ ) {
    json_t       *jPos = json_array_get( jObj, i );
    PlaylistItem *pItem;
    if( !json_is_integer(jPos) ) {
      logwarn( "Playlist: mapping item #%d is not an integer, using id mapping" );
      playlistResetMapping( plst, false );
      break;
    }
    pItem = playlistGetItem( plst, PlaylistOriginal, json_integer_value(jPos) );
    if( !pItem ) {
      logerr( "Playlist: could not find mapped item #%d (position %s), using id mapping",
                    i+1, json_integer_value(jPos) );
      playlistResetMapping( plst, false );
      break;
    }
    _playlistAddItemBefore( plst, NULL, PlaylistMapped, pItem );
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
  int perr;
  DBGMSG( "playlistDelete: %p", plst );
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  perr = pthread_mutex_lock( &plst->mutex );
  if( perr )
    logerr( "playlistDelete: locking list mutex: %s", strerror(perr) );

/*------------------------------------------------------------------------*\
    Free all items and reset header
\*------------------------------------------------------------------------*/
  playlistReset( plst, true );
  
/*------------------------------------------------------------------------*\
    Destroy mutex and free header
\*------------------------------------------------------------------------*/
  perr = pthread_mutex_destroy( &plst->mutex );
  if( perr )
    logerr( "playlistDelete: destroxing list mutex: %s", strerror(perr) );

  Sfree( plst );
}


/*=========================================================================*\
       Lock playlist
\*=========================================================================*/
void playlistLock( Playlist *plst )
{
  int perr;
  DBGMSG( "playlist (%p): lock", plst );
  perr = pthread_mutex_lock( &plst->mutex );
  if( perr )
    logerr( "playlistLock: %s", strerror(perr) );

}

/*=========================================================================*\
       Unlock playlist
\*=========================================================================*/
void playlistUnlock( Playlist *plst )
{
  int perr;
  DBGMSG( "playlist (%p): unlock", plst );
  perr = pthread_mutex_unlock( &plst->mutex );
  if( perr )
    logerr( "playlistUnlock: %s", strerror(perr) );
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
    Unreference all items
\*------------------------------------------------------------------------*/
  for( item=plst->firstItemOriginal; item; item=next ) {
    next = item->nextOriginal;
    item->nextMapped   = NULL;
    item->prevMapped   = NULL;
    item->nextOriginal = NULL;
    item->prevOriginal = NULL;
    playlistItemDecRef( item );
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
    Set timestamp, that's all
\*------------------------------------------------------------------------*/
  plst->lastChange = srvtime();
}


/*=========================================================================*\
       Init or reset mapping to id
         if inverse is set, the mapping is transfered to the original list
\*=========================================================================*/
void playlistResetMapping( Playlist *plst, bool inverse )
{
  PlaylistItem *walk;

  DBGMSG( "playlistResetMapping (%p): inverse: %s", plst, inverse?"Yes":"No" );

/*------------------------------------------------------------------------*\
    Transcribe original index to mapped index
\*------------------------------------------------------------------------*/
  if( !inverse ) {

    // Reset root pointers
    plst->firstItemMapped = plst->firstItemOriginal;
    plst->lastItemMapped  = plst->lastItemOriginal;

    // Copy pointers off all elements
    for( walk=plst->firstItemOriginal; walk; walk=walk->nextOriginal ) {
      walk->nextMapped = walk->nextOriginal;
      walk->prevMapped = walk->prevOriginal;
    }

    // Invalidate cursor index
    plst->_cursorPos = -1;
  }

/*------------------------------------------------------------------------*\
    Transcribe mapped index to original index
\*------------------------------------------------------------------------*/
  else {

    // Reset root pointers
    plst->firstItemOriginal = plst->firstItemMapped;
    plst->lastItemOriginal  = plst->lastItemMapped;

    // Copy pointers off all elements
    for( walk=plst->firstItemMapped; walk; walk=walk->nextMapped ) {
      walk->nextOriginal = walk->nextMapped;
      walk->prevOriginal = walk->prevMapped;
    }

  }

/*------------------------------------------------------------------------*\
    Set timestamp
\*------------------------------------------------------------------------*/
  plst->lastChange = srvtime();

/*------------------------------------------------------------------------*\
    That's all - check list consistency
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
       Set name for playlist
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
       Get name for playlist
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
    If found: store at pointer and store index
\*------------------------------------------------------------------------*/
  if( item ) {
    plst->_cursorItem = item;
    plst->_cursorPos  = pos;
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
         return new item or NULL (empty list or end of list)
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
         count is the (max.) number of elements included (0 means all)
\*=========================================================================*/
json_t *playlistGetJSON( Playlist *plst, PlaylistSortType order, int offset, int count )
{
  json_t       *jResult  = json_array();
  json_t       *jMapping = NULL;
  PlaylistItem *pItem;
  int           i;

  DBGMSG( "playlistGetJSON (%p): order=%s offset=%d count=%d",
           plst, playlistSortTypeToStr(order), offset, count );
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Is the requested sort mode supported?
\*------------------------------------------------------------------------*/
  if( order!=PlaylistOriginal && order!=PlaylistMapped && order!=PlaylistHybrid ) {
    logerr( "playlistGetJSON: unsupported sort mode %s, %d.",
             playlistSortTypeToStr(order), order );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    A zero or negative count argument means all items
\*------------------------------------------------------------------------*/
  if( count<=0 )
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
    pItem = playlistItemGetNext( pItem, order );
  }

/*------------------------------------------------------------------------*\
    Collect mapping
\*------------------------------------------------------------------------*/
  if( jMapping ) {
    pItem = playlistGetItem( plst, PlaylistOriginal, offset );
    for( i=count; pItem && i; i-- ) {
      int pos = playlistGetItemPos( plst, PlaylistMapped, pItem );
      json_array_append_new( jMapping, json_integer(pos) );  // steal reference
      pItem = playlistItemGetNext( pItem, PlaylistOriginal );
    }
  }

/*------------------------------------------------------------------------*\
    Build header
\*------------------------------------------------------------------------*/
  jResult = json_pack( "{ss sf si si si ss so}",
                         "jsonrpc",       "2.0",
                         "lastChanged",   plst->lastChange,
                         "count",         json_array_size(jResult),
                         "countAll",      playlistGetLength(plst),
                         "offset",        offset,
                         "order",         playlistSortTypeToStr(order),
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
  // DBGMSG( "playlistGetJSON(%p): result %p", plst, jResult );
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
  PlaylistItem *item = NULL;

#ifdef ICK_DEBUG
  int posbuf = pos;
#endif
  //CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Which mode?
\*------------------------------------------------------------------------*/
  switch( order ) {
    case PlaylistOriginal:
      item = plst->firstItemOriginal;
      break;
    case PlaylistMapped:
      item = plst->firstItemMapped;
      break;
    default:
      logerr( "playlistGetItem: unsupported sort mode %s, %d.",
               playlistSortTypeToStr(order), order );
      break;
  }

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
          plst, playlistSortTypeToStr(order), posbuf, item, GETITEMTXT(item) );
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
  PlaylistItem *walk = NULL;
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Which mode?
\*------------------------------------------------------------------------*/
  switch( order ) {
    case PlaylistOriginal:
      walk = plst->firstItemOriginal;
      break;
    case PlaylistMapped:
      walk = plst->firstItemMapped;
      break;
    default:
      logerr( "playlistGetItem: unsupported sort mode %s, %d.",
               playlistSortTypeToStr(order), order );
      break;
  }

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
  DBGMSG( "playlistGetItemPos (%p): order=%s item=%p (%s) -> %d ",
          plst, playlistSortTypeToStr(order),
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
  PlaylistItem *oAnchor = NULL;  // Item in original list to add list before
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
    return 0;

/*------------------------------------------------------------------------*\
    Get anchor items (if any and not in reset mode)
\*------------------------------------------------------------------------*/
  if( oPos>=0 && !resetFlag ) {
    oAnchor = playlistGetItem( plst, PlaylistOriginal, oPos );
    if( !oAnchor )
      logwarn( "playlistAddItems: original position %d out of bounds (%d)",
                oPos, plst->_numberOfItems );
  }
  if( mPos>=0 && !resetFlag ) {
    mAnchor = playlistGetItem( plst, PlaylistMapped, mPos );
    if( !mAnchor )
      logwarn( "playlistAddItems: mapped position %d out of bounds (%d)",
                mPos, plst->_numberOfItems );
  }

/*------------------------------------------------------------------------*\
    Loop over all items to add
\*------------------------------------------------------------------------*/
  for( i=0; i<json_array_size(jItems); i++ ) {
    json_t  *jItem = json_array_get( jItems, i );

    // Create a playlist item from json payload
    PlaylistItem *pItem = playlistItemFromJSON( jItem );
    if( !pItem ) {
      logerr( "playlistAddItems: could not parse item #%d.", i );
      rc = -1;
      continue;
    }

    // Add new item to original list before anchor if given or to end otherwise
    _playlistAddItemBefore( plst, oAnchor, PlaylistOriginal, pItem );

    // Add new item to mapped list before anchor if given or to end otherwise
    _playlistAddItemBefore( plst, mAnchor, PlaylistMapped, pItem );

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
      logerr( "playlistDeleteItems: item #%d - field \"id\" missing or of wrong type", i );
      rc = -1;
      continue;
    }              
    id = json_string_value( jObj );

/*------------------------------------------------------------------------*\
    Get item by explicit position
\*------------------------------------------------------------------------*/
    jObj = json_object_get( jItem, "playbackQueuePos" );
    if( jObj && json_is_integer(jObj) ) {
      int           pos   = json_integer_value( jObj );
      PlaylistItem *pItem = playlistGetItem( plst, PlaylistMapped, pos );
      if( pItem && strcmp(id,pItem->id) ) {
        logwarn( "playlistDeleteItems: item #%d id \"%s\" differs from \"%s\" at explicit position %d.",
                  i, id, pItem->id, pos );
        rc = -1;
        continue;
      }
      if( !pItem ) {
        logwarn( "playlistDeleteItems: item #%d with invalid explicit position %d.",
                  plst, i, pos );
        continue;
      }

      // Unlink item from both lists
      _playlistUnlinkItem( plst, pItem, PlaylistBoth );

      // Adjust list length and invalidate cursor index, adjust cursor if necessary
      plst->_numberOfItems--;
      plst->_cursorPos = -1;
      if( plst->_cursorItem==pItem )
        plst->_cursorItem = pItem->nextMapped ? pItem->nextMapped : pItem->prevMapped;

      // Finally unreference the item
      pItem->nextMapped   = NULL;
      pItem->prevMapped   = NULL;
      pItem->nextOriginal = NULL;
      pItem->prevOriginal = NULL;
      playlistItemDecRef( pItem );
    }

/*------------------------------------------------------------------------*\
    Remove all items with this ID
\*------------------------------------------------------------------------*/
    else for(;;) {
      PlaylistItem *pItem = playlistGetItemById( plst, id );
      if( !pItem )
        break;

      // Unlink item from both lists
      _playlistUnlinkItem( plst, pItem, PlaylistBoth );

      // Adjust list length and invalidate cursor index, adjust cursor if necessary
      plst->_numberOfItems--;
      plst->_cursorPos = -1;
      if( plst->_cursorItem==pItem )
        plst->_cursorItem = pItem->nextMapped ? pItem->nextMapped : pItem->prevMapped;

      // Finally unreference the item
      pItem->nextMapped   = NULL;
      pItem->prevMapped   = NULL;
      pItem->nextOriginal = NULL;
      pItem->prevOriginal = NULL;
      playlistItemDecRef( pItem );
    }

  }  // next item from list

/*------------------------------------------------------------------------*\
    Set timestamp, that's it
\*------------------------------------------------------------------------*/
  plst->lastChange = srvtime();
  CHKLIST( plst );
  return rc;
}


/*=========================================================================*\
       Move tracks within playlist
         order  - the list (original or mapping) that should be modified
         pos    - the position to add the tracks before (if <0 append to end)
         jItems - list of items to move
       returns 0 on success, -1 otherwise
       Note: if order is ORIGINAL, a resetMapping has to be executed
             afterwards by the caller to synch the queue mapping
\*=========================================================================*/
int playlistMoveItems( Playlist *plst, PlaylistSortType order, int pos, json_t *jItems )
{
  PlaylistItem **pItems;             // Array of items to move
  int            pItemCnt;           // Elements in pItems 
  PlaylistItem  *cItem;
  PlaylistItem  *anchorItem = NULL;  // Item to add list before
  int            i;
  int            rc = 0;

  DBGMSG( "playlistMoveItems (%p): order=%s before=%d",
           plst, playlistSortTypeToStr(order), pos );
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Which mode?
\*------------------------------------------------------------------------*/
  switch( order ) {
    case PlaylistOriginal:
    case PlaylistMapped:
      break;
    default:
      logerr( "playlistMoveItems: unsupported sort mode %s, %d.",
               playlistSortTypeToStr(order), order );
      return -1;
  }

/*------------------------------------------------------------------------*\
    Nothing to do?
\*------------------------------------------------------------------------*/
  if( !jItems || !json_array_size(jItems) )
    return 0;

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
      logerr( "playlistMoveItems: item #%d - field \"playbackQueuePos\" missing or of wrong type", i );
      rc = -1;
      continue;
    } 
    pos   = json_integer_value( jObj );
      
    // Get item 
    pItem = playlistGetItem( plst, order, pos );
    if( !pItem ) {
      logwarn( "playlistMoveItems: item #%d has invalid explicit position %d", i, pos );
      rc = -1;
      continue;
    }

    // Get item ID
    jObj = json_object_get( jItem, "id" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "playlistMoveItems: item #%d - field \"id\" missing or of wrong type", i );
      rc = -1;
      continue;
    }              
    id = json_string_value( jObj );
      
    // Be defensive
    if( strcmp(id,pItem->id) ) {
      logwarn( "playlistMoveItems: item #%d id \"%s\" differs from \"%s\" at explicit position %d.",
                i, id, pItem->id, pos );
      rc = -1;
      continue;
    }

    // Be paranoid: check for doubles since unlinking is not stable against double calls
    int j = 0;
    while( j<pItemCnt && pItems[j]!=pItem )
      j++;
    if( j!=pItemCnt ) {
      logwarn( "playlistMoveItems: item #%d is double in list (previous instance: %d).", i, j );
      rc = -1;
      continue;
    }
      
    // Add item to temporary list
    pItems[pItemCnt++] = pItem;
      
  } /* for( i=0; i<json_array_size(jItems); i++ ) */
    
/*------------------------------------------------------------------------*\
    Save item under cursor, since unlinking it will change the cursor
\*------------------------------------------------------------------------*/
  cItem = plst->_cursorItem;

/*------------------------------------------------------------------------*\
    Unlink all identified items in requested index, but do not delete them
\*------------------------------------------------------------------------*/
  for( i=0; i<pItemCnt; i++ )
    _playlistUnlinkItem( plst, pItems[i], order );

/*------------------------------------------------------------------------*\
    Reinsert the items at new position
\*------------------------------------------------------------------------*/
  for( i=0; i<pItemCnt; i++ )
    _playlistAddItemBefore( plst, anchorItem, order, pItems[i] );

/*------------------------------------------------------------------------*\
    Invalidate cursor and restore cursor item
\*------------------------------------------------------------------------*/
  plst->_cursorPos  = -1;
  plst->_cursorItem = cItem;

/*------------------------------------------------------------------------*\
    Set timestamp, free temporary list of items to move and return
\*------------------------------------------------------------------------*/
  plst->lastChange = srvtime();
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
           plst, startPos, endPos, plst->_numberOfItems,
           moveCursorToStart?"Yes":"No"  );
  CHKLIST( plst );

/*------------------------------------------------------------------------*\
    Check end position
\*------------------------------------------------------------------------*/
  if( endPos>=plst->_numberOfItems ) {
    logerr( "playlistShuffle: invalid end position %d/%d",
            endPos, plst->_numberOfItems );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Be defensive...
\*------------------------------------------------------------------------*/
  if( !plst->_cursorItem )
    plst->_cursorItem = plst->firstItemMapped;

/*------------------------------------------------------------------------*\
    Get first element
\*------------------------------------------------------------------------*/
  item1 = playlistGetItem( plst, PlaylistMapped, startPos );
  if( !item1 ) {
    logerr( "playlistShuffle: invalid start position %d", startPos );
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
      logerr( "playlistShuffle: corrupt linked list @%d<->%d/%d",
               pos, rnd, endPos );
      break;
    }
    DBGMSG( "playlistShuffle (%p): swap %d<->%d/%d ", plst, pos, rnd, endPos  );

    // This invalidates the cursor position and sets the timestamp
    playlistTranspose( plst, item1, item2 );
    item1 = item2->nextMapped;
    pos++;
  }

/*------------------------------------------------------------------------*\
    Return item under cursor
\*------------------------------------------------------------------------*/
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
    return false;

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
      Get an (ickstream protocol) string from sort type
\*=========================================================================*/
const char *playlistSortTypeToStr( PlaylistSortType order )
{
  DBGMSG( "playlistSortTypeToStr: order=%d", order );

  // Translate
  switch( order ) {
    case PlaylistOriginal: return "ORIGINAL";
    case PlaylistMapped:   return "CURRENT";
    case PlaylistHybrid:   return "ORIGINAL_MAPPED";
    case PlaylistBoth:     return "Both - internal";
  }

  // Not known or unsupported
  logerr( "Playlist order type %d unknown.");
  return "Unknown";
}


/*=========================================================================*\
      Get playlist sort type form ickstream protocol string
\*=========================================================================*/
PlaylistSortType playlistSortTypeFromStr( const char *str )
{
  DBGMSG( "playlistSortTypeFromStr: \"%s\"", str );

  // Translate
  if( !strcmp(str,"ORIGINAL") )
    return PlaylistOriginal;
  if( !strcmp(str,"CURRENT") )
    return PlaylistMapped;
  if( !strcmp(str,"ORIGINAL_MAPPED") )
    return PlaylistHybrid;

  // Not known or unsupported
  return -1;
}


/*=========================================================================*\
       Add an item to the list before a given anchor
          oder defines the list the item is added to
          if anchor is NULL, the item is added to the end of the list
       Note that the caller is responsible for synchronizing the lists and
       for adjusting the meta data (cursorPos and muberOfItems)
\*=========================================================================*/
static void _playlistAddItemBefore( Playlist *plst, PlaylistItem *anchorItem,
                                    PlaylistSortType order, PlaylistItem *newItem )
{
  DBGMSG( "_playlistAddItemBefore (%p): order=%s anchor=%p new=%p",
           plst, playlistSortTypeToStr(order), anchorItem, newItem );

/*------------------------------------------------------------------------*\
    Which mode?
\*------------------------------------------------------------------------*/
  switch( order ) {
    default:
      logerr( "_playlistAddItemBefore: unsupported sort mode %s, %d.",
               playlistSortTypeToStr(order), order );
      return;

/*------------------------------------------------------------------------*\
    Process original list
\*------------------------------------------------------------------------*/
    case PlaylistOriginal:

      // Link item to list
      newItem->nextOriginal = anchorItem;
      newItem->prevOriginal = NULL;
      if( anchorItem ) {
        newItem->prevOriginal    = anchorItem->prevOriginal;
        anchorItem->prevOriginal = newItem;
      }
      else
        newItem->prevOriginal = plst->lastItemOriginal;
      if( newItem->prevOriginal )
        newItem->prevOriginal->nextOriginal = newItem;

      // Adjust root pointers
      if( !plst->firstItemOriginal || plst->firstItemOriginal==anchorItem )
        plst->firstItemOriginal = newItem;
      if( !plst->lastItemOriginal || !anchorItem )
        plst->lastItemOriginal = newItem;

      break;

/*------------------------------------------------------------------------*\
    Process mapped list
\*------------------------------------------------------------------------*/
    case PlaylistMapped:

      // Link item to mapped list
      newItem->nextMapped = anchorItem;
      newItem->prevMapped = NULL;
      if( anchorItem ) {
        newItem->prevMapped    = anchorItem->prevMapped;
        anchorItem->prevMapped = newItem;
      }
      else
        newItem->prevMapped = plst->lastItemMapped;
      if( newItem->prevMapped )
        newItem->prevMapped->nextMapped = newItem;

      // Adjust root pointers
      if( !plst->firstItemMapped || plst->firstItemMapped==anchorItem )
        plst->firstItemMapped = newItem;
      if( !plst->lastItemMapped || !anchorItem )
        plst->lastItemMapped = newItem;
      break;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
       Add an item to the list after a given anchor
          oder defines the list the item is added to
          if anchor is NULL, the item is added to the beginning of the list
       Note that the caller is responsible for synchronizing the lists and
       for adjusting the meta data (cursorPos and muberOfItems)
\*=========================================================================*/
#if 0
static void _playlistAddItemAfter( Playlist *plst, PlaylistItem *anchorItem,
                                    PlaylistSortType order, PlaylistItem *newItem )
{
  DBGMSG( "_playlistAddItemAfter (%p): order=%s anchor=%p new=%p",
           plst, playlistSortTypeToStr(order), anchorItem, newItem );

/*------------------------------------------------------------------------*\
    Which mode?
\*------------------------------------------------------------------------*/
  switch( order ) {
    default:
      logerr( "_playlistAddItemAfter: unsupported sort mode %s, %d.",
               playlistSortTypeToStr(order), order );
      return;

/*------------------------------------------------------------------------*\
    Process original list
\*------------------------------------------------------------------------*/
    case PlaylistOriginal:

      // Link item to original list
      newItem->nextOriginal = NULL;
      newItem->prevOriginal = anchorItem;
      if( anchorItem ) {
        newItem->nextOriginal = anchorItem->nextOriginal;
        anchorItem->nextOriginal = newItem;
      }
      else
        newItem->nextOriginal = plst->firstItemOriginal;
      if( newItem->nextOriginal )
        newItem->nextOriginal->prevOriginal = newItem;

      // Adjust root pointers
      if( !plst->firstItemOriginal || !anchorItem )
        plst->firstItemOriginal = newItem;
      if( !plst->lastItemOriginal || plst->lastItemOriginal==anchorItem )
        plst->lastItemOriginal = newItem;
      break;

/*------------------------------------------------------------------------*\
    Process mapped list
\*------------------------------------------------------------------------*/
    case PlaylistMapped:

      // Link item to mapped list
      newItem->nextMapped = NULL;
      newItem->prevMapped = anchorItem;
      if( anchorItem ) {
        newItem->nextMapped = anchorItem->nextMapped;
        anchorItem->nextMapped = newItem;
      }
      else
        newItem->nextMapped = plst->firstItemMapped;
      if( newItem->nextMapped )
        newItem->nextMapped->prevMapped = newItem;

      // Adjust root pointers
      if( !plst->firstItemMapped || !anchorItem )
        plst->firstItemMapped = newItem;
      if( !plst->lastItemMapped || plst->lastItemMapped==anchorItem )
        plst->lastItemMapped = newItem;
      break;
  }

  /*------------------------------------------------------------------------*\
      That's it
  \*------------------------------------------------------------------------*/
}
#endif

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
  DBGMSG( "_playlistUnlinkItem (%p): order=%s item=%p",
           plst, playlistSortTypeToStr(order), pItem );

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


/***************************************************************************\
 * Functions operating on playlist items
\***************************************************************************/


/*=========================================================================*\
       Create (allocate and init) an item from ickstream JSON object
          reference counter is set to one
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
    Init mutex and reference counter
\*------------------------------------------------------------------------*/
  ickMutexInit( &item->mutex );
  item->refCounter = 1;

/*------------------------------------------------------------------------*\
    Keep instance of JSON object
\*------------------------------------------------------------------------*/
  item->jItem = json_incref( jItem );
  // DBGMSG( "playlistItemFromJSON: JSON refCnt=%d", jItem->refcount );

/*------------------------------------------------------------------------*\
    Extract header info
\*------------------------------------------------------------------------*/
  if( _playlistItemFillHeader(item) ) {
    json_decref( jItem );
    pthread_mutex_destroy( &item->mutex );
    Sfree( item );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  DBGMSG( "playlistItemFromJSON: %p \"%s\" (id=%s)",
                     item, item->text, item->id );
  return item;
}


/*=========================================================================*\
  Increment reference counter of playlist item
\*=========================================================================*/
void playlistItemIncRef( PlaylistItem *pItem )
{
  pItem->refCounter++;
  DBGMSG( "playlistItemIncRef (%p): now %d references (jrefcnt=%d)",
          pItem, pItem->refCounter, pItem->jItem->refcount );
}


/*=========================================================================*\
  Increment reference counter of playlist item (needs to be unlinked before!!!)
\*=========================================================================*/
void playlistItemDecRef( PlaylistItem *pItem )
{
  pItem->refCounter--;
  DBGMSG( "playlistItemDecRef (%p): now %d references (jrefcnt=%d)",
          pItem, pItem->refCounter, pItem->jItem->refcount );

  /* {
    char *out = json_dumps( pItem->jItem, JSON_PRESERVE_ORDER | JSON_COMPACT | JSON_ENSURE_ASCII );
    DBGMSG( "playlistItemDecRef: %s", out );
    free( out );
  } */

/*------------------------------------------------------------------------*\
    Still referenced?
\*------------------------------------------------------------------------*/
  if( pItem->refCounter>0 )
    return;

/*------------------------------------------------------------------------*\
    Free all header features
\*------------------------------------------------------------------------*/
  json_decref( pItem->jItem );
  pthread_mutex_destroy( &pItem->mutex );

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
  int perr;
  DBGMSG( "playlistItem (%p): lock", item );
  perr = pthread_mutex_lock( &item->mutex );
  if( perr )
    logerr( "playlistItemLock: %s", strerror(perr) );

}


/*=========================================================================*\
       Unlock playlist item
\*=========================================================================*/
void playlistItemUnlock( PlaylistItem *item )
{
  int perr;
  DBGMSG( "playlistItem (%p): unlock", item );
  perr = pthread_mutex_unlock( &item->mutex );
  if( perr )
    logerr( "playlistItemUnlock: %s", strerror(perr) );
}


/*=========================================================================*\
       Get next playlist item, respect ordering
\*=========================================================================*/
PlaylistItem *playlistItemGetNext( PlaylistItem *item, PlaylistSortType order )
{
  DBGMSG( "playlistItem (%p): getNext (%s)", item, playlistSortTypeToStr(order) );

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
    default:
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
  DBGMSG( "playlistItem (%p): getPrevious (%s)", item,  playlistSortTypeToStr(order) );

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
    default:
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
    logwarn( "playlistItemGetModelAttribute (%s): Field \"itemAttributes\" not found.",
              pItem->text );
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
    logwarn( "playlistItemGetImageUri (%s): Field \"image\" is not a string.",
             pItem->text );

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
    logerr( "playlistItemSetMetaData (%s): could not merge new meta data.",
            pItem->text );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Adjust header info
\*------------------------------------------------------------------------*/
  if( _playlistItemFillHeader(pItem) ) {
    logerr( "playlistItemSetMetaData (%s): invalid header of new meta data.",
            pItem->text );
    return -1;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
       Extract some header info from playlist JSON item for fast access
         return 0 on success, -1 on error
\*=========================================================================*/
static int _playlistItemFillHeader( PlaylistItem *pItem )
{
  json_t     *jObj;
  const char *typeStr;

/*------------------------------------------------------------------------*\
    Extract id for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( pItem->jItem, "id" );
  if( !jObj || !json_is_string(jObj) ) {
    logerr( "_playlistItemFillHeader: Field \"id\" missing or of wrong type." );
    return -1;
  }
  pItem->id = json_string_value( jObj );

/*------------------------------------------------------------------------*\
    Extract text for quick access (weak ref.)
\*------------------------------------------------------------------------*/
  jObj = json_object_get( pItem->jItem, "text" );
  if( !jObj || !json_is_string(jObj) ) {
    logerr( "_playlistItemFillHeader: Field \"text\" missing or of wrong type." );
    return -1;
  }
  pItem->text = json_string_value( jObj );

/*------------------------------------------------------------------------*\
    Extract item type
\*------------------------------------------------------------------------*/
  jObj = json_object_get( pItem->jItem, "type" );
  if( !jObj || !json_is_string(jObj) ) {
    logwarn( "_playlistItemFillHeader: Field \"type\" missing or of wrong type." );
    return -1;
  }
  typeStr = json_string_value( jObj );
  if( !strcmp(typeStr,"track") )
    pItem->type = PlaylistItemTrack;
  else if( !strcmp(typeStr,"stream") )
    pItem->type = PlaylistItemStream;
  else {
    logerr( "_playlistItemFillHeader: Unknown type \"%s\"", typeStr );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Extract streaming data list for quick access (weak ref.), optional
\*------------------------------------------------------------------------*/
  pItem->jStreamingRefs = json_object_get( pItem->jItem, "streamingRefs" );

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

