/*$*********************************************************************\

Name            : -

Source File     : hmiNCurses.c

Description     : Minimal HMI based on ncurses

Comments        : Own implementations should be named hmiXXXX.c and
                  selected by supplying a "hmi=XXXX" option to the configure script

Called by       : ickstream player

Calls           : 

Error Messages  : -
  
Date            : 16.04.2013

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
#include <ncurses.h>
#include <jansson.h>

#include "utils.h"
#include "playlist.h"
#include "player.h"
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
WINDOW * winTitle;
WINDOW * winConfig;
WINDOW * winStatus;


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
// None


/*=========================================================================*\
      Init HMI module
\*=========================================================================*/
int  hmiInit( void )
{
  DBGMSG( "Initializing HMI module..." );

/*------------------------------------------------------------------------*\
    NCurses initialization
\*------------------------------------------------------------------------*/
  initscr();
  cbreak();
  noecho();
  curs_set( 0 );
  clear();
  refresh();

/*------------------------------------------------------------------------*\
    Setup windows
\*------------------------------------------------------------------------*/
  int rWidth = MIN( 40, COLS/2 );
  winTitle  = newwin(  1, COLS,   0, 0 );
  winConfig = newwin( 20, COLS-rWidth, 2, rWidth+1 );
  winStatus = newwin( 20, rWidth, 2, 0 );

/*------------------------------------------------------------------------*\
    Hello world
\*------------------------------------------------------------------------*/
  wprintw( winTitle, "Initializing...\n" );
  wrefresh( winTitle );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Close HMI module
\*=========================================================================*/
void hmiShutdown( void )
{
  DBGMSG( "Shutting down HMI module..." );

/*------------------------------------------------------------------------*\
    Shutdown NCurses
\*------------------------------------------------------------------------*/
  endwin();
}


/*=========================================================================*\
      Player configuration (name, cloud access) changed
\*=========================================================================*/
void hmiNewConfig( void )
{
  ServiceListItem *service;

  DBGMSG( "hmiNewConfig: %s, \"%s\", \"%s\".",
      playerGetUUID(),  playerGetName(), playerGetToken()?"Cloud":"No Cloud" );

  wmove( winConfig, 0, 0 );
  wprintw( winConfig, "Player id    : %s\n", playerGetUUID() );
  wprintw( winConfig, "Player name  : \"%s\"\n", playerGetName() );
  wprintw( winConfig, "Cloud status : %s\n", playerGetToken()?"Registered":"Unregistered" );

  for( service=ickServiceFind(NULL,NULL,NULL,0); service;
       service=ickServiceFind(service,NULL,NULL,0) )
    wprintw( winConfig, "Service      : \"%s\" (%s)\n", ickServiceGetName(service),
            ickServiceGetType(service)  );

  wrefresh( winConfig );
}


/*=========================================================================*\
      Queue cursor is pointing to a new item
        item might be NULL, if no current track is defined...
\*=========================================================================*/
void hmiNewItem( Playlist *plst, PlaylistItem *item )
{
  DBGMSG( "hmiNewItem: %p (%s).", item, item?playlistItemGetText(item):"<None>" );
  currentItem = item;

  wclear( winTitle );

  playlistLock( plst );
  if( item )
    playlistItemLock( item );

  if( item )
    wprintw( winTitle, "%s: \"%s\"\n",
      playlistItemGetType(item)==PlaylistItemStream?"Stream":"Track",
      playlistItemGetText(item) );

  playlistUnlock( plst );
  if( item )
    playlistItemUnlock( item );

  wrefresh( winTitle );

  wmove( winStatus, 2, 0 );
  wprintw( winStatus, "Playback position: %d/%d\n", playlistGetCursorPos(plst)+1, playlistGetLength(plst) );
  wrefresh( winStatus );

}


/*=========================================================================*\
      Player state has changed
\*=========================================================================*/
void hmiNewState( PlayerState state )
{
  DBGMSG( "hmiNewState: %d.", state );

  char *stateStr = "Unknown";
  switch( state ) {
    case PlayerStateStop:  stateStr = "Stopped"; break;	
    case PlayerStatePlay:  stateStr = "Playing"; break;
    case PlayerStatePause: stateStr = "Paused"; break;
  }

  wmove( winStatus, 3, 0 );
  wprintw( winStatus, "Playback state   : %s\n", stateStr );
  wrefresh(winStatus );
}


/*=========================================================================*\
      Player repeat mode has changed
\*=========================================================================*/
void hmiNewRepeatMode( PlayerRepeatMode mode )
{
  DBGMSG( "hmiNewRepeatMode: %d.", mode );

  char *modeStr = "Unknown";
  switch( mode ) {
    case PlayerRepeatOff:     modeStr = "Off"; break;	
    case PlayerRepeatItem:    modeStr = "Track"; break;
    case PlayerRepeatQueue:   modeStr = "Queue"; break;
    case PlayerRepeatShuffle: modeStr = "Shuffle"; break;
  }

  wmove( winStatus, 5, 0 );
  wprintw( winStatus, "Repeat mode      : %s\n", modeStr );
  wrefresh( winStatus );
}


/*=========================================================================*\
      Volume and muting setting has changed
\*=========================================================================*/
void hmiNewVolume( double volume, bool muted )
{
  DBGMSG( "hmiNewVolume: %.2lf (muted: %s).", volume, muted?"On":"Off" );

  wmove( winStatus, 4, 0 );
  wprintw( winStatus, "Playback volume  : %.2lf (%s)\n", volume, muted?"muted":"not muted" );
  wrefresh( winStatus );
}


/*=========================================================================*\
      Audio backend format has changed
\*=========================================================================*/
void hmiNewFormat( AudioFormat *format )
{
  char buffer[64];

  DBGMSG( "hmiNewFormat: %s.", audioFormatStr(NULL,format) );

  wmove( winStatus, 1, 0 );
  wprintw( winStatus, "Playback format  : %s\n", audioFormatStr(buffer,format) );
  wrefresh( winStatus );
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

  wmove( winStatus, 0, 0 );
  if( h )
    wprintw( winStatus, "Playback position: %d:%02d:%02d%s\n", h, m, s, buf );
  else
    wprintw( winStatus, "Playback position: %d:%02d%s\n", m, s, buf );
  wrefresh( winStatus );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/




