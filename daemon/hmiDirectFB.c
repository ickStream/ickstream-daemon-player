/*$*********************************************************************\

Name            : -

Source File     : hmiDirectFB.c

Description     : HMI implementation for direct frame buffer

Comments        : selected by supplying "hmi=DirectFB" option to the configure script

Called by       : ickstream player

Calls           : 

Error Messages  : -
  
Date            : 28.05.2013

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
#include <stdbool.h>
#include <jansson.h>
#include <directfb.h>

#include "hmi.h"
#include "ickutils.h"
#include "playlist.h"
#include "player.h"
#include "ickService.h"
#include "dfbTools.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/
#define DFB_ITEMS 7

/*=========================================================================*\
	Private symbols
\*=========================================================================*/


static DFBRectangle       artRect;
//static const char        *fontfile = ICK_DFBFONT;
static const char        *fontfile = "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf";
static IDirectFBFont     *font1;
static IDirectFBFont     *font2;
static IDirectFBFont     *font3;

static DfbtWidget        *wPlaylist;       // strong
static DfbtWidget        *wArtwork;        // strong
static DfbtWidget        *wStatus;         // strong
static DfbtWidget        *wConfig;         // strong
static DfbtWidget        *wStateIcon;      // strong
static DfbtWidget        *wRepeatIcon;     // strong
static DfbtWidget        *wShuffleIcon;    // strong
static DfbtWidget        *wSourceIcon;     // strong
static DfbtWidget        *wFormatString1;  // strong
static DfbtWidget        *wFormatString2;  // strong
static DfbtWidget        *wVolumeIcon;     // strong
static DfbtWidget        *wVolumeString;   // strong
static DfbtWidget        *wPositionIcon;   // strong
static DfbtWidget        *wPositionString; // strong

static PlaylistItem *currentItem;


                          // A,    R,    G,    B
static DFBColor cWhite   = { 0xFF, 0xFF, 0xFF, 0xFF };
static DFBColor cRed     = { 0xFF, 0x80, 0x00, 0x00 };
static DFBColor cBlue    = { 0xFF, 0x00, 0x00, 0x80 };
static DFBColor cGray    = { 0xFF, 0x20, 0x20, 0x20 };


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static DfbtWidget *wPlaylistItem( PlaylistItem *item, int pos, int width, int height, bool isCursor );


/*=========================================================================*\
      Init HMI module with command line arguments
\*=========================================================================*/
int hmiInit( int *argc, char *(*argv[]) )
{
  DBGMSG( "hmiInit: input %d args", *argc );

  DFBResult drc = DirectFBInit( argc, argv );
  if( drc!=DFB_OK ) {
    logerr( "hmiInit: could not init direct fb (%s).", DirectFBErrorString(drc) );
    return -1;
  }

  DirectFBSetOption( "no-vt", NULL );
  DirectFBSetOption( "no-sighandler", NULL );

  DBGMSG( "hmiInit: output %d args", *argc );

  return 0;
}


/*=========================================================================*\
      Create HMI entity
\*=========================================================================*/
int hmiCreate( void )
{
  IDirectFB            *dfb;
  DfbtWidget           *screen;
  int                   width, height;
  DFBResult             drc;
  DFBFontDescription    fdsc;
  DBGMSG( "Initializing HMI module..." );

/*------------------------------------------------------------------------*\
    Set some direct FB options
\*------------------------------------------------------------------------*/
  DirectFBSetOption( "no-cursor", NULL );
  DirectFBSetOption ("bg-none", NULL);
  DirectFBSetOption ("no-init-layer", NULL);

/*------------------------------------------------------------------------*\
    Init direct frema buffer and build up screen
\*------------------------------------------------------------------------*/
  if( dfbtInit("../resources") )
    return -1;

  dfb = dfbtGetDdb();
  screen = dfbtGetScreen();
  dfbtGetSize( screen, &width, &height );

/*------------------------------------------------------------------------*\
    Get fonts
\*------------------------------------------------------------------------*/
  fdsc.flags      = DFDESC_HEIGHT | DFDESC_ATTRIBUTES;
  fdsc.height     = 1 + 24*height/1024;
  fdsc.attributes = DFFA_NONE;
  drc = dfb->CreateFont( dfb, fontfile, &fdsc, &font1 );
  if( drc!=DFB_OK ) {
    logerr( "hmiInit: could not find font %s (%s).", fontfile, DirectFBErrorString(drc) );
    return -1;
  }

  fdsc.flags      = DFDESC_HEIGHT | DFDESC_ATTRIBUTES;
  fdsc.height     = 1 + 48*height/1024;
  fdsc.attributes = DFFA_NONE;
  drc = dfb->CreateFont( dfb, fontfile, &fdsc, &font2 );
  if( drc!=DFB_OK ) {
    logerr( "hmiInit: could not find font %s (%s).", fontfile, DirectFBErrorString(drc) );
    DFBRELEASE( font1 );
    return -1;
  }

  fdsc.flags      = DFDESC_HEIGHT | DFDESC_ATTRIBUTES;
  fdsc.height     = 1 + 16*height/1024;
  fdsc.attributes = DFFA_NONE;
  drc = dfb->CreateFont( dfb, fontfile, &fdsc, &font3 );
  if( drc!=DFB_OK ) {
    logerr( "hmiInit: could not find font %s (%s).", fontfile, DirectFBErrorString(drc) );
    DFBRELEASE( font1 );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Define area for cover art
\*------------------------------------------------------------------------*/
  int border = height/100;
  artRect.w = (DFB_ITEMS-2)*(height/DFB_ITEMS)-2*border;
  artRect.h = artRect.w;
  artRect.x = (width/2-artRect.w)/2;
  artRect.y = border;

/*------------------------------------------------------------------------*\
    Load logo as default artwork
\*------------------------------------------------------------------------*/
  wArtwork = dfbtImage( artRect.w, artRect.h, "icklogo.png", true );
  if( wArtwork ) {
   // dfbtSetBackground( wArtwork, &cBlue );
   dfbtContainerAdd( screen, wArtwork, artRect.x, artRect.y, DfbtAlignTopLeft );
  }

/*------------------------------------------------------------------------*\
    Create and add container for playlist elements
\*------------------------------------------------------------------------*/
  wPlaylist = dfbtContainer( width/2, height );
  dfbtContainerAdd( screen, wPlaylist, width, 0, DfbtAlignTopRight );
  dfbtSetName( wPlaylist, "Playlist Window" );

/*------------------------------------------------------------------------*\
    Create and add container for status elements
\*------------------------------------------------------------------------*/
  wStatus = dfbtContainer( width/2-2*border, height/DFB_ITEMS-border );
  dfbtSetBackground( wStatus, &cRed );
  dfbtContainerAdd( screen, wStatus, border, artRect.y+artRect.h+border, DfbtAlignTopLeft );
  dfbtSetName( wStatus, "Status Window" );

/*------------------------------------------------------------------------*\
    Create and add container for config elements
\*------------------------------------------------------------------------*/
  wConfig = dfbtContainer( width/2-2*border, height/DFB_ITEMS-border );
  dfbtSetBackground( wConfig, &cGray );
  dfbtContainerAdd( screen, wConfig, border, artRect.y+artRect.h+height/DFB_ITEMS+border, DfbtAlignTopLeft );
  dfbtSetName( wConfig, "Config Window" );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  dfbtRedrawScreen( false );
  return 0;
}


/*=========================================================================*\
      Close HMI module
\*=========================================================================*/
void hmiShutdown( void )
{
  DBGMSG( "Shutting down HMI module..." );

/*------------------------------------------------------------------------*\
    Get rid of global containers
\*------------------------------------------------------------------------*/
  dfbtRelease( wPlaylist );
  dfbtRelease( wArtwork );
  dfbtRelease( wStatus );
  dfbtRelease( wConfig );
  dfbtRelease( wStateIcon );
  dfbtRelease( wRepeatIcon );
  dfbtRelease( wShuffleIcon );
  dfbtRelease( wSourceIcon );
  dfbtRelease( wFormatString1 );
  dfbtRelease( wFormatString2 );
  dfbtRelease( wVolumeIcon );
  dfbtRelease( wVolumeString );
  dfbtRelease( wPositionIcon );
  dfbtRelease( wPositionString );

/*------------------------------------------------------------------------*\
    Release fonts and primary/super interfaces
\*------------------------------------------------------------------------*/
  DFBRELEASE( font1 );
  DFBRELEASE( font2 );
  DFBRELEASE( font3 );

/*------------------------------------------------------------------------*\
    Shut down toolkit
\*------------------------------------------------------------------------*/
  dfbtShutdown();
}


/*=========================================================================*\
      Player configuration (name, cloud access) changed
\*=========================================================================*/
void hmiNewConfig( void )
{
  DfbtWidget      *wTxt;
  char            *txt;
  int              width, height, border;
  int              txt_x, txt_y;
  int              a;
  size_t           len;

  ServiceListItem *service;

  DBGMSG( "hmiNewConfig: %s, \"%s\", \"%s\".",
      playerGetUUID(),  playerGetName(), playerGetToken()?"Cloud":"No Cloud" );

/*------------------------------------------------------------------------*\
    Clear container
\*------------------------------------------------------------------------*/
  dfbtContainerRemove( wConfig, NULL );
  dfbtGetSize( wConfig, &width, &height );
  border = width/100;
  txt_x  = 0;
  txt_y  = 0;
  font3->GetStringWidth( font3, "Services:__", -1, &txt_x );
  txt_x  += border;

/*------------------------------------------------------------------------*\
    Show player name, id and registration status
\*------------------------------------------------------------------------*/
  font3->GetHeight( font3, &a );
  txt_y += a;
  wTxt = dfbtText( "Player:", font3, &cWhite );
  dfbtContainerAdd( wConfig, wTxt, border, txt_y, DfbtAlignBaseLeft );
  dfbtRelease( wTxt );
  txt = malloc( 30+strlen(playerGetUUID())+strlen(playerGetName()) );
  sprintf( txt, "\"%s\", %s (%s)", playerGetName(),
           playerGetToken()?"registered":"unregistered", playerGetUUID() );
  wTxt = dfbtText( txt, font3, &cWhite );
  dfbtContainerAdd( wConfig, wTxt, txt_x, txt_y, DfbtAlignBaseLeft );
  dfbtRelease( wTxt );
  Sfree( txt );

/*------------------------------------------------------------------------*\
    Show content services
\*------------------------------------------------------------------------*/
  font3->GetHeight( font3, &a );
  txt_y += a;
  wTxt = dfbtText( "Content:", font3, &cWhite );
  dfbtContainerAdd( wConfig, wTxt, border, txt_y, DfbtAlignBaseLeft );
  dfbtRelease( wTxt );
  len = 1;
  for( service=ickServiceFind(NULL,NULL,NULL,0); service;
       service=ickServiceFind(service,NULL,NULL,0) ) {
    if( strcmp(ickServiceGetType(service),"content") )
      continue;
    len += strlen( ickServiceGetName(service) )+2;
  }
  txt = malloc( len );
  *txt = 0;
  for( service=ickServiceFind(NULL,NULL,NULL,0); service;
       service=ickServiceFind(service,NULL,NULL,0) ) {
    if( strcmp(ickServiceGetType(service),"content") )
      continue;
    if( *txt )
      strcat( txt, ", " );
    strcat( txt, ickServiceGetName(service) );
  }
  wTxt = dfbtText( txt, font3, &cWhite );
  dfbtContainerAdd( wConfig, wTxt, txt_x, txt_y, DfbtAlignBaseLeft );
  dfbtRelease( wTxt );
  Sfree( txt );

/*------------------------------------------------------------------------*\
    Show other services
\*------------------------------------------------------------------------*/
  font3->GetHeight( font3, &a );
  txt_y += a;
  wTxt = dfbtText( "Services:", font3, &cWhite );
  dfbtContainerAdd( wConfig, wTxt, border, txt_y, DfbtAlignBaseLeft );
  dfbtRelease( wTxt );
  len = 1;
  for( service=ickServiceFind(NULL,NULL,NULL,0); service;
       service=ickServiceFind(service,NULL,NULL,0) ) {
    if( !strcmp(ickServiceGetType(service),"content") )
      continue;
    len += strlen( ickServiceGetType(service) )+2;
  }
  txt = malloc( len );
  *txt = 0;
  for( service=ickServiceFind(NULL,NULL,NULL,0); service;
       service=ickServiceFind(service,NULL,NULL,0) ) {
    if( !strcmp(ickServiceGetType(service),"content") )
      continue;
    if( *txt )
      strcat( txt, ", " );
    strcat( txt, ickServiceGetType(service) );
  }
  wTxt = dfbtText( txt, font3, &cWhite );
  dfbtContainerAdd( wConfig, wTxt, txt_x, txt_y, DfbtAlignBaseLeft );
  dfbtRelease( wTxt );
  Sfree( txt );

/*------------------------------------------------------------------------*\
    Trigger reconstruction of queue since images might have become
    available through newly added content services
    This also triggers a redraw
\*------------------------------------------------------------------------*/
//  dfbtRedrawScreen( false );
  hmiNewQueue( playerGetQueue() );
}


/*=========================================================================*\
      Queue changed or cursor is pointing to a new item
\*=========================================================================*/
void hmiNewQueue( Playlist *plst )
{
  int i;
  DfbtWidget   *screen = dfbtGetScreen();
  DfbtWidget   *wItem;
  int           width, height, border, size;
  PlaylistItem *item = playlistGetCursorItem( plst );

  DBGMSG( "hmiNewQueue: %p (%s).", item, item?playlistItemGetText(item):"<None>" );
  currentItem = item;


/*------------------------------------------------------------------------*\
    Get geometry
\*------------------------------------------------------------------------*/
  dfbtGetSize( wPlaylist, &width, &height );
  border = height/100;
  height = height / DFB_ITEMS;

/*------------------------------------------------------------------------*\
    Clear artwork, source icon and playlist container
\*------------------------------------------------------------------------*/
  if( wArtwork ) {
    dfbtContainerRemove( screen, wArtwork );
    dfbtRelease( wArtwork );
    wArtwork = NULL;
  }
  if( wSourceIcon ) {
    dfbtContainerRemove( wStatus, wSourceIcon );
    dfbtRelease( wSourceIcon );
    wSourceIcon = NULL;
  }
  dfbtContainerRemove( wPlaylist, NULL );

  wArtwork = dfbtImage( artRect.w, artRect.h, "icklogo.png", true );
  if( wArtwork )
    dfbtContainerAdd( screen, wArtwork, artRect.x, artRect.y, DfbtAlignTopLeft );

  dfbtRedrawScreen( false );
  return;

/*------------------------------------------------------------------------*\
    Show Artwork
\*------------------------------------------------------------------------*/
  if( item ) {
    json_t *jObj = playlistItemGetAttribute( item, "image" );
    if( jObj && json_is_string(jObj) ) {
      char *uri = ickServiceResolveURI( json_string_value(jObj), "content" );
      if( uri )
        wArtwork = dfbtImage( artRect.w, artRect.h, uri, false );
    }
  }
  if( !wArtwork )
    wArtwork = dfbtImage( artRect.w, artRect.h, "icklogo.png", true );
  if( wArtwork ) {
    // dfbtSetBackground( wArtwork, &cBlue );
    dfbtContainerAdd( screen, wArtwork, artRect.x, artRect.y, DfbtAlignTopLeft );
  }


/*------------------------------------------------------------------------*\
    Create widgets for playback queue items
\*------------------------------------------------------------------------*/
  playlistLock( plst );
  for( i=0; i<DFB_ITEMS; i++ ) {
    int           pos     = playlistGetCursorPos(plst)-DFB_ITEMS/2 + i;
    PlaylistItem *theItem = playlistGetItem( plst, PlaylistMapped, pos );

    if( theItem )
      playlistItemLock( theItem );

    wItem = wPlaylistItem( theItem, pos, width-border, height-border, item && theItem==item );
    dfbtContainerAdd( wPlaylist, wItem, 0, i*height, DfbtAlignTopLeft );
    dfbtRelease( wItem );

    if( theItem )
      playlistItemUnlock( theItem );
  }
  playlistUnlock( plst );

/*------------------------------------------------------------------------*\
    Get geometry
\*------------------------------------------------------------------------*/
  dfbtGetSize( wStatus, &width, &height );
  size = (height-2*border)/2;

/*------------------------------------------------------------------------*\
    Show Source Icon
\*------------------------------------------------------------------------*/
  if( item ) {
    switch( playlistItemGetType(item) ) {
      case PlaylistItemTrack:
        wSourceIcon = dfbtImage( size, size, "ickSourceTrack.png", true );
        break;
      case PlaylistItemStream:
        wSourceIcon = dfbtImage( size, size, "ickSourceStream.png", true );
        break;
    }
    dfbtContainerAdd( wStatus, wSourceIcon, width-3*border-2*size, border, DfbtAlignTopRight );
  }

/*------------------------------------------------------------------------*\
    Trigger redraw
\*------------------------------------------------------------------------*/
  dfbtRedrawScreen( false );
}


/*=========================================================================*\
      Player state has changed
\*=========================================================================*/
void hmiNewState( PlayerState state )
{
  DfbtWidget   *screen = dfbtGetScreen();
  int           width, height, border;

  DBGMSG( "hmiNewState: %d.", state );

/*------------------------------------------------------------------------*\
    Get geometry
\*------------------------------------------------------------------------*/
  dfbtGetSize( screen, &width, &height );
  border = height/100;
  dfbtGetSize( wStatus, &width, &height );
  height -= 2*border;
  width   = height;

/*------------------------------------------------------------------------*\
    Clear state icon
\*------------------------------------------------------------------------*/
  if( wStateIcon ) {
    dfbtContainerRemove( wStatus, wStateIcon );
    dfbtRelease( wStateIcon );
    wStateIcon = NULL;
  }

/*------------------------------------------------------------------------*\
    Get and draw new icon
\*------------------------------------------------------------------------*/
  switch( state ) {
    case PlayerStateStop:
      wStateIcon = dfbtImage( width, height, "ickStateStop.png", true );
      break;
    case PlayerStatePlay:
      wStateIcon = dfbtImage( width, height, "ickStatePlay.png", true );
      break;
    case PlayerStatePause:
      wStateIcon = dfbtImage( width, height, "ickStatePause.png", true );
      break;
    default:
      logerr( "hmiNewState: unknown sate %d." );
      break;
  }
  if( wStateIcon )
    dfbtContainerAdd( wStatus, wStateIcon, border, border, DfbtAlignTopLeft );

/*------------------------------------------------------------------------*\
    Trigger redraw
\*------------------------------------------------------------------------*/
  dfbtRedrawScreen( false );
}


/*=========================================================================*\
      Player playback mode has changed
\*=========================================================================*/
void hmiNewPlaybackMode( PlayerPlaybackMode mode )
{
  DfbtWidget   *screen = dfbtGetScreen();
  int           width, height, border;

  DBGMSG( "hmiNewPlaybackMode: %d.", mode );

/*------------------------------------------------------------------------*\
    Get geometry
\*------------------------------------------------------------------------*/
  dfbtGetSize( screen, &width, &height );
  border = height/100;
  dfbtGetSize( wStatus, &width, &height );
  height -= 2*border;
  height /= 2;

/*------------------------------------------------------------------------*\
    Clear icons
\*------------------------------------------------------------------------*/
  if( wRepeatIcon ) {
    dfbtContainerRemove( wStatus, wRepeatIcon );
    dfbtRelease( wRepeatIcon );
    wRepeatIcon = NULL;
  }
  if( wShuffleIcon ) {
    dfbtContainerRemove( wStatus, wShuffleIcon );
    dfbtRelease( wShuffleIcon );
    wShuffleIcon = NULL;
  }

/*------------------------------------------------------------------------*\
    Get and draw new icons
\*------------------------------------------------------------------------*/
  switch( mode ) {
    case PlaybackQueue:
      break;

    case PlaybackShuffle:
      wShuffleIcon = dfbtImage( height, height, "ickShuffle.png", true );
      break;

    case PlaybackRepeatItem:
      wRepeatIcon = dfbtImage( height, height, "ickRepeatItem.png", true );
      break;

    case PlaybackRepeatQueue:
      wRepeatIcon = dfbtImage( height, height, "ickRepeatQueue.png", true );
      break;

    case PlaybackRepeatShuffle:
      wRepeatIcon  = dfbtImage( height, height, "ickRepeatQueue.png", true );
      wShuffleIcon = dfbtImage( height, height, "ickShuffle.png", true );
      break;

    case PlaybackDynamic:
      wRepeatIcon  = dfbtImage( height, height, "ickRepeatItem.png", true );
      wShuffleIcon = dfbtImage( height, height, "ickShuffle.png", true );
      break;
  }

  if( wRepeatIcon )
    dfbtContainerAdd( wStatus, wRepeatIcon, width-border, border, DfbtAlignTopRight );
  if( wShuffleIcon )
    dfbtContainerAdd( wStatus, wShuffleIcon, width-2*border-height, border, DfbtAlignTopRight );

/*------------------------------------------------------------------------*\
    Trigger redraw
\*------------------------------------------------------------------------*/
  dfbtRedrawScreen( false );
}


/*=========================================================================*\
      Volume and muting setting has changed
\*=========================================================================*/
void hmiNewVolume( double volume, bool muted )
{
  DfbtWidget   *screen = dfbtGetScreen();
  int           width, height, border, size, y, a;
  char          buffer[64];
  DBGMSG( "hmiNewVolume: %.2lf (muted: %s).", volume, muted?"On":"Off" );


/*------------------------------------------------------------------------*\
    Get geometry
\*------------------------------------------------------------------------*/
  dfbtGetSize( screen, &width, &height );
  border = height/100;
  dfbtGetSize( wStatus, &width, &height );
  size = (height-2*border)/2;
  font1->GetHeight( font1, &a );

/*------------------------------------------------------------------------*\
    Clear icon and string
\*------------------------------------------------------------------------*/
  if( wVolumeIcon ) {
    dfbtContainerRemove( wStatus, wVolumeIcon );
    dfbtRelease( wVolumeIcon );
    wVolumeIcon = NULL;
  }
  if( wVolumeString ) {
    dfbtContainerRemove( wStatus, wVolumeString );
    dfbtRelease( wVolumeString );
    wVolumeString = NULL;
  }

/*------------------------------------------------------------------------*\
    Show volume icon
\*------------------------------------------------------------------------*/
  if( muted )
    wVolumeIcon = dfbtImage( size, size, "ickVolumeMuted.png", true );
  else if( volume<1.0/3 )
    wVolumeIcon = dfbtImage( size, size, "ickVolume1.png", true );
  else if( volume<2.0/3 )
    wVolumeIcon = dfbtImage( size, size, "ickVolume2.png", true );
  else
    wVolumeIcon = dfbtImage( size, size, "ickVolume3.png", true );
  dfbtContainerAdd( wStatus, wVolumeIcon, width-3*border-2*size, height-border, DfbtAlignBottomRight );

/*------------------------------------------------------------------------*\
    Show volume string
\*------------------------------------------------------------------------*/
  sprintf( buffer, "%3d%%", (int)(volume*100+.5) );
  wVolumeString = dfbtText( buffer, font1, &cWhite );
  y = height-border-size/2;
  dfbtContainerAdd( wStatus, wVolumeString, width-4*border-3*size, y, DfbtAlignCenterRight );

/*------------------------------------------------------------------------*\
    Trigger redraw
\*------------------------------------------------------------------------*/
  dfbtRedrawScreen( false );
}


/*=========================================================================*\
      Audio backend format has changed
\*=========================================================================*/
void hmiNewFormat( AudioFormat *format )
{
  DfbtWidget   *screen = dfbtGetScreen();
  int           width, height, border, size, y;
  char          buffer[64];

  DBGMSG( "hmiNewFormat: %s.", audioFormatStr(NULL,format) );

/*------------------------------------------------------------------------*\
    Get geometry
\*------------------------------------------------------------------------*/
  dfbtGetSize( screen, &width, &height );
  border = height/100;
  dfbtGetSize( wStatus, &width, &height );
  size = (height-2*border)/2;

/*------------------------------------------------------------------------*\
    Clear strings
\*------------------------------------------------------------------------*/
  if( wFormatString1 ) {
    dfbtContainerRemove( wStatus, wFormatString1 );
    dfbtRelease( wFormatString1 );
    wFormatString1 = NULL;
  }
  if( wFormatString2 ) {
    dfbtContainerRemove( wStatus, wFormatString2 );
    dfbtRelease( wFormatString2 );
    wFormatString2 = NULL;
  }

/*------------------------------------------------------------------------*\
    Show new strings
\*------------------------------------------------------------------------*/
  if( audioFormatIsComplete(format) ) {
    sprintf( buffer, "%d Hz", format->sampleRate );
    wFormatString1 = dfbtText( buffer, font1, &cWhite );
    y = height-border-size/2;
    dfbtContainerAdd( wStatus, wFormatString1, width-border, y, DfbtAlignBottomRight );
    sprintf( buffer, "%dx%d bit", format->channels, format->bitWidth );
    wFormatString2 = dfbtText( buffer, font1, &cWhite );
    dfbtContainerAdd( wStatus, wFormatString2, width-border, y, DfbtAlignTopRight );
  }

/*------------------------------------------------------------------------*\
    Trigger redraw
\*------------------------------------------------------------------------*/
  dfbtRedrawScreen( false );
}


/*=========================================================================*\
      New seek Position
\*=========================================================================*/
void hmiNewPosition( double seekPos )
{
  DfbtWidget   *screen = dfbtGetScreen();
  int           width, height, border;
  char          buffer[64];
  int           h, m, s;
  int           d = 0;
  char          percentStr[20];

  DBGMSG( "hmiNewPosition: %.2lf/%.2lf", seekPos, d );

/*------------------------------------------------------------------------*\
    Get geometry
\*------------------------------------------------------------------------*/
  dfbtGetSize( screen, &width, &height );
  border = height/100;
  dfbtGetSize( wStatus, &width, &height );

/*------------------------------------------------------------------------*\
    Clear string
\*------------------------------------------------------------------------*/
  if( wPositionString ) {
    dfbtContainerRemove( wStatus, wPositionString );
    dfbtRelease( wPositionString );
    wPositionString = NULL;
  }

/*------------------------------------------------------------------------*\
    Do we have a duration?
\*------------------------------------------------------------------------*/
  *percentStr = '\0';
  if( currentItem )
    d = playlistItemGetDuration( currentItem );
  if( seekPos>=0 && d>0 ) {
    int x, y;

    // Calculate cursor position
    dfbtGetOffset( wStatus, &x, &y );
    x = seekPos*(width-2*border)/d + .5 + 2*border;

    // Construct string component (total length)
    h = (int)d/3600;
    d -= h*3600;
    m = (int)d/60;
    d -= m*60;
    s = (int)d;
    if( h )
      sprintf( percentStr, " / %d:%02d:%02d", h, m, s );
    else
      sprintf( percentStr, " / %d:%02d", m, s );

    // Load or reposition cursor
    if( wPositionIcon )
      dfbtContainerSetPosition( screen, wPositionIcon, x, y, DfbtAlignCenter );
    else {
      wPositionIcon = dfbtImage( 2*border, 2*border, "ickPositionCursor.png", true );
      dfbtContainerAdd( screen, wPositionIcon, x, y, DfbtAlignCenter );
    }
  }

/*------------------------------------------------------------------------*\
    No duration!
\*------------------------------------------------------------------------*/
  else {
    *percentStr = '\0';
    dfbtContainerRemove( screen, wPositionIcon );
    dfbtRelease( wPositionIcon );
    wPositionIcon = NULL;
  }

/*------------------------------------------------------------------------*\
    Build human readable time string
\*------------------------------------------------------------------------*/
  h = (int)seekPos/3600;
  seekPos -= h*3600;
  m = (int)seekPos/60;
  seekPos -= m*60;
  s = (int)seekPos;
  if( h )
    sprintf( buffer, "%d:%02d:%02d%s", h, m, s, percentStr );
  else
    sprintf( buffer, "%d:%02d%s", m, s, percentStr );

/*------------------------------------------------------------------------*\
    Show string
\*------------------------------------------------------------------------*/
  wPositionString = dfbtText( buffer, font2, &cWhite );
  dfbtContainerAdd( wStatus, wPositionString, height+border, height/2, DfbtAlignCenterLeft );

/*------------------------------------------------------------------------*\
    Trigger redraw
\*------------------------------------------------------------------------*/
  dfbtRedrawScreen( false );
}



/*=========================================================================*\
      Get a direct fb toolkit container for a playlist item
\*=========================================================================*/
static DfbtWidget *wPlaylistItem( PlaylistItem *item, int pos, int width, int height, bool isCursor )
{
  DfbtWidget            *container;
  DfbtWidget            *wNum, *wTxt;
  DfbtWidget            *wImage;
  json_t                *jObj;
  const char            *txt;
  char                   buffer[64];
  int                    border = width/100;
  int                    txt_y  = 0;
  int                    txt_x  = height + border;
  int                    a, i;
  double                 d;

  DBGMSG( "wPlaylistItem: %p (%d - %s) %s", item, pos,
          item?playlistItemGetText(item):"<None>", isCursor?"<<<":"" );

/*------------------------------------------------------------------------*\
    Create new container
\*------------------------------------------------------------------------*/
  container = dfbtContainer( width, height );
  if( !container )
    return NULL;
  dfbtSetName( container, "PlaylistItem" );

/*------------------------------------------------------------------------*\
    Set background
\*------------------------------------------------------------------------*/
  if( isCursor )
    dfbtSetBackground( container, &cRed );
  else if( item )
    dfbtSetBackground( container, &cBlue );
  else
    dfbtSetBackground( container, &cGray );

/*------------------------------------------------------------------------*\
    No item data?
\*------------------------------------------------------------------------*/
  if( !item )
    return container;

/*------------------------------------------------------------------------*\
    Get and show Queue Position and title in one line
\*------------------------------------------------------------------------*/
  sprintf( buffer, "%3d.", pos+1 );
  wNum = dfbtText( buffer, font1, &cWhite );

  jObj = playlistItemGetModelAttribute( item, "name" );
  if( jObj && json_is_string(jObj) )
    txt = json_string_value( jObj );
  else
    txt = playlistItemGetText( item );
  wTxt = dfbtText( txt, font2, &cWhite );

  font1->GetMaxAdvance( font1, &a );
  txt_x += 3*a;
  font2->GetHeight( font2, &a );
  txt_y += a;

  dfbtContainerAdd( container, wNum, txt_x, txt_y, DfbtAlignBaseRight );
  dfbtRelease( wNum );

  txt_x += border;
  dfbtContainerAdd( container, wTxt, txt_x, txt_y, DfbtAlignBaseLeft );
  dfbtRelease( wTxt );


/*------------------------------------------------------------------------*\
    Show album name and duration in one line
\*------------------------------------------------------------------------*/
  font1->GetHeight( font1, &a );
  txt_y += a;
  jObj = playlistItemGetModelAttribute( item, "album" );
  if( jObj ) {
    jObj = json_object_get( jObj, "name" );
    if( jObj && json_is_string(jObj) ) {
      txt = json_string_value( jObj );
      wTxt = dfbtText( txt, font1, &cWhite );
      dfbtContainerAdd( container, wTxt, txt_x, txt_y, DfbtAlignBaseLeft );
      dfbtRelease( wTxt );
    }
  }
  d = playlistItemGetDuration( item );
  if( d ) {
    int h, m, s;
    h = (int)d/3600;
    d -= h*3600;
    m = (int)d/60;
    d -= m*60;
    s = (int)d;
    if( h )
      sprintf( buffer, "(%d:%02d:%02d)", h, m, s );
    else
      sprintf( buffer, "(%d:%02d)", m, s );
    font1->GetMaxAdvance( font1, &a );
    wTxt = dfbtText( buffer, font1, &cWhite );
    dfbtContainerAdd( container, wTxt, txt_x-border, txt_y, DfbtAlignBaseRight );
    dfbtRelease( wTxt );
  }

/*------------------------------------------------------------------------*\
    Show Artists
\*------------------------------------------------------------------------*/
  jObj = playlistItemGetModelAttribute( item, "mainArtists" );
  if( jObj && json_is_array(jObj) ) {
    char *str = NULL;
    for( i=0; i<json_array_size(jObj); i++ ) {
      json_t *jItem = json_array_get( jObj, i );
      jItem = json_object_get( jItem, "name" );
      if( jItem && json_is_string(jItem) ) {
        txt  = json_string_value( jItem );
        if( !str ) {
          str = malloc( strlen(txt)+1 );
          strcpy( str, txt );
        }
        else {
          str = realloc( str, strlen(txt)+strlen(str)+3 );
          strcat( str, ", " );
          strcat( str, txt );
        }
      }
    }
    if( str ) {
      wTxt = dfbtText( str, font1, &cWhite );
      font1->GetHeight( font1, &a );
      txt_y += a;
      dfbtContainerAdd( container, wTxt, txt_x, txt_y, DfbtAlignBaseLeft );
      dfbtRelease( wTxt );
      Sfree( str );
    }
  }

/*------------------------------------------------------------------------*\
    Show Artwork
\*------------------------------------------------------------------------*/
  wImage = NULL;
  jObj = playlistItemGetAttribute( item, "image" );
  if( jObj && json_is_string(jObj) ) {
    char *uri = ickServiceResolveURI( json_string_value(jObj), "content" );
    if( uri )
      wImage = dfbtImage( height-2*border, height-2*border, uri, false );
  }
 if( !wImage )
    wImage = dfbtImage( height-2*border, height-2*border, "icklogo.png", true );
  dfbtContainerAdd( container, wImage, border, border, DfbtAlignTopLeft );
  dfbtRelease( wImage );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return container;
}

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/




