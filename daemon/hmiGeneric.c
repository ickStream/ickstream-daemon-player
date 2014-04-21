/*$*********************************************************************\

Name            : -

Source File     : hmiGeneric.c

Description     : Minimal HMI implementation 

Comments        : usefull for debugging only.
                  Own implementations hould be named hmiXXXX.c and 
                  selected by supplying a "hmi=XXXX" option to the configure script

Called by       : ickstream player

Calls           : 

Error Messages  : -
  
Date            : 17.03.2013

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
#include <jansson.h>

#include "ickutils.h"
#include "playlist.h"
#include "player.h"
#include "ickCloud.h"
#include "ickService.h"

/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/
// none
 

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static PlaylistItem *currentItem;


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
// None


/*=========================================================================*\
      Init HMI module with command line arguments
\*=========================================================================*/
int  hmiInit( int *argc, char *(*argv[]) )
{
  DBGMSG( "Init HMI..." );
  return 0;
}


/*=========================================================================*\
      Create HMI module
\*=========================================================================*/
int  hmiCreate( void )
{
  DBGMSG( "Creating HMI..." );
  return 0;
}


/*=========================================================================*\
      Close HMI module
\*=========================================================================*/
void hmiShutdown( void )
{
  DBGMSG( "Shutting down HMI module..." );
}

/*=========================================================================*\
      Player configuration (name, cloud access) changed
\*=========================================================================*/
void hmiNewConfig( void )
{
  ServiceListItem *service;

  DBGMSG( "hmiNewConfig: %s, \"%s\", \"%s\".",
      playerGetUUID(),  playerGetName(), ickCloudGetAccessToken()?"Cloud":"No Cloud" );

  printf( "HMI Player id        : %s\n", playerGetUUID() );
  printf( "HMI Player name      : \"%s\"\n", playerGetName() );
  printf( "HMI Cloud status     : %s\n", ickCloudGetAccessToken()?"Registered":"Unregistered" );

  for( service=ickServiceFind(NULL,NULL,NULL,0); service;
       service=ickServiceFind(service,NULL,NULL,0) )
    printf( "HMI Service          : \"%s\" (%s)\n", ickServiceGetName(service),
            ickServiceGetType(service)  );

}

/*=========================================================================*\
      Queue changed or cursor is pointing to a new item
\*=========================================================================*/
void hmiNewQueue( Playlist *plst )
{
  PlaylistItem *item = playlistGetCursorItem( plst );

  DBGMSG( "hmiNewQueue: %p (%s).", item, item?playlistItemGetText(item):"<None>" );
  currentItem = item;

  playlistLock( plst );
  if( item )
    playlistItemLock( item );

  printf( "HMI Playback track   : (%d/%d) \"%s\"\n", playlistGetCursorPos(plst)+1, 
           playlistGetLength(plst), item?playlistItemGetText(item):"<None>" );

  playlistUnlock( plst );
  if( item )
    playlistItemUnlock( item );

}


/*=========================================================================*\
      Player state has changed
\*=========================================================================*/
void hmiNewState( PlayerState state )
{
  DBGMSG( "hmiNewState: %d (%s).", state, playerStateToStr(state) );

  printf( "HMI Playback state   : %s\n", playerStateToStr(state) );
}


/*=========================================================================*\
      Player playback mode has changed
\*=========================================================================*/
void hmiNewPlaybackMode( PlayerPlaybackMode mode )
{
  DBGMSG( "hmiNewPlaybackMode: %d.", mode );

  char *modeStr = "Unknown";
  switch( mode ) {
    case PlaybackQueue:         modeStr = "Queue"; break;
    case PlaybackShuffle:       modeStr = "Shuffle"; break;
    case PlaybackRepeatQueue:   modeStr = "Repeat Queue"; break;
    case PlaybackRepeatItem:    modeStr = "Repeat Item"; break;
    case PlaybackRepeatShuffle: modeStr = "Repeat and Shuffle"; break;
    case PlaybackDynamic:       modeStr = "Dynamic Playlist"; break;
  }

  printf( "HMI playback mode    : %s\n", modeStr );
}


/*=========================================================================*\
      Volume and muting setting has changed
\*=========================================================================*/
void hmiNewVolume( double volume, bool muted )
{
  DBGMSG( "hmiNewVolume: %.2lf (muted: %s).", volume, muted?"On":"Off" );

  printf( "HMI Playback volume  : %.2lf (%s)\n", volume, muted?"muted":"not muted" );
}


/*=========================================================================*\
      Audio backend format has changed
\*=========================================================================*/
void hmiNewFormat( AudioFormat *format )
{
  char buffer[64];

  DBGMSG( "hmiNewFormat: %s.", audioFormatStr(NULL,format) );

  printf( "HMI Playback format  : %s\n", audioFormatStr(buffer,format) );  
}


/*=========================================================================*\
      New seek Position
\*=========================================================================*/
void hmiNewPosition( double seekPos )
{
  int h, m, s;
  int d = 0;
  char buf[20];

  if( currentItem )
    d = playlistItemGetDuration( currentItem );

  DBGMSG( "hmiNewPosition: %.2lf/%.2lf", seekPos, d );

  if( seekPos>=0 && d>0 )
    snprintf(buf,sizeof(buf)-1," (%3d%%)", (int)(seekPos*100/d+.5) );
  else
    *buf = 0;

  h = (int)seekPos/3600;
  seekPos -= h*3600;
  m = (int)seekPos/60;
  seekPos -= m*60;
  s = (int)seekPos;

  if( h )
    printf( "HMI Playback position: %d:%02d:%02d%s\n", h, m, s, buf );
  else
    printf( "HMI Playback position: %d:%02d%s\n", m, s, buf );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/




