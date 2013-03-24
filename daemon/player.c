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
#include "hmi.h"

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

 
/*=========================================================================*\
	Private symbols
\*=========================================================================*/

// persistent
static const char         *playerUUID;  
static const char         *playerInterface;  
static const char         *playerAudioDevice;  
static const char         *playerHWID;  
static const char         *playerModel;  
static const char         *playerName;
static const char         *accessToken;
static double              playerVolume;
static bool                playerMuted;
static Playlist           *playerQueue;
static PlayerRepeatMode    playerRepeatMode = PlayerRepeatOff;
static AudioFormatList     defaultAudioFormats;

// transient
pthread_mutex_t            playerMutex;
static PlayerState         playerState = PlayerStateStop;
static double              lastChange;
static AudioIf            *audioIf;

// Playback thread
pthread_t                  playbackThread;
PlayerThreadState          playbackThreadState;
static char               *currentTrackId;
static CodecInstance      *codecInstance;


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static int        _playerSetVolume( double volume, bool muted );
static void      *_playbackThread( void *arg );
static AudioFeed *_feedFromPlayListItem( PlaylistItem *item, Codec **codec );


/*=========================================================================*\
      Init player
\*=========================================================================*/
int playerInit( void )
{
  DBGMSG( "Initializing player module..." );
  
/*------------------------------------------------------------------------*\
    Init state. 
\*------------------------------------------------------------------------*/
  playerState = PlayerStateStop;

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
    Get repeat mode
\*------------------------------------------------------------------------*/
  playerRepeatMode = persistGetInteger( "PlayerRepeatMode" );

/*------------------------------------------------------------------------*\
    Init mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_init( &playerMutex, NULL );

/*------------------------------------------------------------------------*\
    Inform HMI and set timestamp 
\*------------------------------------------------------------------------*/
  hmiNewItem( playerQueue, playlistGetCursorItem(playerQueue) );
  hmiNewState( playerState );
  hmiNewVolume( playerVolume, playerMuted );
  hmiNewRepeatMode( playerRepeatMode );
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

/*------------------------------------------------------------------------*\
    Delete list of default audio formats
\*------------------------------------------------------------------------*/
  audioFreeAudioFormatList( &defaultAudioFormats );
}


/*=========================================================================*\
    Add default audio format
      if format is NULL the default list is deleted
\*=========================================================================*/
int playerAddDefaultAudioFormat( const AudioFormat *format )
{
  DBGMSG( "Adding default audio format: %s", 
          format?audioFormatStr(NULL,format):"<none>" );

/*------------------------------------------------------------------------*\
    Delete list?
\*------------------------------------------------------------------------*/
  if( !format ) {
    DBGMSG( "Deleting all default audio format list entries." );
    audioFreeAudioFormatList( &defaultAudioFormats );
    return 0;
  }

/*------------------------------------------------------------------------*\
    Add format to list, that's it
\*------------------------------------------------------------------------*/
  return audioAddAudioFormat( &defaultAudioFormats, format );
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
      Get repeat mode 
\*=========================================================================*/
PlayerRepeatMode playerGetRepeatMode( void )
{
  DBGMSG( "playerGetRepeatMode: %d", playerRepeatMode );
  return playerRepeatMode;
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
    logerr( "Error inquiring MAC from \"%s\": %s ", 
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
      Get playback position 
\*=========================================================================*/
double playerGetSeekPos( void )
{
  double pos = 0;
    
/*------------------------------------------------------------------------*\
    Get Position from codec
\*------------------------------------------------------------------------*/
  if( codecInstance && codecGetSeekTime(codecInstance,&pos) )
    logwarn( "playerGetSeekPos: could not get seek time" );   

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  DBGMSG( "playerGetSeekPos: %.2lfs", pos );
  return pos;	
}


/*=========================================================================*\
    Set player UUID
\*=========================================================================*/
void playerSetUUID( const char *uuid )
{	
  loginfo( "Setting player UUID to \"%s\"", uuid );
  playerUUID = uuid;  
  persistSetString( "DeviceUUID", uuid );
}


/*=========================================================================*\
    Set player model
\*=========================================================================*/
void playerSetModel( const char *model )
{	
  loginfo( "Setting player model to \"%s\"", model );
  if( playerModel ) {
    logerr( "Can set player model only once" );
    return;
  }
  playerModel = model;  
} 


/*=========================================================================*\
    Set network interface name
\*=========================================================================*/
void playerSetInterface( const char *name )
{
  loginfo( "Setting interface name to \"%s\"", name );
  if( playerInterface ) {
    logerr( "Can set interface name only once" );
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
  loginfo( "Setting audio device to \"%s\"", name );
  playerAudioDevice = name;  
  persistSetString( "PlayerAudioDevice", name );
}


/*=========================================================================*\
    Set access Token
\*=========================================================================*/
void playerSetToken( const char *token )
{
  loginfo( "Setting ickstream access token to \"%s\"", token );
  accessToken = token;  
  persistSetString( "IckAccessToken", token );
}


/*=========================================================================*\
    Set player name
\*=========================================================================*/
void playerSetName( const char *name, bool broadcast )
{
  loginfo( "Setting player name to \"%s\"", name );

/*------------------------------------------------------------------------*\
    Store new name 
\*------------------------------------------------------------------------*/
  playerName = name;  
  persistSetString( "PlayerName", name );

/*------------------------------------------------------------------------*\
    Update timestamp and broadcast new player state
\*------------------------------------------------------------------------*/
  lastChange = srvtime( );
  if( broadcast )
    ickMessageNotifyPlayerState();
}


/*=========================================================================*\
      Set Volume 
        returns effective volume (set to 0 if muted)
\*=========================================================================*/
double playerSetVolume( double volume, bool muted, bool broadcast )
{
  loginfo( "Setting Volume to %lf", volume );

/*------------------------------------------------------------------------*\
    Clip value 
\*------------------------------------------------------------------------*/
  if( volume<0 )
    volume = 0;
  if( volume>1 )
    volume = 1;

/*------------------------------------------------------------------------*\
    use internal interface to backend or codec
\*------------------------------------------------------------------------*/
  if( _playerSetVolume(playerVolume,playerMuted) )
    logwarn( "playerSetVolume: could not set volume to %.2lf%%%s", 
              playerVolume*100, playerMuted?" (muted)":"" ); 

/*------------------------------------------------------------------------*\
    Store new volume and muting state
\*------------------------------------------------------------------------*/
  playerVolume = volume;
  persistSetReal( "PlayerVolume", volume );
  playerMuted = muted;
  persistSetBool( "playerMuted", muted );


/*------------------------------------------------------------------------*\
    Update timestamp and broadcast new player state
\*------------------------------------------------------------------------*/
  lastChange = srvtime( );
  hmiNewVolume( playerVolume, playerMuted );
  if( broadcast )
    ickMessageNotifyPlayerState();

/*------------------------------------------------------------------------*\
    Return new volume 
\*------------------------------------------------------------------------*/
  return muted?0:playerVolume;
}


/*=========================================================================*\
      Set Volume 
        internal interface to audio backand or codec 
\*=========================================================================*/
static int _playerSetVolume( double volume, bool muted )
{
  int rc = 0;
  DBGMSG( "_playerSetVolume: %lf%s", volume, muted?" (muted)":"" );

/*------------------------------------------------------------------------*\
    Use backend
\*------------------------------------------------------------------------*/
  if( audioIf && audioIfSupportsVolume(audioIf) ) {
    if( audioIfSetVolume(audioIf,volume,muted) ) {
      logerr( "_playerSetVolume: could not set volume to %.2lf%%%s for backend \"%s\"", 
               volume*100, muted?" (muted)":"", audioIf->devName ); 
      rc = -1;
    }

  }

/*------------------------------------------------------------------------*\
    Use codec
\*------------------------------------------------------------------------*/
  else if( codecInstance ) {
    if( codecSetVolume(codecInstance,playerVolume,playerMuted) ) {
      logerr( "_playerSetVolume: could not set volume to %.2lf%%%s for codec %s", 
                volume*100, muted?" (muted)":"", codecInstance->codec->name );
      rc = -1;
    } 
  }

/*------------------------------------------------------------------------*\
    Return new volume 
\*------------------------------------------------------------------------*/
  return rc;
}


/*=========================================================================*\
      Set repeat mode 
\*=========================================================================*/
int playerSetRepeatMode( PlayerRepeatMode mode, bool broadcast )
{
  loginfo( "Setting repeat mode to %d", mode );

/*------------------------------------------------------------------------*\
    Store new state 
\*------------------------------------------------------------------------*/
  playerRepeatMode = mode;  
  persistSetInteger( "PlayerRepeatMode", mode );

/*------------------------------------------------------------------------*\
    Update timestamp and broadcast new player state
\*------------------------------------------------------------------------*/
  lastChange = srvtime( );
  hmiNewRepeatMode( mode );
  if( broadcast )
    ickMessageNotifyPlayerState();

/*=========================================================================*\
      return new mode 
\*=========================================================================*/
  return 0;
}


/*=========================================================================*\
      Change playback state 
\*=========================================================================*/
int playerSetState( PlayerState state, bool broadcast )
{
  int           rc = 0;
  PlaylistItem *newTrack;
  
  DBGMSG( "playerSetState: %d -> %d", playerState, state );
  
/*------------------------------------------------------------------------*\
    Lock player, we don't want concurrent modifications going on...
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &playerMutex );

/*------------------------------------------------------------------------*\
    Get current playback item to detect changes in the queue 
\*------------------------------------------------------------------------*/
  newTrack = playlistGetCursorItem( playerQueue );
   
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
          logerr( "_playbackStart: Could not open audio device: %s", playerAudioDevice );
          rc = -1;
          break;
        }
        if( _playerSetVolume(playerVolume,playerMuted) )
          logwarn( "_playerThread: could not set volume to %.2lf%%%s", 
                   playerVolume*100, playerMuted?" (muted)":"" ); 
      }
  	  
      // Is there a track to play ?
      if( !newTrack ) {
  	    lognotice( "_playbackStart: Empty queue or no cursor" );
  	    rc = -1;
  	    break;
      }
      
      // Unpausing existing track ?
      if( playerState==PlayerStatePause && currentTrackId && 
           !strcmp(playlistItemGetId(newTrack),currentTrackId) ) {
  	    lognotice( "_playbackStart: Track \"%s\" (%s) unpaused", 
                   playlistItemGetText(newTrack), playlistItemGetId(newTrack) );
        playerState = PlayerStatePlay;
  	    rc = audioIfSetPause( audioIf, false );
  	    break;
      }
  	  
  	  // Need to stop running track?
  	  if( playerState==PlayerStatePlay ) {
        DBGMSG( "Stopping active player thread." );
  	  	if( !currentTrackId )
  	  	  logerr( "_playbackStart: internal error: playing but no current track." );
  	  	else 
          lognotice( "_playbackStart: stopping current track (%s)", currentTrackId );
        if( playbackThreadState!=PlayerThreadRunning )
          logerr( "_playbackStart: internal error: playing but no running playback thread." );
        else {
          playbackThreadState = PlayerThreadTerminating;
          pthread_join( playbackThread, NULL ); 
        }
  	  }
  	  
      // Create new playback thread
      rc = pthread_create( &playbackThread, NULL, _playbackThread, NULL );
      DBGMSG( "Starting new player thread." );
      if( rc ) {
        logerr( "_playbackStart: Unable to start thread: %s", strerror(rc) );
        rc = -1;
        break;
      }

      // Set new state
      playerState = PlayerStatePlay;
      break;

/*------------------------------------------------------------------------*\
    Pause playback
\*------------------------------------------------------------------------*/
    case PlayerStatePause:

     // not yet initialized?
      if( !audioIf ) {
        logerr( "_playbackPause: audio device not yet initialized." );
      	rc = -1;
      	break;
      } 

      // Is there a new track to play ?
      if( !newTrack ) {
  	    lognotice( "_playbackPause: Empty queue or no cursor" );
  	    rc = -1;
  	    break;
      }
      
      // if the track did not change: set pause flag
      if( currentTrackId && !strcmp(playlistItemGetId(newTrack),currentTrackId) ) {

        // Check for right state
        if( playerState==PlayerStateStop )
          logwarn( "_playbackPause: cannot pause stopped playback" );
        else if( playerState==PlayerStatePause )
          logwarn( "_playbackPause: pause already paused layback" );
        // Pause audio output and set state
        else {
          lognotice( "_playbackPause: playback paused" );
          rc = audioIfSetPause( audioIf, true );
          if( !rc )
            playerState = PlayerStatePause;
        }
        break;
      }
      
      // no break here, as we'll change the status to PlayerStateStop in case 
      // we were changing the track in paused state...
  	  lognotice( "_playbackPause: current track changed, stopping..." );

/*------------------------------------------------------------------------*\
    Stop playback
\*------------------------------------------------------------------------*/
    case PlayerStateStop:

      // not yet initialized?
      if( !audioIf ) {
        loginfo( "_playbackStop: audio device not yet initialized." );
        rc = -1;
        break;
      }

      // request thread to stop playback and set new player state
      playbackThreadState = PlayerThreadTerminating;
      playerState = PlayerStateStop;
      // Inform HMI
      hmiNewPosition( 0.0 );
      if( newTrack && (!currentTrackId || strcmp(playlistItemGetId(newTrack),currentTrackId)) )
        hmiNewItem( playerQueue, newTrack );

      break; 
  	
/*------------------------------------------------------------------------*\
    Unknown command
\*------------------------------------------------------------------------*/
    default:
      logerr( "playerSetState: Unknown tagret state %d", state );
      rc = -1;
      break;
  }

/*------------------------------------------------------------------------*\
    Update timestamp, unlock player and broadcast new player state
\*------------------------------------------------------------------------*/
  lastChange = srvtime( );
  pthread_mutex_unlock( &playerMutex );
  hmiNewState( playerState );
  if( broadcast )
    ickMessageNotifyPlayerState();

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
    AudioFormat    backendFormat;
 
    DBGMSG( "_playerThread: Starting track \"%s\" (%s)", 
              playlistItemGetText(item), playlistItemGetId(item) );
 
    // Try to get feed and codec for new track, if negative skip queue item
    // Fixme: include this in format search loop...
    feed = _feedFromPlayListItem( item, &codec );
    if( !feed ) {
      lognotice( "_playerThread: Track \"%s\" (%s) unavailable or unsupported by audio module", 
                          playlistItemGetText(item), playlistItemGetId(item) );
      item = playlistIncrCursorItem( playerQueue );
      continue;
    }

    // Get fifo from audio interface, try to use explicite format
    memcpy( &backendFormat, &feed->format, sizeof(AudioFormat) );
    fifo = audioIfPlay( audioIf, &backendFormat );

    // Try to complete format from first default entry
    AudioFormatList formatList = codec->defaultAudioFormats;
    if( !formatList )
      formatList = defaultAudioFormats;
    if( !fifo && formatList ) {
      AudioFormat tryFormat;
      memcpy( &tryFormat, &backendFormat, sizeof(AudioFormat) );
      audioFormatComplete( &tryFormat, &formatList->format );
      fifo = audioIfPlay( audioIf, &tryFormat );
      if( fifo )
        memcpy( &backendFormat, &tryFormat, sizeof(AudioFormat) );
    }

    // If necessary try all default audio formats
    if( !fifo ) {
      loginfo( "_playerThread: Cannot setup audio device \"%s\" to format %s.", 
                        audioIf->devName, audioFormatStr(NULL,&feed->format) );
      AudioFormatElement *element = formatList;
      while( !fifo && element ) {
        memcpy( &backendFormat, &element->format, sizeof(AudioFormat) );
        fifo = audioIfPlay( audioIf, &backendFormat );
      }
      if( fifo )
        loginfo( "_playerThread: using default format %s for audio device %s.", 
                       audioFormatStr(NULL,&backendFormat), audioIf->devName );
    }

    // still no fifo?
    if( !fifo  ) {
      lognotice( "_playerThread: Cannot setup audio device \"%s\" to format %s or any default format.", 
                        audioIf->devName, audioFormatStr(NULL,&feed->format) );
      audioFeedDelete( feed , true );
      playbackThreadState = PlayerThreadTerminatedError;
      break;
    }

    // create codec instance
    // Fixme: include this in format selection from defaults
    codecInst = codecNewInstance( codec, fifo, &backendFormat );
    if( !codecInst ) {
      logerr( "_playerThread: could not get instance of codec %s for format %s", 
                       codec->name, audioFormatStr(NULL,&backendFormat) );
      audioFeedDelete( feed, true );
      playbackThreadState = PlayerThreadTerminatedError;
      break;
    }
    
    // initialize volume
    if( _playerSetVolume(playerVolume,playerMuted) )
      logwarn( "_playerThread: could not set volume to %.2lf%%%s", 
                playerVolume*100, playerMuted?" (muted)":"" ); 
 
    // Attach feed to codec and start...
    if( audioFeedStart(feed,codecInst) ) {
      logerr( "_playerThread: Could not start feed \"%s\" on codec %s", 
                         feed->uri, codecInst->codec->name );
      audioFeedDelete( feed, true );
      codecDeleteInstance( codecInst, true );
      playbackThreadState = PlayerThreadTerminatedError;
      break;
    }
    
    // Save new track id and brodcast new player status
    Sfree( currentTrackId );
    currentTrackId = strdup( playlistItemGetId(item) );
    codecInstance = codecInst;
    ickMessageNotifyPlayerState();
    lognotice( "_playerThread: Playing track \"%s\" (%s) with %s", 
      	                playlistItemGetText(item), playlistItemGetId(item), 
                        audioFormatStr(NULL,&backendFormat) );

    // Inform HMI
    hmiNewItem( playerQueue, item );
    hmiNewFormat( &backendFormat );
#ifndef NOHMI
    double seekPos = 0;
    hmiNewPosition( seekPos );
#endif
 	                
    // Wait for end of feed or stop condition ...
    while( playbackThreadState==PlayerThreadRunning ) {
      int rc = codecWaitForEnd( codecInst, 250 );
      if( !rc )
        break;
      if( rc<0 )
        playbackThreadState = PlayerThreadTerminatedError;
      
      // Inform HMI about new player position but suppress updates in paused state
#ifndef NOHMI
      double pos;
      if( rc>0 && !codecGetSeekTime(codecInst,&pos) ) {
        if( pos!=seekPos && playerState==PlayerStatePlay ) {
          seekPos = pos;
          hmiNewPosition( seekPos );
        }
      }
#endif
    }
 
    // Get rid of feed
    if( feed && audioFeedDelete(feed,true) )
      logerr( "_playerThread: Could not delete feeder instance" );
 
    // Get rid of codec
    codecInstance = NULL;
    if( codecDeleteInstance(codecInst,true) )
      logerr( "_playerThread: Could not delete codec instance" );
     	
    // Terminating ?
    if( playbackThreadState!=PlayerThreadRunning )
      break;

    // Repeat track?
    if( playerRepeatMode==PlayerRepeatItem )
      continue;

    // Get next item
    item = playlistIncrCursorItem( playerQueue );
    if( item )
      continue;

    // repeat at end of list
    if( playerRepeatMode==PlayerRepeatQueue )
      item = playlistSetCursorPos( playerQueue, 0 );

    // repeat at end of list with shuffling
    else if( playerRepeatMode==PlayerRepeatShuffle ) {
      item = playlistShuffle( playerQueue, 0, playlistGetLength(playerQueue)-1, false );
      ickMessageNotifyPlaylist();
     }

  }  // End of: Thread main loop
 
/*------------------------------------------------------------------------*\
    Stop audio interface
\*------------------------------------------------------------------------*/
  if( audioIfStop(audioIf,playbackThreadState==PlayerThreadRunning?AudioDrain:AudioDrop) )
    logerr( "_playerThread: Could not stop audio interface %s", audioIf->devName );

/*------------------------------------------------------------------------*\
    Set and broadcast new player state
\*------------------------------------------------------------------------*/
  if( playbackThreadState==PlayerThreadRunning ) {
    lognotice( "_playerThread: End of playlist." );
    playerState = PlayerStateStop;
    ickMessageNotifyPlayerState();
    hmiNewState( playerState );
    hmiNewPosition( 0.0 );
  }

/*------------------------------------------------------------------------*\
    Clean up, that's it ...  
\*------------------------------------------------------------------------*/
  Sfree( currentTrackId );
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
  json_t *jStreamingRefs;
 
/*------------------------------------------------------------------------*\
    Get stream hints 
\*------------------------------------------------------------------------*/
  jStreamingRefs = playlistItemGetStreamingRefs( item );
  if( !jStreamingRefs ) {
    logerr( "No streaming references found for track \"%s\" (%s).", 
              playlistItemGetText(item), playlistItemGetId(item) );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Loop over all hints 
\*------------------------------------------------------------------------*/
  for( i=0; i<json_array_size(jStreamingRefs); i++ ) {
    json_t          *jStreamRef = json_array_get( jStreamingRefs, i );
    json_t          *jObj;
    AudioFormat      format;
    AudioFeed       *feed;
    const char      *type;
    char            *uri;
    
    // Reset Format
    memset( &format, 0,sizeof(AudioFormat) );

    // Get type
    jObj = json_object_get( jStreamRef, "format" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "StreamRef #%d for track \"%s\" (%s): no format!", 
               i, playlistItemGetText(item), playlistItemGetId(item) );
      continue;
    }
    type = json_string_value( jObj );
        
    // Get URI
    jObj = json_object_get( jStreamRef, "url" );
    if( !jObj || !json_is_string(jObj) ) {
      logerr( "StreamRef #%d for track \"%s\" (%s): no url!", 
               i, playlistItemGetText(item), playlistItemGetId(item) );
      continue;
    }  
      
    // Try to resolve the URI (will allocate the string)
    uri = ickServiceResolveURI( json_string_value(jObj), "content" );
    if( !uri ) {
      lognotice( "StreamRef #%d for track \"%s\" (%s): cannot resolve URL \"%s\"!", 
                  i, playlistItemGetText(item), playlistItemGetId(item), 
                  json_string_value(jObj) );
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
              type, audioFormatStr(NULL,&format)  );	
      Sfree( uri );
  	  continue;
    }

    // Try to complete format from first default entry
    AudioFormatList formatList = (*codec)->defaultAudioFormats;
    if( !formatList )
      formatList = defaultAudioFormats;
    if( formatList ) {
      AudioFormat tryFormat;
      memcpy( &tryFormat, &format, sizeof(AudioFormat) );
      audioFormatComplete( &tryFormat, &formatList->format );
      feed = audioFeedCreate( uri, type, &tryFormat );
    }

    // If necessary try all default audio formats for codec
    if( !feed ) {
      loginfo( "_feedFromPlayListItem: Cannot setup audio feed \"%s\" to format %s.", 
                        uri, audioFormatStr(NULL,&format) );
      AudioFormatElement *element = formatList;
      while( !feed && element ) {
        AudioFormat tryFormat;
        memcpy( &tryFormat, &element->format, sizeof(AudioFormat) );
        feed = audioFeedCreate( uri, type, &tryFormat );
      }
      if( feed )
        loginfo( "_feedFromPlayListItem: using default format %s for audio feed %s.", 
                       audioFormatStr(NULL,&feed->format), uri );
    }

    // No feed found? 
    if( !feed )
      DBGMSG( "StreamRef \"%s\" (%s) with (%s): could not open.", 
              uri, type, audioFormatStr(NULL,&format)  );	
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




