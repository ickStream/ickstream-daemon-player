/*$*********************************************************************\

Name            : -

Source File     : player.c

Description     : ickstream player model 

Comments        : This modul implements the ick player model and manages the 
                  playback chain consisting out of
                   - the feeder (data pump from source)
                   - the codec (transcodes the feeder stream to pcm)
                   - the audio interface (the hardware data sink)

Called by       : ickstream protocols 

Calls           : 

Error Messages  : -
  
Date            : 24.02.2013

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
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>
#include <jansson.h>

#include "utils.h"
#include "ickService.h"
#include "ickMessage.h"
#include "persist.h"
#include "playlist.h"
#include "feed.h"
#include "audio.h"
#include "player.h"

/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/

typedef enum {
  PlayerThreadNonexistent,
  PlayerThreadInitialized,
  PlayerThreadRunning,
  PlayerThreadTerminating,
  PlayerThreadTerminatedOk,
  PlayerThreadTerminatedError
} PlayerThreadState;

// Utilities
#define EffectiveVolume() (playerMuted?0.0:playerVolume)

 
/*=========================================================================*\
	Private symbols
\*=========================================================================*/

// persistent
static const char    *playerUUID;  
static const char    *playerInterface;  
static const char    *playerAudioDevice;  
static const char    *playerHWID;  
static const char    *playerModel;  
static const char    *playerName;
static const char    *accessToken;
static double         playerVolume;
static bool           playerMuted;
static Playlist      *playerQueue;

// transient
pthread_mutex_t       playerMutex;
static PlayerState    playerState = PlayerStateStop;
static double         lastChange;
static AudioIf       *audioIf;

// Playback thread
pthread_t             playbackThread;
PlayerThreadState     playbackThreadState = PlayerThreadNonexistent;
static char          *currentTrackId;
static CodecInstance *codecInstance;

// playback chain
//static AudioFeed     *currentFeed;



/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static void      *_playbackThread( void *arg );
static AudioFeed *_feedFromPlayListItem( PlaylistItem *item, Codec **codec );


/*=========================================================================*\
      Init player
\*=========================================================================*/
int playerInit( void )
{
  DBGMSG( "Initializing player module..." );
  
/*------------------------------------------------------------------------*\
    Get volume, safe default is volume 0 and unmuted 
\*------------------------------------------------------------------------*/
  playerVolume   = persistGetReal( "PlayerVolume" );
  playerMuted    = persistGetBool( "PlayerMuted" );  	

/*------------------------------------------------------------------------*\
    Load playlist
\*------------------------------------------------------------------------*/
  json_t *jQueue = persistGetJSON( "PlayerQueue" );
  playerQueue    = playlistFromJSON( jQueue );
  playlistSetCursorPos( playerQueue, persistGetInteger("PlayerQueuePosition") );

/*------------------------------------------------------------------------*\
    Init mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_init( &playerMutex, NULL );

/*------------------------------------------------------------------------*\
    Set timestamp 
\*------------------------------------------------------------------------*/
  lastChange = srvtime( );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Shut down player
\*=========================================================================*/
void playerShutdown( void )
{
  DBGMSG( "Shutting down player module..." );
  
/*------------------------------------------------------------------------*\
    Save playlist state
\*------------------------------------------------------------------------*/
  persistSetInteger( "PlayerQueuePosition", playlistGetCursorPos(playerQueue) );
  persistSetJSON_new( "PlayerQueue", playlistGetJSON(playerQueue,0,0) );

/*------------------------------------------------------------------------*\
    Delete mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &playerMutex );
}


/*=========================================================================*\
      Get playback queue (playlist) 
\*=========================================================================*/
Playlist *playerGetQueue( void )
{
  return playerQueue;
}


/*=========================================================================*\
      Clear player queue 
\*=========================================================================*/
void playerResetQueue( void )
{
  DBGMSG( "playerResetQueue" );
  playlistDelete( playerQueue );
  playerQueue = playlistNew();
}


/*=========================================================================*\
      Get timstamp of last  change 
\*=========================================================================*/
double playerGetLastChange( void )
{
  DBGMSG( "playerGetLastChange: %lf", lastChange );
  return lastChange;
}


/*=========================================================================*\
      Get playback state 
\*=========================================================================*/
PlayerState playerGetState( void )
{
  DBGMSG( "playerGetState: %d", playerState );
  return playerState;
}


/*=========================================================================*\
    Get player hardware ID
      return NULL if not suppored
      We use the MAC adress as default (network name must be set).
\*=========================================================================*/
const char *playerGetHWID( void )
{
  struct ifreq request;
  int    fd, i, retcode;
  char   buf[10], hwid[20];

/*------------------------------------------------------------------------*\
    Already initialized?
\*------------------------------------------------------------------------*/
  if( playerHWID )
    return playerHWID;
    
/*------------------------------------------------------------------------*\
    Create ioctl dtata structure and open an INET socket
\*------------------------------------------------------------------------*/
  const char *ifname = playerGetInterface();
  if( !ifname )
    return NULL;
  strncpy( request.ifr_name, ifname, IFNAMSIZ-1 );
  fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);

/*------------------------------------------------------------------------*\
    Perform request and close socket
\*------------------------------------------------------------------------*/
  retcode = ioctl( fd, SIOCGIFHWADDR, &request );
  close( fd );
  
/*------------------------------------------------------------------------*\
    Error?
\*------------------------------------------------------------------------*/
  if( retcode ) {
    srvmsg( LOG_ERR, "Error inquiring MAC from \"%s\": %s ", 
                     playerInterface, strerror(errno) );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Setup result
\*------------------------------------------------------------------------*/
  *hwid = 0;
  for( i=0; i<6; i++ ) {
    sprintf( buf, "%s%02X", i?":":"", (unsigned char)request.ifr_addr.sa_data[i] );
    strcat( hwid, buf );
  }
  
/*------------------------------------------------------------------------*\
    Store and return result
\*------------------------------------------------------------------------*/
  DBGMSG(  "playerGetHWID: \"%s\"", hwid );
  playerHWID = strdup( hwid );
  return playerHWID;
}


/*=========================================================================*\
    Get player UUID
\*=========================================================================*/
const char *playerGetUUID( void )
{
  if( !playerUUID )
    playerUUID = persistGetString( "DeviceUUID" );
  DBGMSG( "playerGetUUID: \"%s\"", playerUUID?playerUUID:"(null)" );
  return playerUUID;
}


/*=========================================================================*\
    Get player Model
\*=========================================================================*/
const char *playerGetModel( void )
{
  if( !playerModel )
    playerModel = "Generic UN*X Player";
  DBGMSG( "playerGetModel: \"%s\"", playerModel?playerModel:"(null)" );
  return playerModel;
}


/*=========================================================================*\
    Get network interface name
\*=========================================================================*/
const char *playerGetInterface( void )
{
  if( !playerInterface )
    playerInterface = persistGetString( "PlayerInterface" );
  DBGMSG( "playerGetInterface: \"%s\"", playerInterface?playerInterface:"(null)" );
  return playerInterface;
}


/*=========================================================================*\
    Get audio device name
\*=========================================================================*/
const char *playerGetAudioDevice( void )
{
  if( !playerAudioDevice )
    playerAudioDevice = persistGetString( "PlayerAudioDevice" );
  DBGMSG( "playerGetAudioDevice: \"%s\"", playerAudioDevice?playerAudioDevice:"(null)" );
  return playerAudioDevice;
}


/*=========================================================================*\
    Get player name
\*=========================================================================*/
const char *playerGetName( void )
{
  if( !playerName )
    playerName = persistGetString( "PlayerName" );
  DBGMSG( "playerGetName: \"%s\"", playerName?playerName:"(null)" );
  return playerName;
}


/*=========================================================================*\
    Get access token
\*=========================================================================*/
const char *playerGetToken( void )
{
  if( !accessToken )
    accessToken = persistGetString( "IckAccessToken" );
  DBGMSG( "playerGetToken: \"%s\"", accessToken?accessToken:"(null)" );
  return accessToken;
}


/*=========================================================================*\
    Set player UUID
\*=========================================================================*/
void playerSetUUID( const char *uuid )
{	
  srvmsg( LOG_INFO, "Setting player UUID to \"%s\"", uuid );
  playerUUID = uuid;  
  persistSetString( "DeviceUUID", uuid );
}

/*=========================================================================*\
    Set player Model
\*=========================================================================*/
void playerSetModel( const char *model )
{	
  srvmsg( LOG_INFO, "Setting player model to \"%s\"", model );
  if( playerModel ) {
    srvmsg( LOG_ERR, "Can set player model only once" );
    return;
  }
  playerModel = model;  
} 


/*=========================================================================*\
    Set player name
\*=========================================================================*/
void playerSetName( const char *name )
{
  srvmsg( LOG_INFO, "Setting player name to \"%s\"", name );
  playerName = name;  
  persistSetString( "PlayerName", name );
}


/*=========================================================================*\
    Set network interface name
\*=========================================================================*/
void playerSetInterface( const char *name )
{
  srvmsg( LOG_INFO, "Setting interface name to \"%s\"", name );
  if( playerInterface ) {
    srvmsg( LOG_ERR, "Can set interface name only once" );
    return;
  }
  playerInterface = name;  
  persistSetString( "PlayerInterface", name );
}


/*=========================================================================*\
    Set audio device name
\*=========================================================================*/
void playerSetAudioDevice( const char *name )
{
  srvmsg( LOG_INFO, "Setting audio device to \"%s\"", name );
  playerAudioDevice = name;  
  persistSetString( "PlayerAudioDevice", name );
}


/*=========================================================================*\
    Set access Token
\*=========================================================================*/
void playerSetToken( const char *token )
{
  srvmsg( LOG_INFO, "Setting ickstream access token to \"%s\"", token );
  accessToken = token;  
  persistSetString( "IckAccessToken", token );
}


/*=========================================================================*\
      Get Volume 
\*=========================================================================*/
double playerGetVolume( void )
{
  DBGMSG( "playerGetVolume: %.2lf%%", playerVolume*100 );
  return playerVolume;
}


/*=========================================================================*\
      Get Muting state 
\*=========================================================================*/
bool playerGetMuting( void )
{
  DBGMSG( "playerGetMuting: %s", playerMuted?"On":"Off" );
  return playerMuted;
}


/*=========================================================================*\
      Set Volume 
\*=========================================================================*/
double playerSetVolume( double volume )
{
  srvmsg( LOG_INFO, "Setting Volume to %lf", volume );

/*------------------------------------------------------------------------*\
    Clip value 
\*------------------------------------------------------------------------*/
  if( volume<0 )
    volume = 0;
  if( volume>1 )
    volume = 1;

/*------------------------------------------------------------------------*\
    Parametrize codec (if any )
\*------------------------------------------------------------------------*/
  if( codecInstance && codecSetVolume(codecInstance,EffectiveVolume()) )
    srvmsg( LOG_WARNING, "playerSetVolume: could not set volume to %.2lf%%", 
                         EffectiveVolume()*100 ); 

/*------------------------------------------------------------------------*\
    Update timestamp 
\*------------------------------------------------------------------------*/
  lastChange = srvtime( );

/*------------------------------------------------------------------------*\
    Store and return new volume 
\*------------------------------------------------------------------------*/
  playerVolume = volume;
  persistSetReal( "PlayerVolume", volume );
  return playerVolume;
}


/*=========================================================================*\
      Set Muting 
\*=========================================================================*/
bool playerSetMuting( bool muted )
{
  srvmsg( LOG_INFO, "Setting Muting to %s", muted?"On":"Off");

/*------------------------------------------------------------------------*\
    Parametrize codec (if any )
\*------------------------------------------------------------------------*/
  if( codecInstance && codecSetVolume(codecInstance,EffectiveVolume()) )
    srvmsg( LOG_WARNING, "playerSetMuting: could not set volume to %.2lf%%",
                         EffectiveVolume()*100  ); 

/*------------------------------------------------------------------------*\
    Update timestamp 
\*------------------------------------------------------------------------*/
  lastChange = srvtime( );

/*------------------------------------------------------------------------*\
    Store and return new state 
\*------------------------------------------------------------------------*/
  playerMuted = muted;
  persistSetBool( "playerMuted", muted );
  return playerMuted;
}


/*=========================================================================*\
      Get playback position 
\*=========================================================================*/
double playerGetSeekPos( void )
{
  double pos = 0;
    
/*------------------------------------------------------------------------*\
    Get Position from codec
\*------------------------------------------------------------------------*/
  if( codecInstance && codecGetSeekTime(codecInstance,&pos) )
    srvmsg( LOG_WARNING, "playerGetSeekPos: could not get seek time" );   

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  DBGMSG( "playerGetSeekPos: %.2lfs", pos );
  return pos;	
}


/*=========================================================================*\
      Change playback state 
\*=========================================================================*/
int playerSetState( PlayerState state )
{
  int           rc = 0;
  PlaylistItem *newTrack;
  
  DBGMSG( "playerSetState: %d -> %d", playerState, state );
  
/*------------------------------------------------------------------------*\
    Lock player, we don't want concurrent modifications going on...
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &playerMutex );
    
/*------------------------------------------------------------------------*\
    Switch on target state 
\*------------------------------------------------------------------------*/
  switch( state ) {
  	
/*------------------------------------------------------------------------*\
    Start or unpause playback
\*------------------------------------------------------------------------*/
    case PlayerStatePlay:

      // Try to setup audio interface if not yet done 
      if( !audioIf ) {
        const char         *device;
        const AudioBackend *backend = audioBackendByDeviceString( playerAudioDevice, &device );
        if( backend )
          audioIf = audioIfNew( backend, device );	
        if( !audioIf ) {
          srvmsg( LOG_ERR, "_playbackStart: Could not open audio device: %s", playerAudioDevice );
          rc = -1;
          break;
        }
      }
  	  
      // Is there a track to play ?
      newTrack = playlistGetCursorItem( playerQueue );
      if( !newTrack ) {
  	    srvmsg( LOG_NOTICE, "_playbackStart: Empty queue or no cursor" );
  	    rc = -1;
  	    break;
      }
      
      // Unpausing existing track ?
      if( playerState==PlayerStatePause && currentTrackId && !strcmp(newTrack->id,currentTrackId) ) {
  	    srvmsg( LOG_NOTICE, "_playbackStart: Track \"%s\" (%s) unpaused", 
                             newTrack->text, newTrack->id );
        playerState = PlayerStatePlay;
  	    rc = audioIfSetPause( audioIf, false );
  	    break;
      }
  	  
  	  // Need to stop running track?
  	  if( playerState==PlayerStatePlay ) {
  	  	if( !currentTrackId )
  	  	  srvmsg( LOG_ERR, "_playbackStart: internal error: playing but no current track." );
  	  	else 
          srvmsg( LOG_NOTICE, "_playbackStart: stopping current track (%s)", currentTrackId );
        if( playbackThreadState!=PlayerThreadRunning )
          srvmsg( LOG_ERR, "_playbackStart: internal error: playing but no running playback thread." );
        else {
          playbackThreadState = PlayerThreadTerminating;
          pthread_join( playbackThread, NULL ); 
        }
  	  }
  	  
      // Create new playback thread
      rc = pthread_create( &playbackThread, NULL, _playbackThread, NULL );
      if( rc ) {
        srvmsg( LOG_ERR, "_playbackStart: Unable to start thread: %s", strerror(rc) );
        rc = -1;
      }
      break;

/*------------------------------------------------------------------------*\
    Stop playback
\*------------------------------------------------------------------------*/
    case PlayerStateStop:

      // not yet initialized?
      if( !audioIf ) {
        srvmsg( LOG_ERR, "_playbackStop: audio device not yet initialized." );
        rc = -1;
        break;
      }

      // not playing
      if( playerState!=PlayerStatePlay  && playerState!=PlayerStatePause ) {
        srvmsg( LOG_WARNING, "_playbackStop: not playing or no thread before stop (state %d)", playerState );
        playerState = PlayerStateStop;
      }

      // request thread to stop playback.
      playbackThreadState = PlayerThreadTerminating;
      break; 
  	
/*------------------------------------------------------------------------*\
    Pause playback
\*------------------------------------------------------------------------*/
    case PlayerStatePause:

     // not yet initialized?
      if( !audioIf ) {
        srvmsg( LOG_ERR, "_playbackPause: audio device not yet initialized." );
      	rc = -1;
      	break;
      } 

      // Check for right state
      if( playerState==PlayerStateStop )
        srvmsg( LOG_WARNING, "_playbackPause: cannot pause stopped playback" );
      else if( playerState==PlayerStatePause )
        srvmsg( LOG_WARNING, "_playbackPause: pause already paused layback" );

      // Pause audio output and set state
      else {
        srvmsg( LOG_NOTICE, "_playbackPause: playback paused" );
        rc = audioIfSetPause( audioIf, true );
        if( !rc )
          playerState = PlayerStatePause;
      }
      break;
      
/*------------------------------------------------------------------------*\
    Unknown command
\*------------------------------------------------------------------------*/
    default:
      srvmsg( LOG_ERR, "playerSetState: Unknown tagret state %d", state );
      rc = -1;
      break;
  }

/*------------------------------------------------------------------------*\
    Update timestamp and unlock player
\*------------------------------------------------------------------------*/
  lastChange = srvtime( );
  pthread_mutex_unlock( &playerMutex );

/*------------------------------------------------------------------------*\
    That's it 
\*------------------------------------------------------------------------*/
  return rc;
}


/*=========================================================================*\
      Player thread
\*=========================================================================*/
static void *_playbackThread( void *arg )
{
  PlaylistItem *item;

/*------------------------------------------------------------------------*\
    Loop over player queue 
\*------------------------------------------------------------------------*/
  playbackThreadState = PlayerThreadRunning; 
  item = playlistGetCursorItem( playerQueue );
  while( item && playbackThreadState==PlayerThreadRunning ) {
    AudioFeed     *feed;
    Codec         *codec;
    CodecInstance *codecInst;   // mirrored to global variable codecInstance
    Fifo          *fifo;
 
    DBGMSG( "_playerThread: Starting track \"%s\" (%s)", item->text, item->id );
 
    // Try to get feed and codec for new track, if negative skip queue item
    feed = _feedFromPlayListItem( item, &codec );
    if( !feed ) {
      srvmsg( LOG_NOTICE, "_playerThread: Track \"%s\" (%s) unavailable or unsupported by audio module", 
                          item->text, item->id );
      item = playlistIncrCursorItem( playerQueue );
      continue;
    }

    // Get fifo from audio interface
    // Fixme: use fallback format if feed format is not supported...
    fifo = audioIfPlay( audioIf, &feed->format );
    if( !fifo  ) {
      srvmsg( LOG_ERR, "_playerThread: Cannot setup audio device \"%s\" to format %s", 
                        audioIf->devName, audioFormatStr(&feed->format) );
      audioFeedDelete( feed , true );
      playbackThreadState = PlayerThreadTerminatedError;
      break;
    }

    // create codec instance
    codecInst = codecNewInstance( codec, fifo, &feed->format );
    if( !codecInst ) {
      srvmsg( LOG_ERR, "_playerThread: could not get instance of codec %s for format %s", 
                       codec->name, audioFormatStr(&feed->format) );
      audioFeedDelete( feed, true );
      playbackThreadState = PlayerThreadTerminatedError;
      break;
    }
    
    // initialize volume
    if( codecSetVolume(codecInst,EffectiveVolume()) )
      srvmsg( LOG_WARNING, "_playerThread: could not set volume to %.2lf%%", 
                           EffectiveVolume()*100 ); 
 
    // Attach feed to codec and start...
    if( audioFeedStart(feed,codecInst) ) {
      srvmsg( LOG_ERR, "_playerThread: Could not start feed \"%s\" on codec %s", 
                         feed->uri, codecInst->codec->name );
      audioFeedDelete( feed, true );
      codecDeleteInstance( codecInst, true );
      playbackThreadState = PlayerThreadTerminatedError;
      break;
    }
    
    // Save new track id and brodcast new player status
    Sfree( currentTrackId );
    currentTrackId = strdup( item->id );
    codecInstance = codecInst;
    ickMessageNotifyPlayerState();
    srvmsg( LOG_NOTICE, "_playerThread: Playing track \"%s\" (%s)", 
      	                item->text, item->id );
      	                
    // Wait for end of feed or stop condition ...
    while( playbackThreadState==PlayerThreadRunning ) {
      int rc = codecWaitForEnd( codecInst, 250 );
      if( !rc )
        break;
      if( rc<0 )
        playbackThreadState = PlayerThreadTerminatedError;
    }
 
    // Get rid of feed
    if( feed && audioFeedDelete(feed,true) )
      srvmsg( LOG_ERR, "_playerThread: Could not delete feeder instance" );
 
    // Get rid of codec
    codecInstance = NULL;
    if( codecDeleteInstance(codecInst,true) )
      srvmsg( LOG_ERR, "_playerThread: Could not delete codec instance" );
     	
    // Goto next playlist entry 	
    item = playlistIncrCursorItem( playerQueue );
  }  // End of: Thread main loop
 
/*------------------------------------------------------------------------*\
    Stop audio interface
\*------------------------------------------------------------------------*/
  if( audioIfStop(audioIf,playbackThreadState==PlayerThreadRunning?AudioDrain:AudioDrop) )
    srvmsg( LOG_ERR, "_playerThread: Could not stop audio interface %s", audioIf->devName );
  if( playbackThreadState==PlayerThreadRunning )  
    srvmsg( LOG_NOTICE, "_playerThread: End of playlist." );

/*------------------------------------------------------------------------*\
    Set player state and broadcast change
\*------------------------------------------------------------------------*/
  Sfree( currentTrackId );
  playerState = PlayerStateStop;
  ickMessageNotifyPlayerState();
 
/*------------------------------------------------------------------------*\
    That's it ...  
\*------------------------------------------------------------------------*/
  DBGMSG( "Player thread: terminated due to state %d", playbackThreadState );
  return NULL;
}


/*=========================================================================*\
      Get an audio feed for an playlist item 
        return Feed on success, NULL on error
        *codec is set to the first matching codec
        fifo is the output buffer for data delivery
\*=========================================================================*/
static AudioFeed *_feedFromPlayListItem( PlaylistItem *item, Codec **codec ) 
{
  int i;

/*------------------------------------------------------------------------*\
    Loop over all stream hints 
\*------------------------------------------------------------------------*/
  for( i=0; i<json_array_size(item->jStreamingRefs); i++ ) {
    json_t          *jStreamRef = json_array_get( item->jStreamingRefs, i );
    json_t          *jObj;
    AudioFormat      format;
    AudioFeed       *feed;
    const char      *type;
    char            *uri;
        
    // Get type
    jObj = json_object_get( jStreamRef, "format" );
    if( !jObj || !json_is_string(jObj) ) {
      srvmsg( LOG_ERR, "StreamRef #%d for track \"%s\" (%s): no format!", 
                       i, item->text, item->id );
      continue;
    }
    type = json_string_value( jObj );
        
    // Get URI
    jObj = json_object_get( jStreamRef, "url" );
    if( !jObj || !json_is_string(jObj) ) {
      srvmsg( LOG_ERR, "StreamRef #%d for track \"%s\" (%s): no url!", 
                           i, item->text, item->id );
      continue;
    }  
      
    // Try to resolve the URI (will allocate the string)
    uri = ickServiceResolveURI( json_string_value(jObj), "content" );
    if( !uri ) {
      srvmsg( LOG_NOTICE, "StreamRef #%d for track \"%s\" (%s): cannot resolve URL \"%s\"!", 
              i, item->text, item->id, json_string_value(jObj) );
      continue;  
    }
          
    // Get sample rate (optional)
    jObj = json_object_get( jStreamRef, "sampleRate" );
    if( jObj && json_is_integer(jObj) )
      format.sampleRate = json_integer_value( jObj );
    else if( jObj && json_is_string(jObj) )    // workaround
      format.sampleRate = atoi( json_string_value(jObj) );
    else
      format.sampleRate = -1;
  
    // Get number of channels (optional)
    jObj = json_object_get( jStreamRef, "channels" );
    if( jObj && json_is_integer(jObj) )
      format.channels = json_integer_value( jObj );
    else if( jObj && json_is_string(jObj) )    // workaround
      format.channels = atoi( json_string_value(jObj) );
    else
      format.channels = -1;

    // Get first codec matching type and mode 
    *codec = codecFind( type, &format, NULL );
    if( !*codec ) {
      DBGMSG( "No codec found for (%s) with %s", 
              type, audioFormatStr(&format)  );	
      Sfree( uri );
  	  continue;
    }

    // Try to open the feed 
    feed = audioFeedCreate( uri, type, &format );
    if( !feed )
      DBGMSG( "StreamRef \"%s\" (%s) with (%d*%d): could not open.", 
              uri, type, format.sampleRate, format.channels  );	
    Sfree( uri );
    
    // We have found a feed...
    if( feed )
      return feed;
  }

  return NULL;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
