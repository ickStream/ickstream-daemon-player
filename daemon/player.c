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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>
#include <jansson.h>
#ifdef ICK_UUID
#include <uuid/uuid.h>
#endif

#include "ickutils.h"
#include "ickService.h"
#include "ickMessage.h"
#include "ickCloud.h"
#include "ickScrobble.h"
#include "metaIcy.h"
#include "persist.h"
#include "playlist.h"
#include "feed.h"
#include "audio.h"
#include "player.h"
#include "hmi.h"

// #define ICK_RAWMETA

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
static double              playerVolume;
static bool                playerMuted;
static Playlist           *playerQueue;
static PlayerPlaybackMode  playerPlaybackMode = PlaybackQueue;
static AudioFormat         defaultAudioFormat;

// transient
pthread_mutex_t            playerMutex;
static PlayerState         playerState = PlayerStateStop;
static double              lastChange;
static AudioIf            *audioIf;

// Playback thread
static pthread_t                   playbackThread;
static volatile PlayerThreadState  playbackThreadState;
static char                       *currentTrackId;
static CodecInstance              *codecInstance;


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static int        _playerSetVolume( double volume, bool muted );
static void      *_playbackThread( void *arg );
static int        _playItem( PlaylistItem *item, AudioFormat *format );
static AudioFeed *_feedFromPlayListItem( PlaylistItem *item, Codec **codec, AudioFormat *format, int timeout );
static int        _audioFeedCallback( AudioFeed *feed, void* usrData );
static int        _codecNewFormatCallback( CodecInstance *instance, void *userData );
#ifdef ICK_RAWMETA
static void       _codecMetaCallback( CodecInstance *instance, CodecMetaType mType, json_t *jMeta, void *userData );
#endif


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
  playlistLock( playerQueue );
  playlistSetCursorPos( playerQueue, persistGetInteger("PlayerQueuePosition") );
  playlistUnlock( playerQueue );

/*------------------------------------------------------------------------*\
    Get repeat mode
\*------------------------------------------------------------------------*/
  playerPlaybackMode = persistGetInteger( "PlayerPlaybackMode" );

/*------------------------------------------------------------------------*\
    Get default audio format
\*------------------------------------------------------------------------*/
  if( !playerGetDefaultAudioFormat() ) {
    logerr( "Invalid or no default audio format." );
    return -1;
  }


/*------------------------------------------------------------------------*\
    Init mutex
\*------------------------------------------------------------------------*/
  ickMutexInit( &playerMutex );

/*------------------------------------------------------------------------*\
    Inform HMI and set timestamp 
\*------------------------------------------------------------------------*/
  hmiNewQueue( playerQueue );
  hmiNewState( playerState );
  hmiNewVolume( playerVolume, playerMuted );
  hmiNewPlaybackMode( playerPlaybackMode );
  lastChange = srvtime( );

/*------------------------------------------------------------------------*\
    If possible tell cloud services about the current address
\*------------------------------------------------------------------------*/
  if( ickCloudGetAccessToken() )
    ickCloudSetDeviceAddress( );

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
    Save playback queue state
\*------------------------------------------------------------------------*/
  playlistLock( playerQueue );
  persistSetInteger( "PlayerQueuePosition", playlistGetCursorPos(playerQueue) );
  persistSetJSON_new( "PlayerQueue", playlistGetJSON(playerQueue,PlaylistHybrid,0,0) );
  playlistUnlock( playerQueue );

/*------------------------------------------------------------------------*\
    Shut down player thread (if any)
\*------------------------------------------------------------------------*/
  if( playerState==PlayerStatePlay || playerState==PlayerStatePause )
    pthread_join( playbackThread, NULL );

/*------------------------------------------------------------------------*\
    Close audio interface
\*------------------------------------------------------------------------*/
  if( audioIf ) {
    audioIfDelete( audioIf, true );
    audioIf = NULL;
  }

/*------------------------------------------------------------------------*\
    Delete mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &playerMutex );
}


/*=========================================================================*\
      Get an ickstream protocol string from repeat mode
\*=========================================================================*/
const char *playerPlaybackModeToStr( PlayerPlaybackMode mode )
{
  DBGMSG( "playerPlaybackModeToStr: mode %d", mode );

  // Translate
  switch( mode ) {
    case PlaybackQueue:         return "QUEUE";
    case PlaybackShuffle:       return "QUEUE_SHUFFLE";
    case PlaybackRepeatQueue:   return "QUEUE_REPEAT";
    case PlaybackRepeatItem:    return "QUEUE_REPEAT_ITEM";
    case PlaybackRepeatShuffle: return "QUEUE_REPEAT_SHUFFLE";
    case PlaybackDynamic:       return "QUEUE_DYNAMIC";
  }

  // Not known or unsupported
  logerr( "Playback queue mode %d unknown or not supported by ickstream protocol.");
  return "QUEUE";
}


/*=========================================================================*\
      Get playback mode form ickstream protocol string
\*=========================================================================*/
PlayerPlaybackMode playerPlaybackModeFromStr( const char *str )
{
  DBGMSG( "playerPlaybackModeFromStr: \"%s\"", str );

  // Translate
  if( !strcmp(str,"QUEUE") )
    return PlaybackQueue;
  if( !strcmp(str,"QUEUE_SHUFFLE") )
    return PlaybackShuffle;
  if( !strcmp(str,"QUEUE_REPEAT") )
    return PlaybackRepeatQueue;
  if( !strcmp(str,"QUEUE_REPEAT_ITEM") )
    return PlaybackRepeatItem;
  if( !strcmp(str,"QUEUE_REPEAT_SHUFFLE") )
    return PlaybackRepeatShuffle;
  if( !strcmp(str,"QUEUE_DYNAMIC") )
    return PlaybackDynamic;

  // Not known or unsupported
  return -1;
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
  playlistLock( playerQueue );
  playlistReset( playerQueue, true );
  playlistUnlock( playerQueue );
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
PlayerPlaybackMode playerGetPlaybackMode( void )
{
  DBGMSG( "playerGetPlaybackMode: %d", playerPlaybackMode );
  return playerPlaybackMode;
}


/*=========================================================================*\
    Get default audio Format
\*=========================================================================*/
const AudioFormat *playerGetDefaultAudioFormat( void )
{
  const char *formatStr;

/*------------------------------------------------------------------------*\
    Already initialized?
\*------------------------------------------------------------------------*/
  if( audioFormatIsComplete(&defaultAudioFormat) )
    return &defaultAudioFormat;

/*------------------------------------------------------------------------*\
    Try to get persisted value
\*------------------------------------------------------------------------*/
  formatStr = persistGetString( "DefaultAudioFormat" );
  if( !formatStr )
    return NULL;

/*------------------------------------------------------------------------*\
    Try to parse the format string
\*------------------------------------------------------------------------*/
  if( audioStrFormat(&defaultAudioFormat,formatStr) ||
      !audioFormatIsComplete(&defaultAudioFormat))
    return NULL;

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return &defaultAudioFormat;
}


/*=========================================================================*\
    Get player hardware ID
      return NULL if not supported
      We use the MAC address as default (network name must be set).
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
    Get current IP address
\*=========================================================================*/
const char *playerGetIpAddress( void )
{
  const char *deviceIf;
  const char *addrStr = NULL;

/*------------------------------------------------------------------------*\
    Need an interface
\*------------------------------------------------------------------------*/
  deviceIf = playerGetInterface();
  if( !deviceIf ) {
    logerr( "playerGetIpAddress: Device interface not yet defined." );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    The interface could be already defined by an address
\*------------------------------------------------------------------------*/
  if( inet_addr(deviceIf)!=INADDR_NONE )
    addrStr = deviceIf;

/*------------------------------------------------------------------------*\
    Get the address of the interface
\*------------------------------------------------------------------------*/
  else {
    int          fd;
    struct ifreq req;
    int          len;
    memset( &req, 0, sizeof(req) );

    fd = socket( PF_INET, SOCK_DGRAM, 0 );
    if( fd<0 ) {
      logerr( "playerGetIpAddress: Could not get socket (%s).", strerror(errno) );
      return NULL;
    }

    strncpy( req.ifr_name, deviceIf, IFNAMSIZ );
    if( ioctl(fd,SIOCGIFADDR,&req,&len)<0 ) {
      logerr( "playerGetIpAddress: Could not ioctl socket (%s).", strerror(errno) );
      close( fd );
      return NULL;
    }

    addrStr = inet_ntoa( ((struct sockaddr_in *)&req.ifr_addr)->sin_addr );
    close( fd );
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  DBGMSG( "playerGetIpAddress (%s): addrStr.", deviceIf, addrStr );
  return addrStr;
}


/*=========================================================================*\
    Get player UUID
\*=========================================================================*/
const char *playerGetUUID( void )
{

/*------------------------------------------------------------------------*\
    Get persistent value
\*------------------------------------------------------------------------*/
  if( !playerUUID )
    playerUUID = persistGetString( "DeviceUUID" );

/*------------------------------------------------------------------------*\
    Need to set value once
\*------------------------------------------------------------------------*/
  if( !playerUUID ) {
#ifdef ICK_UUID
    uuid_t newUUID;
    char   strUUID[37];
    uuid_generate_random( newUUID );
    uuid_unparse_lower( newUUID, strUUID );
    persistSetString( "DeviceUUID", strUUID );  // persist local buffer
    playerUUID = persistGetString( "DeviceUUID" );
#else
    logwarn( "No UUID generator, trying to use hardware id as default." );
    playerUUID = playerGetHWID();
    if( !playerUUID ) {
      logerr( "Could not determine player UUID" );
      return NULL;
    }
    persistSetString( "DeviceUUID", playerUUID );
#endif
    lognotice( "Initialized player UUID to: %s", playerUUID );
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
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
    Set default audio format
\*=========================================================================*/
int playerSetDefaultAudioFormat( const char *format )
{
  DBGMSG( "Setting default audio format: \"%s\".", format );

/*------------------------------------------------------------------------*\
    Try to parse the format string
\*------------------------------------------------------------------------*/
  if( audioStrFormat(&defaultAudioFormat,format) ) {
    logerr( "Cannot parse default audio format (%s).", format );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Should be complete
\*------------------------------------------------------------------------*/
  if( !audioFormatIsComplete(&defaultAudioFormat) ){
    logerr( "Default audio format needs to be complete (%s).", format );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Store value and return
\*------------------------------------------------------------------------*/
  persistSetString( "DefaultAudioFormat", format );
  return 0;
}


/*=========================================================================*\
    Set player UUID (only in debug mode)
\*=========================================================================*/
#ifdef ICK_DEBUG
void playerSetUUID( const char *uuid )
{	
  loginfo( "Setting player UUID to \"%s\"", uuid );
  playerUUID = uuid;  
  persistSetString( "DeviceUUID", uuid );
}
#endif


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
    ickMessageNotifyPlayerState( NULL );
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
    ickMessageNotifyPlayerState( NULL );

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
int playerSetPlaybackMode( PlayerPlaybackMode mode, bool broadcast )
{
  bool playlistChanged = false;
  loginfo( "Setting playback mode to %d", mode );

/*------------------------------------------------------------------------*\
    Store new state
\*------------------------------------------------------------------------*/
  if( playerPlaybackMode==mode )
    return 0;

/*------------------------------------------------------------------------*\
    Store new state 
\*------------------------------------------------------------------------*/
  playerPlaybackMode = mode;
  persistSetInteger( "PlayerPlaybackMode", mode );

/*------------------------------------------------------------------------*\
    Modify mapping if necessary
\*------------------------------------------------------------------------*/
  switch( playerPlaybackMode ) {
    case PlaybackShuffle:
      if( playerQueue ) {
        int startPos = 0;
        int endPos   = playlistGetLength( playerQueue )-1;
        playlistShuffle( playerQueue, startPos, endPos, true );
        playlistChanged = true;
      }
      break;

    case PlaybackRepeatShuffle:
      break;

    case PlaybackQueue:
    case PlaybackRepeatQueue:
    case PlaybackRepeatItem:
    case PlaybackDynamic:
      if( playerQueue ) {
        playlistResetMapping( playerQueue, false );
        playlistChanged = true;
      }
      break;
  }

/*------------------------------------------------------------------------*\
    Update timestamp and broadcast new player state
\*------------------------------------------------------------------------*/
  lastChange = srvtime( );
  hmiNewPlaybackMode( mode );
  if( playlistChanged )
    hmiNewQueue( playerQueue );
  if( broadcast )
    ickMessageNotifyPlayerState( NULL );
  if( broadcast && playlistChanged )
    ickMessageNotifyPlaylist( NULL );

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
  const char   *newTrackId;
  
  DBGMSG( "playerSetState: %d -> %d", playerState, state );
  
/*------------------------------------------------------------------------*\
    Lock player, we don't want concurrent modifications going on...
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &playerMutex );

/*------------------------------------------------------------------------*\
    Get current playback item to detect changes in the queue 
\*------------------------------------------------------------------------*/
  playlistLock( playerQueue );
  newTrack = playlistGetCursorItem( playerQueue );
  playlistUnlock( playerQueue );

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
          audioIf = audioIfNew( backend, device, AudioFifoDefaultSize );
        if( !audioIf ) {
          logerr( "playerSetState (start): Could not open audio device \"%s\".", playerAudioDevice );
          rc = -1;
          break;
        }
        if( _playerSetVolume(playerVolume,playerMuted) )
          logwarn( "playerSetState (start): Could not set volume to %.2lf%% (%s).",
                   playerVolume*100, playerMuted?"muted":"unmuted" );
      }

      // Is there a track to play ?
      if( !newTrack ) {
        lognotice( "playerSetState (start): Empty queue or no cursor." );
        rc = -1;
       break;
      }
      
      // Unpausing existing track ?
      playlistItemLock( newTrack );
      newTrackId = playlistItemGetId( newTrack );

      if( playerState==PlayerStatePause && currentTrackId && 
           !strcmp(newTrackId,currentTrackId) ) {
        lognotice( "playerSetState (start): Unpausing item \"%s\" (%s).",
                   playlistItemGetText(newTrack), playlistItemGetId(newTrack) );
        playlistItemUnlock( newTrack );
        playerState = PlayerStatePlay;
        rc = audioIfSetPause( audioIf, false );
        break;
      }
      playlistItemUnlock( newTrack );

     // Need to stop running track?
      if( playerState==PlayerStatePlay ) {
        DBGMSG( "playerSetState (start): Request active playback thread to terminate." );
        if( !currentTrackId )
          logerr( "playerSetState (start): internal error (No current track)." );
        else
          lognotice( "playerSetState (start): Stopping current track (%s).", currentTrackId );
        if( playbackThreadState!=PlayerThreadRunning )
          logerr( "playerSetState (start): internal error: playing but no running playback thread." );
        else {
          playbackThreadState = PlayerThreadTerminating;
          pthread_join( playbackThread, NULL ); 
        }
      }

      // Create new playback thread
      rc = pthread_create( &playbackThread, NULL, _playbackThread, NULL );
      DBGMSG( "Starting new player thread." );
      if( rc ) {
        logerr( "playerSetState (start): Unable to start thread (%s).", strerror(rc) );
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
        logerr( "playerSetState (pause): audio device not yet initialized." );
        rc = -1;
        break;
      } 

      // Is there a, feed->usrData track to play ?
      if( !newTrack ) {
        lognotice( "playerSetState (pause): Empty queue or no cursor" );
        rc = -1;
        break;
      }
      
      // if the track did not change: set pause flag
      playlistItemLock( newTrack );
      newTrackId = playlistItemGetId( newTrack );
      playlistItemUnlock( newTrack );
      if( currentTrackId && !strcmp(newTrackId,currentTrackId) ) {

        // Check for right state
        if( playerState==PlayerStateStop )
          logwarn( "playerSetState (pause): Cannot pause stopped playback." );
        else if( playerState==PlayerStatePause )
          logwarn( "playerSetState (pause): Pause already paused playback." );
        // Pause audio output and set state
        else {
          lognotice( "playerSetState (pause): Playback paused." );
          rc = audioIfSetPause( audioIf, true );
          if( !rc )
            playerState = PlayerStatePause;
        }
        break;
      }
      
      lognotice( "playerSetState (pause): Current track changed, stopping..." );
      // no break here, as we'll change the status to PlayerStateStop in case 
      // we were changing the track in paused state...


/*------------------------------------------------------------------------*\
    Stop playback
\*------------------------------------------------------------------------*/
    case PlayerStateStop:

      // Inform HMI in any case
      hmiNewPosition( 0.0 );
      /*
      playlistItemLock( newTrack );
      newTrackId = playlistItemGetId( newTrack );
      playlistItemUnlock( newTrack );
      if( newTrack && (!currentTrackId || strcmp(newTrackId,currentTrackId)) )
      */
      hmiNewQueue( playerQueue );

      // not yet initialized?
      if( !audioIf ) {
        loginfo( "playerSetState (stop): Audio device not yet initialized." );
        rc = -1;
        break;
      }

      // request thread to stop playback and set new player state
      playbackThreadState = PlayerThreadTerminating;
      if( playerState==PlayerStatePlay || playerState==PlayerStatePause )
        pthread_join( playbackThread, NULL );
      playerState = PlayerStateStop;


      break; 

/*------------------------------------------------------------------------*\
    Unknown command
\*------------------------------------------------------------------------*/
    default:
      logerr( "playerSetState: Unknown target state %d.", state );
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
    ickMessageNotifyPlayerState( NULL );

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
  AudioFormat   backendFormat;

  DBGMSG( "Player thread: starting." );
  PTHREADSETNAME( "player" );

/*------------------------------------------------------------------------*\
    Init encoding part of audio format from player default
\*------------------------------------------------------------------------*/
  memcpy( &backendFormat, &defaultAudioFormat, sizeof(AudioFormat) );
  backendFormat.sampleRate = -1;
  backendFormat.channels   = -1;

/*------------------------------------------------------------------------*\
    Loop over player queue 
\*------------------------------------------------------------------------*/
  playbackThreadState = PlayerThreadRunning;
  playlistLock( playerQueue );
  item = playlistGetCursorItem( playerQueue );
  playlistUnlock( playerQueue );
  while( item && playbackThreadState==PlayerThreadRunning ) {

    // Play item
    if( _playItem(item,&backendFormat) )
      playbackThreadState = PlayerThreadTerminatedError;

    // Error or stopped?
    if( playbackThreadState!=PlayerThreadRunning )
      break;

    // Repeat track or reconnect stream?
    if( playerPlaybackMode==PlaybackRepeatItem ||
        playlistItemGetType(item)==PlaylistItemStream )
      continue;

    // Get next item
    playlistLock( playerQueue );
    item = playlistIncrCursorItem( playerQueue );
    playlistUnlock( playerQueue );
    if( item )
      continue;

    // repeat at end of list
    if( playerPlaybackMode==PlaybackRepeatQueue ) {
      playlistLock( playerQueue );
      item = playlistSetCursorPos( playerQueue, 0 );
      playlistUnlock( playerQueue );
    }

    // repeat at end of list with shuffling
    else if( playerPlaybackMode==PlaybackRepeatShuffle ) {
      playlistLock( playerQueue );
      playlistShuffle( playerQueue, 0, playlistGetLength(playerQueue)-1, false );
      item = playlistSetCursorPos( playerQueue, 0 );
      playlistUnlock( playerQueue );
      ickMessageNotifyPlaylist( NULL );
     }

  }  // End of: Thread main loop
  DBGMSG( "Player thread: End of playback loop (state %d).", playbackThreadState );

/*------------------------------------------------------------------------*\
    Stop audio interface
\*------------------------------------------------------------------------*/
  if( audioIfStop(audioIf,playbackThreadState==PlayerThreadRunning?AudioDrain:AudioDrop) )
    logerr( "Player thread: Could not stop audio interface \"%s\".", audioIf->devName );

/*------------------------------------------------------------------------*\
    Set and broadcast new player state
\*------------------------------------------------------------------------*/
  if( playbackThreadState==PlayerThreadRunning ) {
    lognotice( "_playerThread: End of queue." );
    playerState = PlayerStateStop;
    ickMessageNotifyPlayerState( NULL );
    hmiNewState( playerState );
    hmiNewPosition( 0.0 );
  }

/*------------------------------------------------------------------------*\
    Clean up, that's it ...
\*------------------------------------------------------------------------*/
  Sfree( currentTrackId );
  DBGMSG( "Player thread: Terminated due to state %d.", playbackThreadState );
  return NULL;
}


/*=========================================================================*\
    Play an item
      Setup and modify the audio chain.
      Try to use format if possible. Store actual format in format.
      Returns 0 in case the playback loop shall do the next iteration or
      -1 if it shall break
    Fixme: somehow reference the streamRef to allow the usage of
           alternatives for broken streams.
\*=========================================================================*/
static int _playItem( PlaylistItem *item, AudioFormat *format )
{
  AudioFeed     *feed;
  Codec         *codec;
  CodecInstance *codecInst;   // mirrored to global variable codecInstance
  double         seekPos = 0;
  double         pos     = 0;
  int            retval = 0;

  playlistItemLock( item );
  DBGMSG( "_playItem: Starting %s \"%s\" (%s)",
            playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
            playlistItemGetText(item), playlistItemGetId(item) );
  playlistItemUnlock( item );

/*------------------------------------------------------------------------*\
    We will need an audio interface...
\*------------------------------------------------------------------------*/
  if( !audioIf ) {
    logerr( "_playItem: Called without initialized audio backend." );
    return -1;
  }

/*------------------------------------------------------------------------*\
   Inform HMI
\*------------------------------------------------------------------------*/
  hmiNewQueue( playerQueue );
  hmiNewPosition( seekPos );

/*------------------------------------------------------------------------*\
    Try to get a connected feed and codec for new track,
    if not successful skip queue item
\*------------------------------------------------------------------------*/
  DBGMSG( "_playItem (%s \"%s\"): Get feed.",
            playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
            playlistItemGetText(item) );
  feed = _feedFromPlayListItem( item, &codec, format, 5000 );
  if( !feed ) {
    playlistItemLock( item );
    lognotice( "_playItem (%s \"%s\"): Unavailable or format %s unsupported.",
            playlistItemGetType(item)==PlaylistItemStream?"Stream":"Track",
            playlistItemGetText(item), audioFormatStr(NULL,format) );
    playlistItemUnlock( item );
    return 0;
  }

/*------------------------------------------------------------------------*\
    Interpret ICY header fields and add to item
\*------------------------------------------------------------------------*/
  if( audioFeedGetFlags(feed)&FeedIcy ) {
    json_t *jIcyHdr;

    // Parse HTTP header for ICY information
    jIcyHdr = icyExtractHeaders( audioFeedGetResponseHeader(feed) );
    if( !jIcyHdr ) {
      logwarn( "_feedFromPlayListItem (%s,%s), Could not parse ICY headers.",
               playlistItemGetText(item), playlistItemGetId(item) );
      json_decref( jIcyHdr );
    }

    // Change and distribute meta data of current item
#ifdef ICK_RAWMETA
    else {
      json_t *jRawMeta = json_object_get( playlistItemGetJSON(item), "rawMeta" );
      if( !jRawMeta ) {
        jRawMeta = json_object();
        json_object_set_new( playlistItemGetJSON(item), "rawMeta", jRawMeta );
      }
      json_object_set_new( jRawMeta, "icyHeader", jIcyHdr );
      ickMessageNotifyPlayerState( NULL );
    }
#endif

  }

/*------------------------------------------------------------------------*\
    Create a codec instance...
\*------------------------------------------------------------------------*/
  DBGMSG( "_playItem (%s,\"%s\"): Init instance for codec %s (format %s).",
            playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
            playlistItemGetText(item), codec->name, audioFormatStr(NULL,format) );
  codecInst = codecNewInstance( codec, format, audioFeedGetFd(feed), audioIf->fifoIn );
  if( !codecInst ) {
    logerr( "_playItem (%s \"%s\"): Could not get instance of codec %s (format %s).",
            playlistItemGetType(item)==PlaylistItemStream?"Stream":"Track",
            playlistItemGetText(item), codec->name, audioFormatStr(NULL,format) );
    audioFeedDelete( feed, true );
    return -1;
  }
  codecSetIcyInterval( codecInst, audioFeedGetIcyInterval(feed) );
  codecSetFormatCallback( codecInst, &_codecNewFormatCallback, format );
#ifdef ICK_RAWMETA
  codecSetMetaCallback( codecInst, &_codecMetaCallback, item );
#endif
  if( codecStartInstance(codecInst) ) {
    logerr( "_playItem (%s \"%s\"): Could not start codec.",
                playlistItemGetType(item)==PlaylistItemStream?"Stream":"Track",
                playlistItemGetText(item), audioFormatStr(NULL,format) );
    codecDeleteInstance( codecInst, true );
    audioFeedDelete( feed, true );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Wait till the audio format is known
\*------------------------------------------------------------------------*/
  while( !audioFormatIsComplete(format) ) {
    DBGMSG( "_playItem (%s \"%s\"): Waiting for audio format detection (%s).",
              playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
              playlistItemGetText(item), audioFormatStr(NULL,format) );
    sleep( 1 );  // Fixme
  }

/*------------------------------------------------------------------------*\
    Start and/or parameterize output
\*------------------------------------------------------------------------*/
  DBGMSG( "_playItem (%s \"%s\"): Setup audio backend.",
            playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
            playlistItemGetText(item) );
  if( audioIfPlay(audioIf,format,AudioDrain) ) {
    logerr( "_playItem (%s \"%s\"): Could not setup audio backend (format %s).",
            playlistItemGetType(item)==PlaylistItemStream?"Stream":"Track",
            playlistItemGetText(item), audioFormatStr(NULL,format) );
    codecDeleteInstance( codecInst, true );
    audioFeedDelete( feed, true );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Set volume
\*------------------------------------------------------------------------*/
  DBGMSG( "_playItem (%s \"%s\"): Set volume.",
            playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
            playlistItemGetText(item) );
  if( _playerSetVolume(playerVolume,playerMuted) )
    logwarn( "_playItem (%s \"%s\"): Could not set volume to %.2lf%% (%s).",
            playlistItemGetType(item)==PlaylistItemStream?"Stream":"Track",
            playlistItemGetText(item), playerVolume*100, playerMuted?"muted":"unmuted" );

/*------------------------------------------------------------------------*\
    Save new track id and brodcast new player status
\*------------------------------------------------------------------------*/
  Sfree( currentTrackId );
  playlistItemLock( item );
  currentTrackId = strdup( playlistItemGetId(item) );
  playlistItemUnlock( item );
  codecInstance = codecInst;
  ickMessageNotifyPlayerState( NULL );
  playlistItemLock( item );
  lognotice( "_playItem (%s \"%s\"): Playing with %s (ID %s).",
                      playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
                      playlistItemGetText(item), audioFormatStr(NULL,format),
                      playlistItemGetId(item) );
  playlistItemUnlock( item );

/*------------------------------------------------------------------------*\
   Inform HMI
\*------------------------------------------------------------------------*/
  hmiNewFormat( format );

/*------------------------------------------------------------------------*\
  Scrobble streams at start of playing
\*------------------------------------------------------------------------*/
if( playlistItemGetType(item)==PlaylistItemStream &&
    ickScrobbleTrack(item,-1) )
  logerr( "_playItem (%s): Could not scrobble stream.", playlistItemGetText(item) );

/*------------------------------------------------------------------------*\
    Wait for end of feed or stop condition ...
\*------------------------------------------------------------------------*/
  while( playbackThreadState==PlayerThreadRunning ) {
    DBGMSG( "_playItem (%s \"%s\"): Start wait loop iteration.",
            playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
            playlistItemGetText(item) );
    int rc = codecWaitForEnd( codecInst, 250 );
    DBGMSG( "_playItem (%s \"%s\"): codecWaitForEnd returned %d (%s).",
            playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
            playlistItemGetText(item), rc, strerror(rc) );
    if( !rc )
      break;
    if( rc!=ETIMEDOUT ) {
      logerr( "_playItem (%s \"%s\"): Error while waiting for track end (%s).",
              playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
              playlistItemGetText(item), strerror(rc) );
      retval = -1;
      break;
    }

    // Get new player position
    if( rc>0 && !codecGetSeekTime(codecInst,&pos) ) {

      // Inform HMI on new positions but suppress updates in paused state
      if( pos>seekPos && playerState==PlayerStatePlay )
        hmiNewPosition( seekPos );

      // Store new position
      if( pos>seekPos )
        seekPos = pos;
    }

  }
  DBGMSG( "_playItem (%s \"%s\"): Left wait loop with state %d.",
          playlistItemGetType(item)==PlaylistItemStream?"stream":"track",
          playlistItemGetText(item), playbackThreadState );

/*------------------------------------------------------------------------*\
    Scrobble tracks after playing
\*------------------------------------------------------------------------*/
  if( playlistItemGetType(item)==PlaylistItemTrack &&
      ickScrobbleTrack(item,seekPos) )
    logerr( "_playItem (%s): Could not scrobble track.", playlistItemGetText(item)  );

/*------------------------------------------------------------------------*\
    Get rid of feed
\*------------------------------------------------------------------------*/
  if( feed && audioFeedDelete(feed,true) )
    logerr( "_playItem (%s): Could not delete feeder instance.", playlistItemGetText(item)  );

/*------------------------------------------------------------------------*\
    Get rid of codec instance
\*------------------------------------------------------------------------*/
  codecInstance = NULL;
  if( codecDeleteInstance(codecInst,true) )
    logerr( "_playItem (%s): Could not delete codec instance.", playlistItemGetText(item)  );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return retval;
}



/*=========================================================================*\
      Select an audio feed for a playlist item
        return opened feed on success, NULL on error
        *codec is set to the first matching codec
        format can be used to supply a preferred format and will be set to
               the streamRef hints (if available)
        timeout is the connection timeout in milliseconds
\*=========================================================================*/
static AudioFeed *_feedFromPlayListItem( PlaylistItem *item, Codec **codec, AudioFormat *format, int timeout )
{
  int          i;
  json_t      *jStreamingRefs;
  int          feedFlags = 0;
  AudioFeed   *feed = NULL;
  AudioFormat  refFormat;
  enum {
    FormatStrict,
    FormatIgnore
  }            formatMode;
  memcpy( &refFormat, format, sizeof(AudioFormat) );


/*------------------------------------------------------------------------*\
    Get flags for feed
\*------------------------------------------------------------------------*/
  if( playlistItemGetType(item)==PlaylistItemStream )
    feedFlags = FeedIcy;

/*------------------------------------------------------------------------*\
    Get stream hints 
\*------------------------------------------------------------------------*/
  playlistItemLock( item );
  jStreamingRefs = playlistItemGetStreamingRefs( item );
  playlistItemUnlock( item );
  if( !jStreamingRefs ) {
    logerr( "_feedFromPlayListItem (%s,%s): No streaming references found.",
              playlistItemGetText(item), playlistItemGetId(item) );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Loop over all hints 
\*------------------------------------------------------------------------*/
  for( formatMode=FormatStrict; formatMode<=FormatIgnore; formatMode++ ) {
    for( i=0; !feed&&i<json_array_size(jStreamingRefs); i++ ) {
      json_t      *jStreamRef = json_array_get( jStreamingRefs, i );
      json_t      *jObj;
      const char  *type;
      char        *uri;
      const char  *oAuthToken = NULL;

      // Reset Format
      memcpy( &refFormat, format, sizeof(AudioFormat) );

      // Get type
      jObj = json_object_get( jStreamRef, "format" );
      if( !jObj || !json_is_string(jObj) ) {
        playlistItemLock( item );
        logwarn( "_feedFromPlayListItem (%s,%s): StreamRef #%d contains no format!",
                playlistItemGetText(item), playlistItemGetId(item), i );
        playlistItemUnlock( item );
        continue;
      }
      type = json_string_value( jObj );

      // Get URI
      jObj = json_object_get( jStreamRef, "url" );
      if( !jObj || !json_is_string(jObj) ) {
        playlistItemLock( item );
        logwarn( "_feedFromPlayListItem (%s,%s): StreamRef #%d contains no URI!",
                playlistItemGetText(item), playlistItemGetId(item), i );
        playlistItemUnlock( item );
        continue;
      }

      // Try to resolve the URI (will allocate the string)
      uri = ickServiceResolveURI( json_string_value(jObj), "content" );
      if( !uri ) {
        playlistItemLock( item );
        logwarn( "_feedFromPlayListItem (%s,%s), StreamRef #%d: Cannot resolve URL \"%s\"!",
                 playlistItemGetText(item), playlistItemGetId(item), i,
                 json_string_value(jObj) );
        playlistItemUnlock( item );
        continue;
      }

      // Do we need authorization?
      jObj = json_object_get( jStreamRef, "intermediate" );
      if( json_is_true(jObj) ) {
        oAuthToken = ickCloudGetAccessToken();
        if( !oAuthToken ) {
          playlistItemLock( item );
          logwarn( "_feedFromPlayListItem (%s,%s), StreamRef #%d: Need token but device not yet registered.",
                   playlistItemGetText(item), playlistItemGetId(item), i );
          playlistItemUnlock( item );
          continue;
        }
      }

      // Get sample rate (optional)
      jObj = json_object_get( jStreamRef, "sampleRate" );
      if( jObj && json_is_integer(jObj) )
        refFormat.sampleRate = json_integer_value( jObj );
      else if( jObj && json_is_string(jObj) )    // workaround
        refFormat.sampleRate = atoi( json_string_value(jObj) );
      else
        refFormat.sampleRate = -1;

      // Get sample size (optional)
      jObj = json_object_get( jStreamRef, "sampleSize" );
      if( jObj && json_is_integer(jObj) )
        refFormat.bitWidth = json_integer_value( jObj );
      else if( jObj && json_is_string(jObj) )    // workaround
        refFormat.bitWidth = atoi( json_string_value(jObj) );
      else
        refFormat.bitWidth = -1;

      // Get number of channels (optional)
      jObj = json_object_get( jStreamRef, "channels" );
      if( jObj && json_is_integer(jObj) )
        refFormat.channels = json_integer_value( jObj );
      else if( jObj && json_is_string(jObj) )    // workaround
        refFormat.channels = atoi( json_string_value(jObj) );
      else
        refFormat.channels = -1;

      // Ignore format in second try
      if( formatMode==FormatIgnore ) {
        refFormat.sampleRate = -1;
        refFormat.channels = -1;
      }

      // Get first codec matching type and format
      *codec = codecFind( type, &refFormat, NULL );
      if( !*codec ) {
        logwarn( "_feedFromPlayListItem (%s,%s), StreamRef #%d: No codec found for %s, %s.",
                 playlistItemGetText(item), playlistItemGetId(item), i,
                 type, audioFormatStr(NULL,&refFormat) );
        Sfree( uri );
        continue;
      }

      // Try to open feed
      feed = audioFeedCreate( uri, oAuthToken, feedFlags, &_audioFeedCallback, item );
      if( !feed ) {
        logwarn( "_feedFromPlayListItem (%s,%s), StreamRef #%d: Could not open feed for \"%s\".",
                 playlistItemGetText(item), playlistItemGetId(item), i, uri );
        Sfree( uri );
        continue;
      }
      Sfree( uri );

      // Wait for connection
      if( audioFeedLockWaitForConnection(feed,timeout) ) {
        char *httpResponse = audioFeedGetResponseHeaderField( feed, NULL );
        logwarn( "_feedFromPlayListItem (%s,%s), StreamRef #%d: Connection error for \"%s\" (%s, \"%s\").",
                 playlistItemGetText(item), playlistItemGetId(item), i, audioFeedGetURI(feed), strerror(errno), httpResponse );
        Sfree( httpResponse );
        audioFeedDelete( feed, true );
        feed = NULL;
      }
      else
        audioFeedUnlock( feed );

    }  // for( i=0; !feed&&i<json_array_size(jStreamingRefs); i++ )
  }

/*------------------------------------------------------------------------*\
    Return result (if any)
\*------------------------------------------------------------------------*/
  if( feed )
    memcpy( format, &refFormat, sizeof(AudioFormat) );
  return feed;
}


/*=========================================================================*\
    Handle call backs from an audio data feed
\*=========================================================================*/
static int _audioFeedCallback( AudioFeed *feed, void *usrData )
{
  DBGMSG( "_audioFeedCallback (%p,%s): state %d.",
          feed, audioFeedGetURI(feed), audioFeedGetState(feed) );

/*------------------------------------------------------------------------*\
    Check state
\*------------------------------------------------------------------------*/
  switch( audioFeedGetState(feed) ) {

    // Ignored
    case FeedInitialized:
    case FeedTerminating:
    case FeedConnecting:
      break;

    // Connection established
    case FeedConnected:
      break;

    // Connection not established or terminated
    case FeedTerminatedOk:
    case FeedTerminatedError:

      break;

    // Unknown
    default:
      logerr( "_audioFeedCallback (%s): unknown state %d.",
               audioFeedGetURI(feed), audioFeedGetState(feed) );
      return -1;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Handle callbacks from codec format detection
\*=========================================================================*/
static int _codecNewFormatCallback( CodecInstance *instance, void *userData )
{
  AudioFormat       *backendFormat = userData;
  const AudioFormat *newFormat     = codecGetAudioFormat( instance );
  DBGMSG( "_codecFormatCallback (%p,%s): %s.",
          instance, instance->codec->name, audioFormatStr(NULL,newFormat) );

  // Format should be complete now, or something is wrong
  if( !audioFormatIsComplete(newFormat) ) {
    logerr( "_codecFormatCallback (%p,%s): Format is incomplete (%s).",
             instance, instance->codec->name, audioFormatStr(NULL,newFormat) );
    return -1;
  }

  // Did format change?
  if( audioFormatIsComplete(backendFormat) && audioFormatCompare(backendFormat,newFormat) ) {
#ifdef ICK_DEBUG
    char buffer[30];
    DBGMSG( "_codecFormatCallback (%p,%s): Format changed from %s to %s.",
            instance, instance->codec->name,
            audioFormatStr(buffer,backendFormat), audioFormatStr(NULL,newFormat) );
#endif
    // Fixme.
  }

  // Copy to local format
  memcpy( backendFormat, newFormat, sizeof(AudioFormat) );

  // That's all
  return 0;
}


/*=========================================================================*\
    Handle callbacks from codec meta data detection
\*=========================================================================*/
#ifdef ICK_RAWMETA
static void _codecMetaCallback( CodecInstance *instance, CodecMetaType mType, json_t *jMeta, void *userData )
{
  PlaylistItem *item     = userData;
  json_t       *jRawMeta = json_object_get( playlistItemGetJSON(item), "rawMeta" );

  DBGMSG( "_codecMetaCallback (%p,%s): type %d.",
          instance, instance->codec->name, mType );

  // Process ICY data
  if( mType==CodecMetaICY ) {
    if( !jRawMeta ) {
      jRawMeta = json_object();
      json_object_set_new( playlistItemGetJSON(item), "rawMeta", jRawMeta );
    }
    json_object_set( jRawMeta, "icyInband", jMeta );
  }

  // Process ID3V1
  else if( mType==CodecMetaID3V1 ) {
    if( !jRawMeta ) {
      jRawMeta = json_object();
      json_object_set_new( playlistItemGetJSON(item), "rawMeta", jRawMeta );
    }
    json_object_set( jRawMeta, "id3v1", jMeta );
  }

  // Process ID3V2
  else if( mType==CodecMetaID3V2 ) {
    if( !jRawMeta ) {
      jRawMeta = json_object();
      json_object_set_new( playlistItemGetJSON(item), "rawMeta", jRawMeta );
    }
    json_object_set( jRawMeta, "id3v2", jMeta );
  }

  // Nothing to do...
  else
    return;

  // Inform controller
  ickMessageNotifyPlayerState( NULL );
}
#endif


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/




