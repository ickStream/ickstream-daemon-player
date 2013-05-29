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
#define DFB_ITEMS 7
#define DFBRELEASE(a) ((a)->Release(a),(a)=NULL)

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static IDirectFB         *dfb;
static IDirectFBSurface  *dfb_primary;
static int                dfb_width;
static int                dfb_height;
//static const char        *fontfile = ICK_DFBFONT;
static const char        *fontfile = "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf";
static IDirectFBFont     *font1;
static int                font1_height;
static IDirectFBFont     *font2;
static int                font2_height;

static IDirectFBSurface  *dfb_items[DFB_ITEMS];


static PlaylistItem *currentItem;


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static IDirectFBSurface *dfb_item( PlaylistItem *item, int pos, int width, int height, bool isCursor );


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
  DFBResult             drc;
  DFBSurfaceDescription sdsc;
  DFBFontDescription    fdsc;
  DBGMSG( "Initializing HMI module..." );

/*------------------------------------------------------------------------*\
    Get main interface
\*------------------------------------------------------------------------*/
  DirectFBSetOption( "no-cursor", NULL );
  DirectFBSetOption ("bg-none", NULL);
  DirectFBSetOption ("no-init-layer", NULL);

  drc = DirectFBCreate( &dfb );
  if( drc!=DFB_OK ) {
    logerr( "hmiInit: could not create direct fb (%s).", DirectFBErrorString(drc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Get and clear main surface
\*------------------------------------------------------------------------*/
  sdsc.flags = DSDESC_CAPS;
  sdsc.caps  = DSCAPS_PRIMARY | DSCAPS_FLIPPING;
  drc = dfb->CreateSurface( dfb, &sdsc, &dfb_primary ) ;
  if( drc!=DFB_OK ) {
    logerr( "hmiInit: could not create surface (%s).", DirectFBErrorString(drc) );
    dfb->Release( dfb );
    return -1;
  }
  drc = dfb_primary->GetSize( dfb_primary, &dfb_width, &dfb_height ) ;
  if( drc!=DFB_OK ) {
    logerr( "hmiInit: could not get size (%s).", DirectFBErrorString(drc) );
    DFBRELEASE( dfb_primary );
    DFBRELEASE( dfb );
    return -1;
  }
  drc = dfb_primary->FillRectangle( dfb_primary, 0, 0, dfb_width, dfb_height ) ;
  if( drc!=DFB_OK ) {
    logerr( "hmiInit: could not fill screen (%s).", DirectFBErrorString(drc) );
    DFBRELEASE( dfb_primary );
    DFBRELEASE( dfb );
    return -1;
  }
  dfb_primary->Flip( dfb_primary, NULL, 0 );

/*------------------------------------------------------------------------*\
    Get fonts
\*------------------------------------------------------------------------*/
  fdsc.flags      = DFDESC_HEIGHT | DFDESC_ATTRIBUTES;
  fdsc.height     = 1 + 24*dfb_height/1024;
  fdsc.attributes = DFFA_NONE;
  drc = dfb->CreateFont( dfb, fontfile, &fdsc, &font1 );
  if( drc!=DFB_OK ) {
    logerr( "hmiInit: could not find font %s (%s).", fontfile, DirectFBErrorString(drc) );
    DFBRELEASE( dfb_primary );
    DFBRELEASE( dfb );
    return -1;
  }
  font1->GetHeight( font1, &font1_height );
  dfb_primary->SetFont( dfb_primary, font1 );

  fdsc.flags      = DFDESC_HEIGHT | DFDESC_ATTRIBUTES;
  fdsc.height     = 1 + 48*dfb_height/1024;
  fdsc.attributes = DFFA_NONE;
  drc = dfb->CreateFont( dfb, fontfile, &fdsc, &font2 );
  if( drc!=DFB_OK ) {
    logerr( "hmiInit: could not find font %s (%s).", fontfile, DirectFBErrorString(drc) );
    DFBRELEASE( font1 );
    DFBRELEASE( dfb_primary );
    DFBRELEASE( dfb );
    return -1;
  }
  font2->GetHeight( font2, &font2_height );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Close HMI module
\*=========================================================================*/
void hmiShutdown( void )
{
  int i;
  DBGMSG( "Shutting down HMI module..." );

/*------------------------------------------------------------------------*\
    Release surfaces of playback queue items
\*------------------------------------------------------------------------*/
  for( i=0; i<DFB_ITEMS; i++ ) {
    if( dfb_items[i] )
      DFBRELEASE( dfb_items[i] );
  }

/*------------------------------------------------------------------------*\
    Release fonts and primary/super interfaces
\*------------------------------------------------------------------------*/
  DFBRELEASE( font1 );
  DFBRELEASE( font2 );
  DFBRELEASE( dfb_primary );
  DFBRELEASE( dfb );
}


/*=========================================================================*\
      Player configuration (name, cloud access) changed
\*=========================================================================*/
void hmiNewConfig( void )
{
  ServiceListItem *service;

  DBGMSG( "hmiNewConfig: %s, \"%s\", \"%s\".",
      playerGetUUID(),  playerGetName(), playerGetToken()?"Cloud":"No Cloud" );

  printf( "HMI Player id        : %s\n", playerGetUUID() );
  printf( "HMI Player name      : \"%s\"\n", playerGetName() );
  printf( "HMI Cloud status     : %s\n", playerGetToken()?"Registered":"Unregistered" );

  for( service=ickServiceFind(NULL,NULL,NULL,0); service;
       service=ickServiceFind(service,NULL,NULL,0) )
    printf( "HMI Service          : \"%s\" (%s)\n", ickServiceGetName(service),
            ickServiceGetType(service)  );

}


/*=========================================================================*\
      Queue cursor is pointing to a new item
        item might be NULL, if no current track is defined...
\*=========================================================================*/
void hmiNewItem( Playlist *plst, PlaylistItem *item )
{
  int i;
  int width  = dfb_width / 2;
  int height = dfb_height / DFB_ITEMS;
  int border = dfb_height/100;

  DBGMSG( "hmiNewItem: %p (%s).", item, item?playlistItemGetText(item):"<None>" );
  currentItem = item;

/*------------------------------------------------------------------------*\
    Release surfaces of playback queue items
\*------------------------------------------------------------------------*/
  for( i=0; i<DFB_ITEMS; i++ ) {
    if( dfb_items[i] )
      DFBRELEASE( dfb_items[i] );
  }

/*------------------------------------------------------------------------*\
    Create surfaces for playback queue items
\*------------------------------------------------------------------------*/
  playlistLock( plst );
  for( i=0; i<DFB_ITEMS; i++ ) {
    PlaylistItem *theItem;
    int           pos = playlistGetCursorPos(plst)-DFB_ITEMS/2 + i;

    // Out of bounds?
    theItem = playlistGetItem( plst, pos );
    if( !theItem )
      continue;

    // Draw item
    playlistItemLock( theItem );
    dfb_items[i] = dfb_item( theItem, pos, width-border, height-border, theItem==item );
    playlistItemUnlock( theItem );
  }
  playlistUnlock( plst );

/*------------------------------------------------------------------------*\
    Show surfaces for playback queue items
\*------------------------------------------------------------------------*/
  dfb_primary->SetColor( dfb_primary, 0x0, 0x0, 0x0, 0x0 );
  dfb_primary->FillRectangle( dfb_primary, dfb_width-width, 0, width, dfb_height );
  for( i=0; i<DFB_ITEMS; i++ ) {
    int x = dfb_width-width;
    int y = i*height;
    if( !dfb_items[i] )
      continue;
    dfb_primary->Blit( dfb_primary, dfb_items[i], NULL, x, y );
  }
  dfb_primary->Flip( dfb_primary, NULL, 0 );

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

  printf( "HMI Playback state   : %s\n", stateStr );
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

  printf( "HMI Repeat mode      : %s\n", modeStr );
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
      Get a direct fb surface for an playlist item
\*=========================================================================*/
static IDirectFBSurface *dfb_item( PlaylistItem *item, int pos, int width, int height, bool isCursor )
{
  DFBResult              drc;
  DFBSurfaceDescription  sdsc;
  IDirectFBSurface      *surf;
  json_t                *jObj;
  const char            *txt;
  char                   buffer[64];
  int                    border = dfb_height/100;
  int                    txt_y  = 0;
  int                    txt_x  = height + border;
  int                    w;

  DBGMSG( "dfb_item: %p (%d - %s) %s",
          item, pos, playlistItemGetText(item), isCursor?"<<<":"" );

/*------------------------------------------------------------------------*\
    Create new surface
\*------------------------------------------------------------------------*/
  //sdsc.flags = DSDESC_CAPS | DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT;
  //sdsc.caps        = DSCAPS_PREMULTIPLIED;
  sdsc.flags = DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT;
  dfb_primary->GetPixelFormat( dfb_primary, &sdsc.pixelformat );
  sdsc.width       = width;
  sdsc.height      = height;

  drc= dfb->CreateSurface( dfb, &sdsc, &surf );
  if( drc!=DFB_OK ) {
    logerr( "dfb_item: could not get surface (%s).", DirectFBErrorString(drc) );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Clear background
\*------------------------------------------------------------------------*/
  if( isCursor )
    surf->SetColor( surf, 0x80, 0x00, 0x0, 0x80 );
  else
    surf->SetColor( surf, 0x0, 0x0, 0x80, 0x80 );
  surf->FillRectangle( surf, 0, 0, width, height );

/*------------------------------------------------------------------------*\
    Get and show Queue Position and title in one line
\*------------------------------------------------------------------------*/
  txt_y += font2_height;
  surf->SetFont( surf, font1 );
  sprintf( buffer, "%d.", pos+1 );
  font1->GetStringWidth( font1, buffer, -1, &w );
  txt_x += w;
  surf->SetColor( surf, 0x80, 0x80, 0x80, 0xFF );
  surf->DrawString( surf, buffer, -1, txt_x, txt_y, DSTF_RIGHT );
  txt_x += border;

  jObj = playlistItemGetModelAttribute( item, "name" );
  if( jObj && json_is_string(jObj) )
    txt = json_string_value( jObj );
  else
    txt = playlistItemGetText( item );
  surf->SetFont( surf, font2 );
  surf->SetColor( surf, 0x80, 0x80, 0x80, 0xFF );
  surf->DrawString( surf, txt, -1, txt_x, txt_y, DSTF_LEFT );

/*------------------------------------------------------------------------*\
    Show Album
\*------------------------------------------------------------------------*/
  jObj = playlistItemGetModelAttribute( item, "album" );
  if( jObj ) {
    jObj = json_object_get( jObj, "name" );
    if( jObj && json_is_string(jObj) ) {
      txt = json_string_value( jObj );
      surf->SetFont( surf, font1 );
      txt_y += font1_height;
      surf->SetColor( surf, 0x80, 0x80, 0x80, 0xFF );
      surf->DrawString( surf, txt, -1, txt_x, txt_y, DSTF_LEFT );
    }
  }

/*------------------------------------------------------------------------*\
    Show Artist
\*------------------------------------------------------------------------*/
  jObj = playlistItemGetModelAttribute( item, "mainArtist" );
  if( jObj ) {
    jObj = json_object_get( jObj, "name" );
    if( jObj && json_is_string(jObj) ) {
      txt = json_string_value( jObj );
      surf->SetFont( surf, font1 );
      txt_y += font1_height;
      surf->SetColor( surf, 0x80, 0x80, 0x80, 0xFF );
      surf->DrawString( surf, txt, -1, txt_x, txt_y, DSTF_LEFT );
    }
  }

/*------------------------------------------------------------------------*\
    Show Artwork
\*------------------------------------------------------------------------*/

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return surf;
}

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/




