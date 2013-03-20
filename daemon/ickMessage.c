/*$*********************************************************************\

Name            : -

Source File     : ickMessage.c

Description     : implement ickstream device related  protocol 

Comments        : -

Called by       : ickstream wrapper 

Calls           : 

Error Messages  : -
  
Date            : 20.02.2013

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
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <ickDiscovery.h>
#include <jansson.h>

#include "utils.h"
#include "ickMessage.h"
#include "player.h"
#include "playlist.h"
#include "audio.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private definitions and symbols
\*=========================================================================*/

// Linked list of open command requests
typedef struct _openRequest {
  struct _openRequest  *next;
  int                   id;
  char                 *szDeviceId;
  json_t               *jCommand;
  IckCmdCallback        callback;
  double                timestamp; 
} OpenRequest;
OpenRequest    *openRequestList;
pthread_mutex_t openRequestListMutex = PTHREAD_MUTEX_INITIALIZER;


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
void    _unlinkOpenRequest( OpenRequest *request );	
void    _freeOpenRequest( OpenRequest *request );	
void    _timeoutOpenRequest( int timeout );	
json_t *_jPlayerStatus( void );


/*=========================================================================*\
       Handle messages for this device 
\*=========================================================================*/
void ickMessage( const char *szDeviceId, const void *iMessage, 
                 size_t messageLength, enum ickMessage_communicationstate state )
{
  char         *message;
  json_t       *jRoot,
               *jObj,
               *jParams,
               *jResult = NULL;
  json_error_t  error;
  const char   *method;
  json_t       *requestId;
  bool          playlistChanged = 0;

  // DBGMSG( "ickMessage from %s: %ld bytes", szDeviceId, (long)messageLength );

/*------------------------------------------------------------------------*\
    Get a standard terminated string out of buffer
\*------------------------------------------------------------------------*/
  message = malloc( messageLength+1 );
  if( !message ) {
    logerr( "ickMessage: out of memory" );
    return;
  }
  strncpy( message, iMessage, messageLength );
  message[messageLength] = 0;
  loginfo( "ickMessage from %s: %s", szDeviceId, message );

/*------------------------------------------------------------------------*\
    Init JSON interpreter
\*------------------------------------------------------------------------*/
  jRoot = json_loads( message, 0, &error );
  if( !jRoot ) {
    logerr( "ickMessage from %s: currupt line %d: %s", 
                     szDeviceId, error.line, error.text );
    Sfree( message );
    return;
  }
  if( !json_is_object(jRoot) ) {
  	logerr( "ickMessage from %s: could not parse to object: %s", 
  	                 szDeviceId, message );
    json_decref( jRoot );
    Sfree( message );
    return;
  } 
  // DBGMSG( "ickMessage from %s: parsed.", szDeviceId );
  
/*------------------------------------------------------------------------*\
    Get request ID
\*------------------------------------------------------------------------*/
  requestId = json_object_get( jRoot, "id" );
  if( !requestId ) {
    logerr( "ickMessage from %s contains no id: %s", 
                     szDeviceId, message );
    json_decref( jRoot );
    Sfree( message );
    return;
  }
  // DBGMSG( "ickMessage from %s: found id.", szDeviceId );
  
/*------------------------------------------------------------------------*\
    Check for result field: this is an answer to a command we've issued
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jRoot, "result" );
  if( jObj && json_is_object(jObj) ) {
    OpenRequest *request = openRequestList;
    int id;
    
    // Get integer Id from message
    if( json_is_integer(requestId) )
      id = json_integer_value( requestId );
    else if( json_is_string(requestId) ) {
#ifdef DEBUG    	
      logwarn( "ickMessage from %s returned id as string: %s", 
                            szDeviceId, message );
#endif                            
      id = atoi( json_string_value(requestId) );                
    }
    else {
      logwarn( "ickMessage from %s returned id in unknown format: %s", 
                            szDeviceId, message );
      json_decref( jRoot );
      Sfree( message );
      return;
    }
      
    // Find open request for ID 
    pthread_mutex_lock( &openRequestListMutex );
    while( request && request->id!=id )
      request = request->next;
    pthread_mutex_unlock( &openRequestListMutex );
    
    // Unlink open request and execute callback
    if( request ) {
      _unlinkOpenRequest( request );
      if( request->callback )
        request->callback( request->szDeviceId, request->jCommand, jRoot );
      _freeOpenRequest( request );
    }
    
    // Orphaned result?
    else 
      logwarn( "Found no open request for ickMessage from %s : %s", 
                           szDeviceId, message );

    // Clean up and take the chance to check for timedout requests
    json_decref( jRoot ); 
    _timeoutOpenRequest( 60 );

    // That's all for processing results
    Sfree( message );
    return;
  }
  
/*------------------------------------------------------------------------*\
    This is a command we need to answer: get message type and parameters
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jRoot, "method" );
  if( !jObj || !json_is_string(jObj) ) {
    logerr( "ickMessage from %s contains neither method or result: %s", 
                     szDeviceId, message );
    json_decref( jRoot );
    Sfree( message );
    return;
  }
  method = json_string_value( jObj );
  DBGMSG( "ickMessage from %s: Executing command \"%s\"", szDeviceId, method );

  jParams = json_object_get( jRoot, "params" );
  if( !jParams || !json_is_object(jParams) ) {
    logerr( "ickMessage from %s contains no parameters: %s", 
                     szDeviceId, message );
    json_decref( jRoot );
    Sfree( message );
    return;
  }

/*------------------------------------------------------------------------*\
    Get player status 
\*------------------------------------------------------------------------*/
  if( !strcasecmp(method,"getPlayerStatus") ) {
  	jResult   = _jPlayerStatus();
  }

/*------------------------------------------------------------------------*\
    Get position in track
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getSeekPosition") ) {
  	Playlist *plst    = playerGetQueue();
  	jResult = json_pack( "{sisf}",
                         "playlistPos", playlistGetCursorPos(plst),
                         "seekPos",     playerGetSeekPos() );
  }

/*------------------------------------------------------------------------*\
    Get track info
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getTrack") ) {
  	Playlist     *plst = playerGetQueue();
  	PlaylistItem *pItem;
  	int           pos;

  	// Get requested position or use cursor for current track 
  	jObj = json_object_get( jParams, "playlistPos" );
    if( jObj && json_is_real(jObj) ) 
      pos = json_integer_value( jObj );
    else
      pos = playlistGetCursorPos( plst );

    // Construct result
  	jResult   = json_pack( "{sssi}",
  	                       "playlistId",  playlistGetId(plst),
                           "playlistPos", pos );
    
    // Add info about current track (if any)
    pItem = playlistGetItem( plst, pos );
    if( pItem )
      json_object_set( jResult, "track", pItem->jItem );
  }  

/*------------------------------------------------------------------------*\
    Set track index to play 
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"setTrack") ) {
    Playlist *plst    = playerGetQueue();
    int       offset  = 0;
    
    // Get position
    jObj = json_object_get( jParams, "playlistPos" );
    if( !jObj || !json_is_integer(jObj) )  {
      logerr( "ickMessage from %s: missing field \"playlistPos\": %s", 
                       szDeviceId, message );
      json_decref( jRoot );
      Sfree( message );
      return;
    }
    offset = json_integer_value( jObj );

    // Change pointer in playlist
    playlistSetCursorPos( plst, offset );

    // Set and broadcast player mode to account for skipped tracks
    playerSetState( playerGetState(), true );
       
    // report current state
    jResult = json_pack( "{si}",
                         "playlistPos", playlistGetCursorPos(plst) );                         
  }

/*------------------------------------------------------------------------*\
    Play or pause 
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"play") ) {
    PlayerState newState;
    
    // Get mode
    jObj = json_object_get( jParams, "playing" );
    if( !jObj || !json_is_boolean(jObj) )  {
      logerr( "ickMessage from %s: missing field \"playing\": %s", 
                       szDeviceId, message );
      json_decref( jRoot );
      Sfree( message );
      return;
    }
    newState = json_is_true(jObj) ? PlayerStatePlay : PlayerStatePause;
    
    // Set and broadcast player mode
    playerSetState( newState, true );
     
    // report current state
    jResult = json_pack( "{sb}",
                         "playing", playerGetState()==PlayerStatePlay );                         
  }
    
/*------------------------------------------------------------------------*\
    Report player volume 
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getVolume") ) {
    jResult = json_pack( "{fb}", 
                         "volumeLevel", playerGetVolume(), 
                         "muted", playerGetMuting() );
  }

/*------------------------------------------------------------------------*\
    Adjust player volume 
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"setVolume") ) {

    // Set volume absultely
    jObj = json_object_get( jParams, "volumeLevel" );
    if( jObj && json_is_real(jObj) ) {
      playerSetVolume( json_real_value(jObj), true );
    }

    // Set volume relative to current value
    jObj = json_object_get( jParams, "relativeVolumeLevel" );
    if( jObj && json_is_real(jObj) ) {
      double volume =  playerGetVolume();
      volume *= 1 + json_real_value( jObj );
      playerSetVolume( volume, true );
    }

    // Set muting state 
    jObj = json_object_get( jParams, "muted" );
    if( jObj && json_is_boolean(jObj) ) {
      playerSetMuting( json_is_true(jObj), true );
    }

    // report current state
    jResult = json_pack( "{sfsb}",
                         "volumeLevel", playerGetVolume(),
                         "muted", playerGetMuting() );                         
  }

/*------------------------------------------------------------------------*\
    Get playlist
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getPlaylist") ) {
    Playlist     *plst    = playerGetQueue();
    int           offset  = 0;
    int           count   = 0;
      
    // Get explicit offset
    jObj = json_object_get( jParams, "offset" );
    if( jObj && json_is_integer(jObj) ) {
       offset = json_integer_value( jObj );
    }
    
    // Get explicit count
    jObj = json_object_get( jParams, "count" );
    if( jObj && json_is_integer(jObj) ) {
       count = json_integer_value( jObj );
    }

    // Compile JSON projection of list
    jResult = playlistGetJSON( plst, offset, count ); 
  }
           
/*------------------------------------------------------------------------*\
    Set playlist id and name
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"setPlaylistName") ) {
    Playlist     *plst = playerGetQueue();
    const char   *id   = NULL;
    const char   *name = NULL;
      
    // Get ID
    jObj = json_object_get( jParams, "playlistId" );
    if( !jObj || !json_is_string(jObj) )  {
      logerr( "ickMessage from %s: missing field \"playlistId\": %s", 
                       szDeviceId, message );
      json_decref( jRoot );
      Sfree( message );
      return;
    }
    id = json_string_value( jObj );
    
    // Get name
    jObj = json_object_get( jParams, "playlistName" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "ickMessage from %s: missing field \"playlistName\": %s", 
                       szDeviceId, message );
      json_decref( jRoot );
      Sfree( message );
      return;
    }
    name = json_string_value( jObj );

    // Set id and name
    playlistSetId( plst, id );
    playlistSetName( plst, name );
    
    // Playlist has changed
    playlistChanged = true;

    // report result 
    jResult = json_pack( "{sssssi}",
                         "playlistId", id, 
                         "playlistName", name, 
                         "countAll", playlistGetLength(plst) );
  }
           
/*------------------------------------------------------------------------*\
    Add track to playlist or replace playlist
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"addTracks") || 
           !strcasecmp(method,"setTracks") ) {
    int           i;
    json_t       *jItems;
    Playlist     *plst;
    PlaylistItem *anchorItem = NULL;          // Item to add list before

    // Replace the current list, don't store current content
    if( !strcasecmp(method,"setTracks") )
      playerResetQueue( );
    
    // Get existing or new playlist   
    plst = playerGetQueue( );
      
    // Get explicite position 
    jObj = json_object_get( jParams, "playlistPos" );
    if( jObj && json_is_integer(jObj) ) {
       int pos    = json_integer_value( jObj );
       anchorItem = playlistGetItem( plst, pos );
    }
    
    // Get list of new items
    jItems = json_object_get( jParams, "items" );
    if( !jItems || !json_is_array(jItems) ) {
      logerr( "ickMessage from %s: missing field \"items\": %s", 
                       szDeviceId, message );
      json_decref( jRoot );
      Sfree( message );
      return;
    }
    
    // Loop over all items to add
    for( i=0; i<json_array_size(jItems); i++ ) {
      json_t  *jItem = json_array_get( jItems, i );

      // Create playlist entry from json payload
      PlaylistItem *pItem = playlistItemFromJSON( jItem );
      if( !pItem ) {
        logerr( "ickMessage from %s: could not parse item #%d: %s",
                       szDeviceId, i+1, message );
      } 

      // Add new item to playlist before anchor 
      else if( anchorItem )
        playlistAddItemBefore( plst, anchorItem, pItem );
      
      // Add new item to end of list
      else
        playlistAddItemAfter( plst, NULL, pItem );
    } 

    // Playlist has changed
    playlistChanged = true;

    // report result 
    jResult = json_pack( "{sbsi}",
                         "result", 1, 
                         "playlistPos", playlistGetCursorPos(plst) );
  }

/*------------------------------------------------------------------------*\
    Remove tracks from playlist
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"removeTracks") ) {
    int           i;
    json_t       *jItems;
    Playlist     *plst = playerGetQueue();
   
    // Get list of new items
    jItems = json_object_get( jParams, "items" );
    if( !jItems || !json_is_array(jItems) ) {
      logerr( "ickMessage from %s: missing field \"items\": %s", 
                       szDeviceId, message );
      json_decref( jRoot );
      Sfree( message );
      return;
    }
    
    // Loop over all items to remove
    for( i=0; i<json_array_size(jItems); i++ ) {
      json_t       *jItem = json_array_get( jItems, i );
      const char   *id    = NULL;
      PlaylistItem *pItem;
      
      // Get item ID
      jObj = json_object_get( jItem, "id" );
      if( !jObj || !json_is_string(jObj) ) {
        logerr( "ickMessage from %s: item missing field \"id\": %s", 
                       szDeviceId, message );
        continue;
      }              
      id = json_string_value( jObj );
        
      // Get item by explicit position 
      jObj = json_object_get( jItem, "playlistPos" );
      if( jObj && json_is_integer(jObj) ) {
        int pos    = json_integer_value( jObj );
        pItem      = playlistGetItem( plst, pos );
        if( pItem && strcmp(id,pItem->id) ) {
          logwarn( "ickMessage from %s: item id differs from id at explicite position: %s", 
                       szDeviceId, message );
        }
        if( pItem ) { 
          playlistUnlinkItem( plst, pItem );
          playlistItemDelete( pItem );
        }
        else
          logwarn( "ickMessage from %s: cannot remove item @%d: %s", 
                       szDeviceId, pos, message );        
      }
      
      // Remove all items with this ID
      else for(;;) {
        pItem = playlistGetItemById( plst, id );
        if( !pItem )
          break;
        playlistUnlinkItem( plst, pItem );
        playlistItemDelete( pItem );
      }
    }
    
    // Playlist has changed
    playlistChanged = true;

    // report result 
    jResult = json_pack( "{sbsi}",
                         "result", 1, 
                         "playlistPos", playlistGetCursorPos(plst) );
  }

/*------------------------------------------------------------------------*\
    Move tracks within playlist
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"moveTracks") ) {
    int            i;
    json_t        *jItems;
    Playlist      *plst = playerGetQueue();
    PlaylistItem  *anchorItem = NULL;          // Item to move others before
    PlaylistItem **pItems;                     // Array of items to move
    int            pItemCnt;                   // Elements in pItems 
    
    // Get explicite position 
    jObj = json_object_get( jParams, "playlistPos" );
    if( jObj && json_is_integer(jObj) ) {
       int pos    = json_integer_value( jObj );
       anchorItem = playlistGetItem( plst, pos );
    }
    
    // Get list of items to move
    jItems = json_object_get( jParams, "items" );
    if( !jItems || !json_is_array(jItems) ) {
      logerr( "ickMessage from %s: missing field \"items\": %s", 
                       szDeviceId, message );
      json_decref( jRoot );
      Sfree( message );
      return;
    }
    
    // Allocate temporary array of items to move
    pItemCnt = 0;
    pItems   = calloc( json_array_size(jItems), sizeof(PlaylistItem *) );
    if( !pItems ) {
      logerr( "ickMessage: out of memory" );
      json_decref( jRoot );
      Sfree( message );
      return;
    }
    
    // Loop over all items to move
    for( i=0; i<json_array_size(jItems); i++ ) {
      json_t       *jItem = json_array_get( jItems, i );
      const char   *id;
      int           pos;
      PlaylistItem *pItem;
              
      // Get explicit position 
      jObj = json_object_get( jItem, "playlistPos" );
      if( !jObj || !json_is_integer(jObj) ) {
        logerr( "ickMessage from %s: item missing field \"playlistPos\": %s", 
                       szDeviceId, message );
        continue;
      } 
      pos   = json_integer_value( jObj );
      
      // Get item 
      pItem = playlistGetItem( plst, pos );
      if( !pItem ) {
        logwarn( "ickMessage from %s: cannot remove item @%d: %s", 
                        szDeviceId, pos, message );
        continue;
      }

      // Get item ID
      jObj = json_object_get( jItem, "id" );
      if( !jObj || !json_is_string(jObj) ) {
        logerr( "ickMessage from %s: item missing field \"id\": %s", 
                       szDeviceId, message );
        continue;
      }              
      id = json_string_value( jObj );
      
      // Be defensive
      if( strcmp(id,pItem->id) ) {
        logwarn( "ickMessage from %s: item id differs from id at explicite position: %s", 
                              szDeviceId, message );
      }

      // Be pranoid: check for doubles since unlinking is not stable against double calls
      bool found = 0;
      int j;
      for( j=0; j<pItemCnt && !found; j++ ) {
        found = ( pItems[j]==pItem );
      }
      if( found ) {
        logwarn( "ickMessage from %s: doubles in item list (playlist pos %d): %s", 
                              szDeviceId, pos, message );
        continue;                      
      }
      
      // add Item to temporary list 
      pItems[pItemCnt++] = pItem;
      
    } /* for( i=0; i<json_array_size(jItems); i++ ) */
    
    // Unlink all identified items, but do not delete them
    for( i=0; i<pItemCnt; i++ ) {
       playlistUnlinkItem( plst, pItems[i] ); 
    }
    
    // Reinsert the items 
    for( i=0; i<pItemCnt; i++ ) {
       if( anchorItem )
         playlistAddItemBefore( plst, anchorItem, pItems[i] );
       else 
         playlistAddItemAfter( plst, NULL, pItems[i] );
    }
    
    // Free temporary list of items to move
    Sfree( pItems );
    
    // Playlist has changed
    playlistChanged = true;
      
    // report result 
    jResult = json_pack( "{sbsi}",
                         "result", 1, 
                         "playlistPos", playlistGetCursorPos(plst) );
  } 
  
/*------------------------------------------------------------------------*\
    Set player configuration
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"setPlayerConfiguration") ) {
  	
    // Get player name 
    jObj = json_object_get( jParams, "playerName" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "ickMessage from %s: item missing field \"playerName\": %s", 
                       szDeviceId, message );
      json_decref( jRoot );
      Sfree( message );
      return;
    } 
    const char *name = json_string_value( jObj );

    // Get access token 
    jObj = json_object_get( jParams, "accessToken" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "ickMessage from %s: item missing field \"accessToken\": %s", 
                       szDeviceId, message );
      json_decref( jRoot );
      Sfree( message );
      return;
    } 
    const char *token = json_string_value( jObj );

    // Store variables
    playerSetName( name, true );
    playerSetToken( token );
    
    // report result 
    jResult = json_pack( "{ssss}",
                         "playerName", playerGetName(), 
                         "playerModel", playerGetModel() );
  }

/*------------------------------------------------------------------------*\
    Report player configuration
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getPlayerConfiguration") ) {
    const char *hwid;
  	
    // compile result 
    jResult = json_pack( "{ssss}",
                         "playerName",  playerGetName(), 
                         "playerModel", playerGetModel() );
    
    // Append hardware id if available
    hwid = playerGetHWID();
    if( hwid ) 
      json_object_set_new( jResult, "hardwareId", json_string(hwid) );
  }  
  
/*------------------------------------------------------------------------*\
    Unknown message 
\*------------------------------------------------------------------------*/
  else {
    logerr( "ickMessage from %s: ignoring method %s", szDeviceId, method );
  }

/*------------------------------------------------------------------------*\
   return result 
\*------------------------------------------------------------------------*/
  if( jResult ) {
    jResult = json_pack( "{sssoso}",
                         "jsonrpc", "2.0", 
                         "id",     requestId,
                         "result", jResult );
    sendIckMessage( szDeviceId, jResult );
    json_decref( jResult );
  } 
  
/*------------------------------------------------------------------------*\
   Broadcast changes in playlist 
\*------------------------------------------------------------------------*/
  DBGMSG( "ickMessage from %s: need to update playlist: %s", 
            szDeviceId, playlistChanged?"Yes":"No" );
  if( playlistChanged )
    ickMessageNotifyPlaylist();

/*------------------------------------------------------------------------*\
    Clean up: Free JSON message object and check for timedout requests
\*------------------------------------------------------------------------*/
  json_decref( jRoot );  
  Sfree( message );
  _timeoutOpenRequest( 60 );
}


/*=========================================================================*\
	Send a notification for playlist update
\*=========================================================================*/
void ickMessageNotifyPlaylist( void )
{
  Playlist *plst;;
  json_t   *jMsg;

  DBGMSG( "ickMessageNotifyPlaylist" );    

/*------------------------------------------------------------------------*\
    Set up parameters
\*------------------------------------------------------------------------*/
  plst = playerGetQueue( );
  jMsg = json_pack( "{sf si}",
                      "lastChanged", (double) playlistGetLastChange(plst), 
                      "countAll", playlistGetLength(plst) );
  // Name and ID are optional                       
  if( plst->id )
    json_object_set_new( jMsg, "playlistId", json_string(plst->id) );
  if( plst->name )
    json_object_set_new( jMsg, "playlistName", json_string(plst->name) );

/*------------------------------------------------------------------------*\
    Set up message
\*------------------------------------------------------------------------*/
    jMsg = json_pack( "{ss ss so }",
                      "jsonrpc", "2.0", 
                      "method", "playlistChanged",
                      "params", jMsg );
                        
/*------------------------------------------------------------------------*\
    Broadcast and clean up
\*------------------------------------------------------------------------*/
    sendIckMessage( NULL, jMsg );
    json_decref( jMsg );                       	
}


/*=========================================================================*\
	Send a notification for plyer status update
\*=========================================================================*/
void ickMessageNotifyPlayerState( void )
{
  json_t *jMsg;
  
  DBGMSG( "ickMessageNotifyPlayerState" );    
  
  jMsg = _jPlayerStatus();                        
  sendIckMessage( NULL, jMsg );
  json_decref( jMsg );                       	
}


/*=========================================================================*\
	Compile player status for getPlayerStatus or playerStatusChanged
\*=========================================================================*/
json_t *_jPlayerStatus( void )
{
  Playlist     *plst     = playerGetQueue();
  int           cursorPos;
  double        pChange, aChange;
  json_t       *jResult; 	
  PlaylistItem *pItem;	
  	
/*------------------------------------------------------------------------*\
    Create status message
\*------------------------------------------------------------------------*/
  cursorPos = playlistGetCursorPos( plst );
  pChange   = playlistGetLastChange( plst );
  aChange   = playerGetLastChange( );
  jResult   = json_pack( "{sbsfsisfsbsf}",
  	                       "playing",     playerGetState()==PlayerStatePlay,
                           "seekPos",     playerGetSeekPos(),
                           "playlistPos", cursorPos,
                           "volumeLevel", playerGetVolume(), 
                           "muted",       playerGetMuting(),
                           "lastChanged", MAX(aChange,pChange) );

/*------------------------------------------------------------------------*\
    Add info about current track (if any)
\*------------------------------------------------------------------------*/
  pItem = playlistGetItem( plst, cursorPos );
  if( pItem )
    json_object_set( jResult, "track", pItem->jItem );
    
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return jResult;   
}

/*=========================================================================*\
	Wrapper for Sending an ickstream JSON message 
\*=========================================================================*/
enum ickMessage_communicationstate sendIckMessage( const char *szDeviceId, json_t *jMessage )
{
  char *message;
  int  i;
  enum ickMessage_communicationstate result;
  
/*------------------------------------------------------------------------*\
    Convert JSON to string
\*------------------------------------------------------------------------*/
  message = json_dumps( jMessage, JSON_PRESERVE_ORDER | JSON_COMPACT | JSON_ENSURE_ASCII );

/*------------------------------------------------------------------------*\
    Loop till timeout or success
\*------------------------------------------------------------------------*/
  for( i = 1; i<10; i++ ) {  
    loginfo( "ickMessage (try %d) to %s: %s", i, szDeviceId?szDeviceId:"ALL", message );    
    result = ickDeviceSendMsg( szDeviceId, message, strlen(message) );
    if( result==ICKMESSAGE_SUCCESS )
      break;
    sleep( 1 );
  }
  
/*------------------------------------------------------------------------*\
    Clean up and that's all
\*------------------------------------------------------------------------*/
  Sfree( message );
  return result;
}


/*=========================================================================*\
	Send a command and register callback.
	  requestID returns the unique ID (we use an integer here) and might be NULL
\*=========================================================================*/
enum ickMessage_communicationstate  
  sendIckCommand( const char *szDeviceId, const char *method, json_t *jParams, 
                  int *requestId, IckCmdCallback callback )
{
  static int requestIdCntr = 0;
  
/*------------------------------------------------------------------------*\
    Create an init list element for callbacks
\*------------------------------------------------------------------------*/
  OpenRequest *request = calloc( 1, sizeof(OpenRequest) );
  if( !request )
    return -1;
    
  request->szDeviceId = strdup( szDeviceId );  
  request->id         = ++requestIdCntr;
  request->callback   = callback;
  request->timestamp  = srvtime(); 
  
  if( requestId )
    *requestId = request->id ;

/*------------------------------------------------------------------------*\
    Build request 
\*------------------------------------------------------------------------*/
  request->jCommand = json_pack( "{sssiss}",
                                 "jsonrpc", "2.0", 
                                 "id", request->id,
                                 "method", method );
  if( jParams )
    json_object_set( request->jCommand, "params", jParams );
  else                  
    json_object_set_new( request->jCommand, "params", json_object() );
      
/*------------------------------------------------------------------------*\
    Link request to open list
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &openRequestListMutex );
  request->next   = openRequestList;
  openRequestList = request;
  pthread_mutex_unlock( &openRequestListMutex );
  
/*------------------------------------------------------------------------*\
    Send Request (need no decref, since the command is stored in open request
\*------------------------------------------------------------------------*/
  return sendIckMessage( szDeviceId, request->jCommand );
}


/*=========================================================================*\
	Unlink an open request from list
\*=========================================================================*/
void _unlinkOpenRequest( OpenRequest *request )	
{
		  
/*------------------------------------------------------------------------*\
    Lock list and search for entry 
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &openRequestListMutex );
  OpenRequest *prevElement = NULL;
  OpenRequest *element     = openRequestList;
  while( element ) {
  	if( element==request )
  	  break;
    prevElement = element;
    element = element->next;  	
  }

/*------------------------------------------------------------------------*\
    Unlink element 
\*------------------------------------------------------------------------*/
  if( element ) {
    if( !prevElement )    // replace list root
      openRequestList = element->next;
    else
      prevElement->next = element->next;
  }
  
/*------------------------------------------------------------------------*\
    Straying request? 
\*------------------------------------------------------------------------*/
  else {
  	char *txt = json_dumps( request->jCommand, JSON_PRESERVE_ORDER | JSON_COMPACT | JSON_ENSURE_ASCII );
    logwarn( "Cannot unlink straying open request #%d: %s", request->id, txt );
    Sfree( txt );
  }
    
/*------------------------------------------------------------------------*\
    Unlock list 
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &openRequestListMutex );
}


/*=========================================================================*\
	Free an open request list element
\*=========================================================================*/
void _freeOpenRequest( OpenRequest *request )	
{
  Sfree( request->szDeviceId );		
  json_decref( request->jCommand );
  Sfree( request );
}


/*=========================================================================*\
	Check for timedout requests
\*=========================================================================*/
void _timeoutOpenRequest( int timeout )	
{
  double timeoutStamp = srvtime() - timeout;
  		  
/*------------------------------------------------------------------------*\
    Lock list and loop over all entries 
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &openRequestListMutex );
  OpenRequest *prevElement = NULL;
  OpenRequest *element     = openRequestList;
  while( element ) {
  	
/*------------------------------------------------------------------------*\
    Not yet timed out
\*------------------------------------------------------------------------*/
  	if( element->timestamp>timeoutStamp ) {
  	  prevElement = element;
      element = element->next;  		
      continue;
    }

/*------------------------------------------------------------------------*\
    Buffer follower (since element might vanish)
\*------------------------------------------------------------------------*/
    OpenRequest *nextElement = element->next;  
    
/*------------------------------------------------------------------------*\
    Be verbose
\*------------------------------------------------------------------------*/
    char *txt = json_dumps( element->jCommand, JSON_PRESERVE_ORDER | JSON_COMPACT | JSON_ENSURE_ASCII );  
    logwarn( "ickRequest #%d timed out: %s", element->id, txt );
    Sfree( txt );
  	
/*------------------------------------------------------------------------*\
    Unlink timedout element
\*------------------------------------------------------------------------*/
  	if( !prevElement )    // replace list root
      openRequestList = element->next;
    else
      prevElement->next = element->next;
        
/*------------------------------------------------------------------------*\
    Free ressources
\*------------------------------------------------------------------------*/
    _freeOpenRequest( element );  
    
/*------------------------------------------------------------------------*\
    Go to next element
\*------------------------------------------------------------------------*/
    element = nextElement;  	
  }
    
/*------------------------------------------------------------------------*\
    Unlock list 
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &openRequestListMutex );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/



