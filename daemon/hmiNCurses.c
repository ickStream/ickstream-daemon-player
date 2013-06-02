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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <ncurses.h>
#include <jansson.h>

#include "ickutils.h"
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
enum PipeIndex { PipeRead, PipeWrite };

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static PlaylistItem *currentItem;
static WINDOW       *winTitle;
static WINDOW       *winConfig;
static WINDOW       *winStatus;
static WINDOW       *winLog;
static int           pipefd[2];
static int           oldStderrFd;
pthread_mutex_t      mutex;



/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static void *_captureThread( void *arg );


/*=========================================================================*\
      Init HMI module with command line arguments
\*=========================================================================*/
int hmiInit( int *argc, char *(*argv[]) )
{
  DBGMSG( "Init HMI..." );
  return 0;
}

/*=========================================================================*\
      Create HMI
\*=========================================================================*/
int hmiInit( void )
{
  pthread_t            thread;
  int                  rc;
  pthread_mutexattr_t  attr;

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
  winTitle  = newwin(  1, COLS,        0, 0 );
  winConfig = newwin( 20, COLS-rWidth, 5, rWidth+1 );
  winStatus = newwin( 20, rWidth,      5, 0 );
  winLog    = newwin( 10, COLS,       25, 0);
  scrollok( winLog, true );

/*------------------------------------------------------------------------*\
    Init mutex
\*------------------------------------------------------------------------*/
  pthread_mutexattr_init( &attr );
  pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_ERRORCHECK );
  pthread_mutex_init( &mutex, &attr );

/*------------------------------------------------------------------------*\
    Create pipe and redirect stderr
\*------------------------------------------------------------------------*/
  if( pipe(pipefd) ) {
    logerr( "hmiInit: could not create pipe (%s).", strerror(errno) );
    return -1;
  }
  oldStderrFd = dup( fileno(stderr) );
  fflush( stderr );
  dup2( pipefd[PipeWrite], fileno(stderr) );

/*------------------------------------------------------------------------*\
    Start thread for capturing stderr
\*------------------------------------------------------------------------*/
  rc = pthread_create( &thread, NULL, _captureThread, NULL );
  if( rc ) {
    logerr( "hmiInit: Unable to start capture thread (%s).", strerror(rc) );
    close( pipefd[0] );
    close( pipefd[1] );
    return -1;
  }

/*------------------------------------------------------------------------*\
    We don't care for that thread any more
\*------------------------------------------------------------------------*/
  rc = pthread_detach( thread );
  if( rc )
    logerr( "hmiInit: Unable to detach request thread (%s).", strerror(rc) );

/*------------------------------------------------------------------------*\
    Hello world
\*------------------------------------------------------------------------*/
  fprintf( stderr, "Initializing...\n" );

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
    End capturing of stderr
\*------------------------------------------------------------------------*/
  fflush( stderr );
  dup2( oldStderrFd, fileno(stderr) );
  close( pipefd[PipeWrite] );

/*------------------------------------------------------------------------*\
    Delete mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &mutex );

/*------------------------------------------------------------------------*\
    Shutdown NCurses
\*------------------------------------------------------------------------*/
  endwin();
}


/*=========================================================================*\
      Thread for capturing stdout to a ncurses window
\*=========================================================================*/
static void *_captureThread( void *arg )
{
  int maxx, maxy;
  char buffer[256];
  int  rc;

  DBGMSG( "NCurses capture thread starting." );
  PTHREADSETNAME( "ncurses" );

/*------------------------------------------------------------------------*\
    Block broken pipe signals from this thread
\*------------------------------------------------------------------------*/
  sigset_t sigSet;
  sigemptyset( &sigSet );
  sigaddset( &sigSet, SIGPIPE );
  pthread_sigmask( SIG_BLOCK, NULL, &sigSet );

/*------------------------------------------------------------------------*\
    Get window size
\*------------------------------------------------------------------------*/
  getmaxyx( winLog, maxy, maxx );

/*------------------------------------------------------------------------*\
    Loop while pipe is active
\*------------------------------------------------------------------------*/
  for(;;) {
    char *chr;

    rc = read( pipefd[PipeRead], buffer, sizeof(buffer)-1 );

    // Any error?
    if( rc<0 ) {
      logerr( "NCurses capture thread: could not read pipe (%s).", strerror(errno) );
      break;
    }

    // Terminate buffer
    buffer[ rc ] = 0;

    // End of input
    if( !rc )
      break;

    // Do the output - that's an awful quick hack meant only for debugging purposes...
    pthread_mutex_lock( &mutex );
    for( chr=buffer; *chr; chr++ ) {
      int x, y;

      // Character
      if( *chr!='\n' ) {
        waddch( winLog, *chr );
        getyx( winLog, y, x );
        if( x>=maxx ) {
          x = 0;
          y++;
        }
      }

      // new line
      else {
        getyx( winLog, y, x );
        y++;
        x = 0;
      }

      // need to scroll
      if( y>=maxy ) {
        y--;
        wscrl( winLog, 1 );
      }

      // need to move cursor
      if( !x )
        wmove( winLog, y, x );
    }
    wrefresh( winLog );
    pthread_mutex_unlock( &mutex );

  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return NULL;
}


/*=========================================================================*\
      Player configuration (name, cloud access) changed
\*=========================================================================*/
void hmiNewConfig( void )
{
  ServiceListItem *service;

  DBGMSG( "hmiNewConfig: %s, \"%s\", \"%s\".",
      playerGetUUID(),  playerGetName(), playerGetToken()?"Cloud":"No Cloud" );

  pthread_mutex_lock( &mutex );
  wmove( winConfig, 0, 0 );
  wprintw( winConfig, "Player id    : %s\n", playerGetUUID() );
  wprintw( winConfig, "Player name  : \"%s\"\n", playerGetName() );
  wprintw( winConfig, "Cloud status : %s\n", playerGetToken()?"Registered":"Unregistered" );

  for( service=ickServiceFind(NULL,NULL,NULL,0); service;
       service=ickServiceFind(service,NULL,NULL,0) )
    wprintw( winConfig, "Service      : \"%s\" (%s)\n", ickServiceGetName(service),
            ickServiceGetType(service)  );

  wrefresh( winConfig );
  pthread_mutex_unlock( &mutex );

}


/*=========================================================================*\
      Queue changed or cursor is pointing to a new item
\*=========================================================================*/
void hmiNewQueue( Playlist *plst )
{
  PlaylistItem *item = playlistGetCursorItem( plst );

  DBGMSG( "hmiNewQueue: %p (%s).", item, item?playlistItemGetText(item):"<None>" );
  currentItem = item;

  pthread_mutex_lock( &mutex );
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
  pthread_mutex_unlock( &mutex );

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

  pthread_mutex_lock( &mutex );
  wmove( winStatus, 3, 0 );
  wprintw( winStatus, "Playback state   : %s\n", stateStr );
  wrefresh(winStatus );
  pthread_mutex_unlock( &mutex );

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

  pthread_mutex_lock( &mutex );
  wmove( winStatus, 5, 0 );
  wprintw( winStatus, "Repeat mode      : %s\n", modeStr );
  wrefresh( winStatus );
  pthread_mutex_unlock( &mutex );

}


/*=========================================================================*\
      Volume and muting setting has changed
\*=========================================================================*/
void hmiNewVolume( double volume, bool muted )
{
  DBGMSG( "hmiNewVolume: %.2lf (muted: %s).", volume, muted?"On":"Off" );

  pthread_mutex_lock( &mutex );
  wmove( winStatus, 4, 0 );
  wprintw( winStatus, "Playback volume  : %.2lf (%s)\n", volume, muted?"muted":"not muted" );
  wrefresh( winStatus );
  pthread_mutex_unlock( &mutex );

}


/*=========================================================================*\
      Audio backend format has changed
\*=========================================================================*/
void hmiNewFormat( AudioFormat *format )
{
  char buffer[64];

  DBGMSG( "hmiNewFormat: %s.", audioFormatStr(NULL,format) );

  pthread_mutex_lock( &mutex );
  wmove( winStatus, 1, 0 );
  wprintw( winStatus, "Playback format  : %s\n", audioFormatStr(buffer,format) );
  wrefresh( winStatus );
  pthread_mutex_unlock( &mutex );

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

  pthread_mutex_lock( &mutex );
  wmove( winStatus, 0, 0 );
  if( h )
    wprintw( winStatus, "Playback position: %d:%02d:%02d%s\n", h, m, s, buf );
  else
    wprintw( winStatus, "Playback position: %d:%02d%s\n", m, s, buf );
  wrefresh( winStatus );
  pthread_mutex_unlock( &mutex );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/




