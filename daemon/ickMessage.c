/*$*********************************************************************\

Name            : -

Source File     : ickMessage.c

Description     : implement ickstream device related  protocol 

Comments        : -

Called by       : ickstream wrapper 

Calls           : 

Error Messages  : -
  
Date            : 20.02.2013

Updates         : 03.04.2013 implemented JSON-RPC error handling  //MAF

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
#include "hmi.h"
#include "ickMessage.h"
#include "ickCloud.h"
#include "ickService.h"
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
  long                  id;
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
  json_t       *rpcId;
  int           rpcErrCode      = RPC_NO_ERROR;
  char         *rpcErrMessage   = "Generic Error";
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
    logerr( "ickMessage from %s: corrupt line %d: %s",
             szDeviceId, error.line, error.text );
    rpcErrCode    = RPC_INVALID_REQUEST;
    rpcErrMessage = "Could not parse";
    goto rpcError;
  }
  if( !json_is_object(jRoot) ) {
    logerr( "ickMessage from %s: could not parse to object: %s",
            szDeviceId, message );
    rpcErrCode    = RPC_INVALID_REQUEST;
    rpcErrMessage = "Parse did not result in JSON object";
    goto rpcError;
  } 
  // DBGMSG( "ickMessage from %s: parsed.", szDeviceId );
  
/*------------------------------------------------------------------------*\
    Get request ID
\*------------------------------------------------------------------------*/
  rpcId = json_object_get( jRoot, "id" );
  if( !rpcId ) {
    logerr( "ickMessage from %s contains no id: %s", szDeviceId, message );
    rpcErrCode    = RPC_INVALID_REQUEST;
    rpcErrMessage = "RPC header contains no Id";
    goto rpcError;
  }
  // DBGMSG( "ickMessage from %s: found id.", szDeviceId );

/*------------------------------------------------------------------------*\
    Check for result or error field: this is an answer to a command we've issued
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jRoot, "result" );
  if( !jObj )
    jObj = json_object_get( jRoot, "error" );
  if( jObj && json_is_object(jObj) ) {
    OpenRequest *request = openRequestList;
    long id;
    
    // Get integer Id from message
    if( json_is_integer(rpcId) )
      id = json_integer_value( rpcId );
    else if( json_is_string(rpcId) ) {
#ifdef ICK_DEBUG
      logwarn( "ickMessage from %s returned id as string: %s", szDeviceId, message );
#endif
      id = atol( json_string_value(rpcId) );
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
    rpcErrCode    = RPC_INVALID_REQUEST;
    rpcErrMessage = "RPC header contains no method, result or error field";
    goto rpcError;
  }
  method = json_string_value( jObj );
  DBGMSG( "ickMessage from %s: Executing command \"%s\"", szDeviceId, method );

  jParams = json_object_get( jRoot, "params" );

/*------------------------------------------------------------------------*\
    Get player status 
\*------------------------------------------------------------------------*/
  if( !strcasecmp(method,"getPlayerStatus") ) {
    jResult = _jPlayerStatus();
  }

/*------------------------------------------------------------------------*\
    Get position in track
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getSeekPosition") ) {
  	Playlist *plst = playerGetQueue();
    playlistLock( plst );
  	jResult = json_pack( "{sisf}",
                         "playlistPos", playlistGetCursorPos(plst),
                         "seekPos",     playerGetSeekPos() );
    playlistUnlock( plst );
  }

/*------------------------------------------------------------------------*\
    Get track info
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getTrack") ) {
    Playlist     *plst = playerGetQueue();
    PlaylistItem *pItem;
    int           pos;
    playlistLock( plst );

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

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
    if( pItem ) {
      playlistItemLock( pItem );
      json_object_set( jResult, "track", playlistItemGetJSON(pItem) );
      playlistItemUnlock( pItem );
    }

    playlistUnlock( plst );
  }  

/*------------------------------------------------------------------------*\
    Set repeat mode
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"setRepeatMode") ) {
    PlayerRepeatMode   mode;

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

    // Get mode
    jObj = json_object_get( jParams, "repeatMode" );
    if( !jObj || !json_is_string(jObj) )  {
      logerr( "ickMessage from %s: missing field \"repeatMode\": %s",
                       szDeviceId, message );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"repeatMode\": missing or of wrong type";
      goto rpcError;
    }
    mode = playerRepeatModeFromStr( json_string_value(jObj) );
    if( mode<0 ) {
      logerr( "ickMessage from %s: unknown repeat mode: %s",
                       szDeviceId, json_string_value(jObj) );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"repeatMode\": invalid value";
      goto rpcError;
    }

    // Set and broadcast player mode to account for skipped tracks
    playerSetRepeatMode( mode, true );

    // report current state
    jResult = json_pack( "{si}",
                         "repeatMode", playerRepeatModeToStr(playerGetRepeatMode()) );
  }

/*------------------------------------------------------------------------*\
    Set track index to play 
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"setTrack") ) {
    Playlist *plst   = playerGetQueue();
    int       offset = 0;

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

    // Get position
    jObj = json_object_get( jParams, "playlistPos" );
    if( !jObj || !json_is_integer(jObj) )  {
      logerr( "ickMessage from %s: missing field \"playlistPos\": %s", 
                       szDeviceId, message );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"playlistPos\": missing or of wrong type";
      goto rpcError;
    }
    offset = json_integer_value( jObj );

    // Change pointer in playlist
    playlistLock( plst );
    playlistSetCursorPos( plst, offset );
    playlistUnlock( plst );

    // Set and broadcast player mode to account for skipped tracks
    playerSetState( playerGetState(), true );
       
    // report current state
    playlistLock( plst );
    jResult = json_pack( "{si}",
                         "playlistPos", playlistGetCursorPos(plst) );
    playlistUnlock( plst );
  }

/*------------------------------------------------------------------------*\
    Play or pause 
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"play") ) {
    PlayerState newState;

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

    // Get mode
    jObj = json_object_get( jParams, "playing" );
    if( !jObj || !json_is_boolean(jObj) )  {
      logerr( "ickMessage from %s: missing field \"playing\": %s", 
                       szDeviceId, message );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"playing\": missing or of wrong type";
      goto rpcError;
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
    double volume = playerGetVolume();
    bool   muted  = playerGetMuting();

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

    // Get and set muting state
    muted = playerGetMuting();
    jObj = json_object_get( jParams, "muted" );
    if( jObj && json_is_boolean(jObj) )
      muted = json_is_true( jObj );

    // Set volume relative to current value
    jObj = json_object_get( jParams, "relativeVolumeLevel" );
    if( jObj && json_is_real(jObj) )
      volume *= 1 + json_real_value( jObj );

    // Set volume absultely
    jObj = json_object_get( jParams, "volumeLevel" );
    if( jObj && json_is_real(jObj) )
      volume = json_real_value( jObj );

    // Set Volume
    playerSetVolume( volume, muted, true );

    // report current state
    jResult = json_pack( "{sfsb}",
                         "volumeLevel", playerGetVolume(),
                         "muted",       playerGetMuting() );                         
  }

/*------------------------------------------------------------------------*\
    Get playback queue
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"getPlaylist") ) {
    Playlist *plst   = playerGetQueue();
    int       offset = 0;
    int       count  = 0;

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

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
    playlistLock( plst );
    jResult = playlistGetJSON( plst, offset, count ); 
    playlistUnlock( plst );
  }

/*------------------------------------------------------------------------*\
    Set playback queue id and name
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"setPlaylistName") ) {
    Playlist   *plst = playerGetQueue();
    const char *id   = NULL;
    const char *name = NULL;

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

    // Get ID
    jObj = json_object_get( jParams, "playlistId" );
    if( !jObj || !json_is_string(jObj) )  {
      logerr( "ickMessage from %s: missing field \"playlistId\": %s", 
              szDeviceId, message );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"playListId\": missing or of wrong type";
      goto rpcError;
    }
    id = json_string_value( jObj );
    
    // Get name
    jObj = json_object_get( jParams, "playlistName" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "ickMessage from %s: missing field \"playlistName\": %s", 
                       szDeviceId, message );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"playListName\": missing or of wrong type";
      goto rpcError;
    }
    name = json_string_value( jObj );

    // Set id and name
    playlistLock( plst );
    playlistSetId( plst, id );
    playlistSetName( plst, name );

    // Playlist has changed
    playlistChanged = true;

    // report result 
    jResult = json_pack( "{sssssi}",
                         "playlistId", id, 
                         "playlistName", name, 
                         "countAll", playlistGetLength(plst) );

    playlistUnlock( plst );
  }

/*------------------------------------------------------------------------*\
    Add tracks to playback queue or replace playback queue
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"addTracks") || 
           !strcasecmp(method,"setTracks") ) {
    Playlist *plst      = playerGetQueue();
    bool      resetFlag = strcasecmp(method,"setTracks") ? false : true;
    int       pos       = -1;        // Item to add list before
    json_t   *jItems;                 // List of new items
    int       result = 1;

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

    // Get explicit position
    jObj = json_object_get( jParams, "playlistPos" );
    if( jObj && json_is_integer(jObj) )
      pos    = json_integer_value( jObj );
    
    // Get list of new items
    jItems = json_object_get( jParams, "items" );
    if( !jItems || !json_is_array(jItems) ) {
      logerr( "ickMessage from %s: missing field \"items\": %s", 
               szDeviceId, message );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"items\": missing or of wrong type";
      goto rpcError;
    }

    // Add tracks to playback queue
    playlistLock( plst );
    if( playlistAddItems(plst,pos,jItems,resetFlag) ) {
      logerr( "ickMessage from %s: could not add items to playlist: %s", 
               szDeviceId, message );
      playlistUnlock( plst );
      result = 0;
    }

    // Playback queue has changed
    playlistChanged = true;

    // report result 
    jResult = json_pack( "{sbsi}",
                         "result", result,
                         "playlistPos", playlistGetCursorPos(plst) );

    playlistUnlock( plst );
  }

/*------------------------------------------------------------------------*\
    Remove tracks from playback queue
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"removeTracks") ) {
    Playlist *plst = playerGetQueue();
    json_t   *jItems;
    int       result = 1;

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

    // Get list of items to be removed
    jItems = json_object_get( jParams, "items" );
    if( !jItems || !json_is_array(jItems) ) {
      logerr( "ickMessage from %s: missing field \"items\": %s", 
                       szDeviceId, message );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"items\": missing or of wrong type";
      goto rpcError;
    }
    
    // Remove items from playlist
    playlistLock( plst );
    if( playlistDeleteItems(plst,jItems) ) {
      logerr( "ickMessage from %s: could not remove items from playlist: %s", 
               szDeviceId, message );
      playlistUnlock( plst );
      result = 0;
    }

    // Playback queue has changed
    playlistChanged = true;

    // report result 
    jResult = json_pack( "{sbsi}",
                         "result", result,
                         "playlistPos", playlistGetCursorPos(plst) );
    playlistUnlock( plst );
  }

/*------------------------------------------------------------------------*\
    Move tracks within playback queue
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"moveTracks") ) {
    Playlist      *plst    = playerGetQueue();
    json_t        *jItems;
    int            pos     = -1;        // Item to add list before
    int            result  = 1;

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

    // Get explicite position 
    jObj = json_object_get( jParams, "playlistPos" );
    if( jObj && json_is_integer(jObj) )
      pos = json_integer_value( jObj );
    
    // Get list of items to move
    jItems = json_object_get( jParams, "items" );
    if( !jItems || !json_is_array(jItems) ) {
      logerr( "ickMessage from %s: missing field \"items\": %s", 
                       szDeviceId, message );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"items\": missing or of wrong type";
      goto rpcError;
    }

    // Move tracks within playlist
    playlistLock( plst );
    if( playlistMoveItems(plst,pos,jItems) ) {
      logerr( "ickMessage from %s: could not move items in playlist: %s", 
               szDeviceId, message );
      playlistUnlock( plst );
      result = 0;
    }

    // Playback queue has changed
    playlistChanged = true;
      
    // report result 
    jResult = json_pack( "{sbsi}",
                         "result", result,
                         "playlistPos", playlistGetCursorPos(plst) );

    playlistUnlock( plst );
  } 

/*------------------------------------------------------------------------*\
   Shuffle playback queue
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"shuffleTracks") ) {
    Playlist      *plst    = playerGetQueue();
    int            result  = 1;

    playlistLock( plst );
    int            rangeStart = 0;
    int            rangeEnd   = playlistGetLength( plst )-1;

    // Get explicit positions
    if( jParams ) {
      jObj = json_object_get( jParams, "playlistStartPos" );
      if( jObj && json_is_integer(jObj) )
        rangeStart = json_integer_value( jObj );
      jObj = json_object_get( jParams, "playlistEndPos" );
      if( jObj && json_is_integer(jObj) )
        rangeEnd = json_integer_value( jObj );
    }

    // Do the shuffling
    if( rangeStart<rangeEnd ) {
      if( !playlistShuffle(plst,rangeStart,rangeEnd,true) )
        result = 0;

      // Playback queue has changed
      playlistChanged = true;
    }

    // report result
    jResult = json_pack( "{sbsi}",
                         "result", result,
                         "playlistPos", playlistGetCursorPos(plst) );

    playlistUnlock( plst );
  }

/*------------------------------------------------------------------------*\
   Modify data of a playback queue item
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"setTrackMetadata") ) {
    Playlist      *plst        = playerGetQueue();
    int            pos;
    bool           replaceFlag = true;
    PlaylistItem  *pItem;
    int            result     = 1;

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

    // Lock playlist and get default position
    playlistLock( plst );
    pos = playlistGetCursorPos( plst );

    // Get explicit position
    jObj = json_object_get( jParams, "playlistPos" );
    if( jObj && json_is_integer(jObj) )
      pos = json_integer_value( jObj );

    // Get replace flag
    jObj = json_object_get( jParams, "replace" );
    if( jObj && json_is_boolean(jObj) )
      replaceFlag = json_is_true(jObj) ? true : false;

    // Get new meta data
    jObj = json_object_get( jParams, "track" );
    if( !jObj || !json_is_object(jObj) ) {
      logerr( "ickMessage from %s: missing field \"track\": %s",
                       szDeviceId, message );
      playlistUnlock( plst );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"track\": missing or of wrong type";
      goto rpcError;
    }

    // Address item of interest
    pItem = playlistGetItem( plst, pos );
    if( !pItem ) {
      logwarn( "ickMessage from %s: no item found at queue position %d: %s",
               szDeviceId, pos, message );
      result = 0;
    }

    // Modify meta data
    else {
      playlistItemLock( pItem );
      if( playlistItemSetMetaData(pItem,jObj,replaceFlag) )
        result = 0;
      playlistItemUnlock( pItem );
      playlistChanged = true;
    }

    // report result
    jResult = json_pack( "{sb}",
                         "result", result );
    if( pItem ) {
      playlistItemLock( pItem );
      json_object_set_new( jResult, "track", playlistItemGetJSON(pItem) );
      playlistItemUnlock( pItem );
    }

    playlistUnlock( plst );
  }

/*------------------------------------------------------------------------*\
    Set player configuration
\*------------------------------------------------------------------------*/
  else if( !strcasecmp(method,"setPlayerConfiguration") ) {

    // Expect parameters
    if( !jParams || !json_is_object(jParams) ) {
      logerr( "ickMessage from %s contains no parameters: %s", szDeviceId, message );
      rpcErrCode    = RPC_INVALID_REQUEST;
      rpcErrMessage = "Missing parameters in RPC header";
      goto rpcError;
    }

    // Get player name (required)
    jObj = json_object_get( jParams, "playerName" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "ickMessage from %s: item missing field \"playerName\": %s", 
                       szDeviceId, message );
      rpcErrCode    = RPC_INVALID_PARAMS;
      rpcErrMessage = "Parameter \"playerName\": missing or of wrong type";
      goto rpcError;
    } 
    playerSetName( json_string_value(jObj), true );

    // Get access token (optional)
    jObj = json_object_get( jParams, "accessToken" );
    if( jObj && json_is_string(jObj) )
      playerSetToken( json_string_value(jObj) );

    // Register with the cloud core
    ickCloudSetDeviceAddress( );

    // Get services using this token
    ickServiceAddFromCloud( NULL, true );

    // Inform HMI
    hmiNewConfig( );

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
    rpcErrCode = RPC_METHOD_NOT_FOUND;
    rpcErrMessage = "Method not found";
  }

/*------------------------------------------------------------------------*\
   Return error
\*------------------------------------------------------------------------*/
rpcError:
  if( rpcErrCode!=RPC_NO_ERROR ) {
    logwarn( "ickMessage from %s: error code %d (%s) (message was: %s)",
             szDeviceId, rpcErrCode, rpcErrMessage, message);
    jResult = json_pack( "{siss}",
                         "code",    rpcErrCode,
                         "message", rpcErrMessage);
    jResult = json_pack( "{ssso}",
                         "jsonrpc", "2.0",
                         "error",   jResult);
    if( rpcId )
      json_object_set_new( jResult, "id", rpcId );
  }

/*------------------------------------------------------------------------*\
   Return nominal result
\*------------------------------------------------------------------------*/
  else if( jResult ) {
    jResult = json_pack( "{sssoso}",
                         "jsonrpc", "2.0", 
                         "id",     rpcId,
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
    ickMessageNotifyPlaylist( NULL );

/*------------------------------------------------------------------------*\
    Clean up: Free JSON message object and check for timedout requests
\*------------------------------------------------------------------------*/
  if( jRoot )
    json_decref( jRoot );
  Sfree( message );
  _timeoutOpenRequest( 60 );
}


/*=========================================================================*\
  Send a notification for playlist update
\*=========================================================================*/
void ickMessageNotifyPlaylist( const char *szDeviceId )
{
  Playlist   *plst;
  json_t     *jMsg;
  const char *str;

  DBGMSG( "ickMessageNotifyPlaylist: %s.", szDeviceId?szDeviceId:"ALL" );

/*------------------------------------------------------------------------*\
    Set up parameters
\*------------------------------------------------------------------------*/
  plst = playerGetQueue( );
  playlistLock( plst );
  jMsg = json_pack( "{sf si}",
                      "lastChanged", (double) playlistGetLastChange(plst), 
                      "countAll", playlistGetLength(plst) );
  // Name and ID are optional                       
  str = playlistGetId( plst );
  if( str )
    json_object_set_new( jMsg, "playlistId", json_string(str) );
  str = playlistGetName( plst );
  if( str )
    json_object_set_new( jMsg, "playlistName", json_string(str) );
  playlistUnlock( plst );

/*------------------------------------------------------------------------*\
    Set up message
\*------------------------------------------------------------------------*/
  jMsg = json_pack( "{ss ss so}",
                    "jsonrpc", "2.0",
                    "method", "playlistChanged",
                    "params", jMsg );
                        
/*------------------------------------------------------------------------*\
    Broadcast and clean up
\*------------------------------------------------------------------------*/
  sendIckMessage( szDeviceId, jMsg );
  json_decref( jMsg );
}


/*=========================================================================*\
  Send a notification for plyer status update
\*=========================================================================*/
void ickMessageNotifyPlayerState( const char *szDeviceId )
{
  json_t *jMsg;
  
  DBGMSG( "ickMessageNotifyPlayerState: %s.", szDeviceId?szDeviceId:"ALL" );
  
/*------------------------------------------------------------------------*\
    Get player state
\*------------------------------------------------------------------------*/
  jMsg = _jPlayerStatus();     

/*------------------------------------------------------------------------*\
    Set up message
\*------------------------------------------------------------------------*/
  jMsg = json_pack( "{ss ss so}",
                    "jsonrpc", "2.0", 
                    "method", "playerStatusChanged",
                    "params", jMsg );
                   
/*------------------------------------------------------------------------*\
    Broadcast and clean up
\*------------------------------------------------------------------------*/
  sendIckMessage( szDeviceId, jMsg );
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
  playlistLock( plst );

  DBGMSG( "_PlayerStatus." );

/*------------------------------------------------------------------------*\
    Create status message
\*------------------------------------------------------------------------*/
  cursorPos = playlistGetCursorPos( plst );
  pChange   = playlistGetLastChange( plst );
  aChange   = playerGetLastChange( );
  jResult   = json_pack( "{sbsfsisfsbsssf}",
                           "playing",     playerGetState()==PlayerStatePlay,
                           "seekPos",     playerGetSeekPos(),
                           "playlistPos", cursorPos,
                           "volumeLevel", playerGetVolume(), 
                           "muted",       playerGetMuting(),
                           "repeatMode",  playerRepeatModeToStr(playerGetRepeatMode()),
                           "lastChanged", MAX(aChange,pChange) );

/*------------------------------------------------------------------------*\
    Add info about current track (if any)
\*------------------------------------------------------------------------*/
  pItem = playlistGetItem( plst, cursorPos );
  if( pItem ) {
    playlistItemLock( pItem );
    json_object_set( jResult, "track", playlistItemGetJSON(pItem) );
    playlistItemUnlock( pItem );
  }
    
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  playlistUnlock( plst );
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

/*------------------------------------------------------------------------*\
    Create an init list element for callbacks
\*------------------------------------------------------------------------*/
  OpenRequest *request = calloc( 1, sizeof(OpenRequest) );
  if( !request )
    return -1;
    
  request->szDeviceId = strdup( szDeviceId );  
  request->id         = getAndIncrementCounter();
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
    Free resources
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



