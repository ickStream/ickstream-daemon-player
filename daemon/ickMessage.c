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
#include <ickDiscovery.h>
#include <jansson.h>

#include "ickpd.h"
#include "ickMessage.h"
#include "audio.h"
#include "playlist.h"
#include "player.h"

/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	private symbols
\*=========================================================================*/
json_t *_jPlayerStatus( void );
static enum ickMessage_communicationstate  _sendIckMessage( const char *szDeviceId, json_t *jMessage );


/*=========================================================================*\
       Handle messages for this device 
\*=========================================================================*/
void ickMessage( const char *szDeviceId, const void *message, 
                 size_t messageLength, enum ickMessage_communicationstate state )
{
  json_t       *jRoot,
               *jObj,
               *jParams,
               *jResult = NULL;
  json_error_t  error;
  const char   *method;
  int           requestId;
  bool          playlistChanged = 0;
  bool          playerStateChanged = 0;

  srvmsg( LOG_DEBUG, "ickMessage from %s: %s", szDeviceId, (const char *)message );

/*------------------------------------------------------------------------*\
    Init JSON interpreter
\*------------------------------------------------------------------------*/
  jRoot = json_loads(message, 0, &error);
  if( !jRoot ) {
    srvmsg( LOG_ERR, "ickMessage from %s: currupt line %d: %s", szDeviceId, error.line, error.text );
    return;
  }
  if( !json_is_object(jRoot) ) {
    json_decref( jRoot );
    return;
  } 

/*------------------------------------------------------------------------*\
    Get message type, id and parameters
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jRoot, "method" );
  if( !jObj || !json_is_string(jObj) ) {
    srvmsg( LOG_ERR, "ickMessage from %s contains no method: %s", 
                     szDeviceId, (const char *)message );
    json_decref( jRoot );
    return;
  }
  method = json_string_value( jObj );

  jObj = json_object_get( jRoot, "id" );
  if( !jObj || !json_is_integer(jObj) ) {
    srvmsg( LOG_ERR, "ickMessage from %s contains no id: %s", 
                     szDeviceId, (const char *)message );
    json_decref( jRoot );
    return;
  }
  requestId = json_integer_value( jObj );

  jParams = json_object_get( jRoot, "params" );
  if( !jParams || !json_is_object(jParams) ) {
    srvmsg( LOG_ERR, "ickMessage from %s contains no parameters: %s", 
                     szDeviceId, (const char *)message );
    json_decref( jRoot );
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
  	Playlist *plst    = playlistGetPlayerQueue();
  	jResult = json_pack( "{sisf}",
                         "playlistPos", playlistGetCursorPos(plst),
                         "seekPos",     audioGetSeekPos() );
  }

/*------------------------------------------------------------------------*\
    Get index of track in playlist
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getTrack") ) {
  	Playlist     *plst = playlistGetPlayerQueue();
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
    Playlist *plst    = playlistGetPlayerQueue();
    int       offset  = 0;
    
    // Get position
    jObj = json_object_get( jParams, "playlistPos" );
    if( !jObj || !json_is_integer(jObj) )  {
      srvmsg( LOG_ERR, "ickMessage from %s: missing field \"playlistPos\": %s", 
                       szDeviceId, (const char *)message );
      json_decref( jRoot );
      return;
    }
    offset = json_integer_value( jObj );

    // Change pointer in playlist
    playlistSetCursorPos( plst, offset );

    // Fixme: skip to new track
     
    // player state has changed
    playerStateChanged = true;

    // report current state
    jResult = json_pack( "{si}",
                         "playlistPos", playlistGetCursorPos(plst) );                         
  }
  
/*------------------------------------------------------------------------*\
    Report player volume 
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getVolume") ) {
    jResult = json_pack( "{fb}", 
                         "volumeLevel", audioGetVolume(), 
                         "muted", audioGetMuting() );
  }

/*------------------------------------------------------------------------*\
    Adjust player volume 
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"setVolume") ) {

    // Set volume absultely
    jObj = json_object_get( jParams, "volumeLevel" );
    if( jObj && json_is_real(jObj) ) {
      audioSetVolume( json_real_value(jObj) );
    }

    // Set volume relative to current value
    jObj = json_object_get( jParams, "relativeVolumeLevel" );
    if( jObj && json_is_real(jObj) ) {
      double volume =  audioGetVolume();
      volume *= 1 + json_real_value( jObj );
      audioSetVolume( volume );
    }

    // Set muting state 
    jObj = json_object_get( jParams, "muted" );
    if( jObj && json_is_boolean(jObj) ) {
      audioSetMuting( json_is_true(jObj) );
    }

    // player state has changed
    playerStateChanged = true;

    // report current state
    jResult = json_pack( "{sfsb}",
                         "volumeLevel", audioGetVolume(),
                         "muted", audioGetMuting() );                         
  }

/*------------------------------------------------------------------------*\
    Get playlist
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getPlaylist") ) {
    Playlist     *plst    = playlistGetPlayerQueue();
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
    Playlist     *plst = playlistGetPlayerQueue();
    const char   *id   = NULL;
    const char   *name = NULL;
      
    // Get ID
    jObj = json_object_get( jParams, "playlistId" );
    if( !jObj || !json_is_string(jObj) )  {
      srvmsg( LOG_ERR, "ickMessage from %s: missing field \"playlistId\": %s", 
                       szDeviceId, (const char *)message );
      json_decref( jRoot );
      return;
    }
    id = json_string_value( jObj );
    
    // Get name
    jObj = json_object_get( jParams, "playlistName" );
    if( jObj && json_is_string(jObj) ) {
      srvmsg( LOG_ERR, "ickMessage from %s: missing field \"playlistName\": %s", 
                       szDeviceId, (const char *)message );
      json_decref( jRoot );
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
      playlistFreePlayerQueue( false );
    
    // Get existing or new playlist   
    plst = playlistGetPlayerQueue();
      
    // Get explicite position 
    jObj = json_object_get( jParams, "playlistPos" );
    if( jObj && json_is_integer(jObj) ) {
       int pos    = json_integer_value( jObj );
       anchorItem = playlistGetItem( plst, pos );
    }
    
    // Get list of new items
    jItems = json_object_get( jParams, "items" );
    if( !jItems || !json_is_array(jItems) ) {
      srvmsg( LOG_ERR, "ickMessage from %s: missing field \"items\": %s", 
                       szDeviceId, (const char *)message );
      json_decref( jRoot );
      return;
    }
    
    // Loop over all items to add
    for( i=0; i<json_array_size(jItems); i++ ) {
      json_t  *jItem = json_array_get( jItems, i );

      // Create playlist entry from json payload
      PlaylistItem *pItem = playlistItemFromJSON( jItem );
      if( !pItem ) {
        srvmsg( LOG_ERR, "ickMessage from %s: could not parse item #%d: %s",
                       szDeviceId, i+1, (const char *)message );
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

    // player state has changed
    playerStateChanged = true;  //Fixme: check for actual change

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
    Playlist     *plst = playlistGetPlayerQueue();
   
    // Get list of new items
    jItems = json_object_get( jParams, "items" );
    if( !jItems || !json_is_array(jItems) ) {
      srvmsg( LOG_ERR, "ickMessage from %s: missing field \"items\": %s", 
                       szDeviceId, (const char *)message );
      json_decref( jRoot );
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
        srvmsg( LOG_ERR, "ickMessage from %s: item missing field \"id\": %s", 
                       szDeviceId, (const char *)message );
        continue;
      }              
      id = json_string_value( jObj );
        
      // Get item by explicit position 
      jObj = json_object_get( jItem, "playlistPos" );
      if( jObj && json_is_integer(jObj) ) {
        int pos    = json_integer_value( jObj );
        pItem      = playlistGetItem( plst, pos );
        if( pItem && strcmp(id,pItem->id) ) {
          srvmsg( LOG_WARNING, "ickMessage from %s: item id differs from id at explicite position: %s", 
                       szDeviceId, (const char *)message );
        }
        if( pItem ) { 
          playlistUnlinkItem( plst, pItem );
          playlistItemDelete( pItem );
        }
        else
          srvmsg( LOG_WARNING, "ickMessage from %s: cannot remove item @%d: %s", 
                       szDeviceId, pos, (const char *)message );        
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

    // player state has changed
    playerStateChanged = true;  //Fixme: check for actual change

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
    Playlist      *plst = playlistGetPlayerQueue();
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
      srvmsg( LOG_ERR, "ickMessage from %s: missing field \"items\": %s", 
                       szDeviceId, (const char *)message );
      json_decref( jRoot );
      return;
    }
    
    // Allocate temporary arry of items to move
    pItemCnt = 0;
    pItems   = calloc( json_array_size(jItems), sizeof(PlaylistItem *) );
    if( !pItems ) {
      srvmsg( LOG_ERR, "ickMessage: out of memory" );
      json_decref( jRoot );
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
        srvmsg( LOG_ERR, "ickMessage from %s: item missing field \"playlistPos\": %s", 
                       szDeviceId, (const char *)message );
        continue;
      } 
      pos   = json_integer_value( jObj );
      
      // Get item 
      pItem = playlistGetItem( plst, pos );
      if( !pItem ) {
        srvmsg( LOG_WARNING, "ickMessage from %s: cannot remove item @%d: %s", 
                        szDeviceId, pos, (const char *)message );
        continue;
      }

      // Get item ID
      jObj = json_object_get( jItem, "id" );
      if( !jObj || !json_is_string(jObj) ) {
        srvmsg( LOG_ERR, "ickMessage from %s: item missing field \"id\": %s", 
                       szDeviceId, (const char *)message );
        continue;
      }              
      id = json_string_value( jObj );
      
      // Be defensive
      if( strcmp(id,pItem->id) ) {
        srvmsg( LOG_WARNING, "ickMessage from %s: item id differs from id at explicite position: %s", 
                              szDeviceId, (const char *)message );
      }

      // Be pranoid: check for doubles since unlinking is not stable against double calls
      bool found = 0;
      int j;
      for( j=0; j<pItemCnt && !found; j++ ) {
        found = ( pItems[j]==pItem );
      }
      if( found ) {
        srvmsg( LOG_WARNING, "ickMessage from %s: doubles in item list (playlist pos %d): %s", 
                              szDeviceId, pos, (const char *)message );
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
      
    // player state has changed
    playerStateChanged = true;  //Fixme: check for actual change

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
      srvmsg( LOG_ERR, "ickMessage from %s: item missing field \"playerName\": %s", 
                       szDeviceId, (const char *)message );
      json_decref( jRoot );
      return;
    } 
    const char *name = json_string_value( jObj );

    // Get access token 
    jObj = json_object_get( jParams, "accessToken" );
    if( !jObj || !json_is_string(jObj) ) {
      srvmsg( LOG_ERR, "ickMessage from %s: item missing field \"accessToken\": %s", 
                       szDeviceId, (const char *)message );
      json_decref( jRoot );
      return;
    } 
    const char *token = json_string_value( jObj );

    // Store variables
    playerSetName( name );
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
  	
  	// compile result 
    jResult = json_pack( playerGetHWID()?"{ssssss}":"{ssss}",
                         "playerName",  playerGetName(), 
                         "playerModel", playerGetModel(),
                         "hardwareId",  playerGetHWID() );

  }  
  
/*------------------------------------------------------------------------*\
    Unknown message 
\*------------------------------------------------------------------------*/
  else {
    srvmsg( LOG_ERR, "ickMessage from %s: ignoring method %s", szDeviceId, method );
  }

/*------------------------------------------------------------------------*\
   return result 
\*------------------------------------------------------------------------*/
  if( jResult ) {
    jResult = json_pack( "{sssiso}",
                         "jsonrpc", "2.0", 
                         "id", requestId,
                         "result", jResult );
    _sendIckMessage( szDeviceId, jResult );
    json_decref( jResult );
  } 
  
/*------------------------------------------------------------------------*\
   Broadcast changes in playlist 
\*------------------------------------------------------------------------*/
  if( playlistChanged )
    ickMessageNotifyPlaylist();
    
/*------------------------------------------------------------------------*\
   Broadcast changes in player state
\*------------------------------------------------------------------------*/
  if( playerStateChanged )
    ickMessageNotifyPlayerState();
    
/*------------------------------------------------------------------------*\
    That's it; clean up
\*------------------------------------------------------------------------*/
  json_decref( jRoot );
}


/*=========================================================================*\
	Send a notification for playlist update
\*=========================================================================*/
void ickMessageNotifyPlaylist( void )
{
    Playlist *plst;
    json_t   *jMsg;
    
    plst = playlistGetPlayerQueue();
    jMsg = json_pack( "{ss ss s {ss ss sf si} }",
                      "jsonrpc", "2.0", 
                      "method", "playlistChanged",
                      "params", 
                        "playlistId",  playlistGetId(plst),
    	                "playlistName", playlistGetName(plst),
                        "lastChanged", (double) playlistGetLastChange(plst), 
                        "countAll", playlistGetLength(plst) );
                        
    _sendIckMessage( NULL, jMsg );
    json_decref( jMsg );                       	
}


/*=========================================================================*\
	Send a notification for plyer status update
\*=========================================================================*/
void ickMessageNotifyPlayerState( void )
{
    json_t *jMsg = _jPlayerStatus();                        
    _sendIckMessage( NULL, jMsg );
    json_decref( jMsg );                       	
}


/*=========================================================================*\
	Compile player status for getPlayerStatus or playerStatusChanged
\*=========================================================================*/
json_t *_jPlayerStatus( void )
{
  Playlist     *plst     = playlistGetPlayerQueue();
  int           cursorPos;
  time_t        pChange, aChange;
  json_t       *jResult; 	
  PlaylistItem *pItem;	
  	
/*------------------------------------------------------------------------*\
    Create status message
\*------------------------------------------------------------------------*/
  cursorPos = playlistGetCursorPos( plst );
  pChange   = playlistGetLastChange( plst );
  aChange   = audioGetLastChange( );
  jResult   = json_pack( "{sbsfsisfsbsf}",
  	                       "playing",     audioGetPlayingState(),
                           "seekPos",     audioGetSeekPos(),
                           "playlistPos", cursorPos,
                           "volumeLevel", audioGetVolume(), 
                           "muted",       audioGetMuting(),
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
static enum ickMessage_communicationstate _sendIckMessage( const char *szDeviceId, json_t *jMessage )
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
    srvmsg( LOG_DEBUG, "ickMessage (try %d) to %s: %s", i, szDeviceId?szDeviceId:"ALL", message );    
    result = ickDeviceSendMsg( szDeviceId, message, strlen(message) );
    if( result==ICKMESSAGE_SUCCESS )
      break;
    sleep( 1 );
  }
  
/*------------------------------------------------------------------------*\
    Clean up and that's all
\*------------------------------------------------------------------------*/
  free( message );
  return result;
}

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
