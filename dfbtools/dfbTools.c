/*$*********************************************************************\

Name            : -

Source File     : dfbTools.c

Description     : Simple widget toolkit for direct frame buffer HMI

Comments        : only needed for "hmi=DirectFB" configurations

Called by       : hmiDirectFB.c

Calls           : 

Error Messages  : -
  
Date            : 01.06.2013

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

// #undef ICK_DEBUG

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#include <directfb.h>
#include "dfbTools.h"
#include "dfbToolsInternal.h"
#include "ickutils.h"


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
static char              *resPath;
static IDirectFB         *dfb;
static IDirectFBSurface  *primary;
static DfbtWidget        *screen;
static pthread_mutex_t    screenMutex = PTHREAD_MUTEX_INITIALIZER;


/*=========================================================================*\
    Private prototypes
\*=========================================================================*/
static bool _checkUpdates( DfbtWidget *widget );
static int _redraw( DfbtWidget *widget, IDirectFBSurface *surf );


/*=========================================================================*\
    Init toolkit
\*=========================================================================*/
int dfbtInit( const char *resourcePath )
{
  DFBResult             drc;
  DFBSurfaceDescription sdsc;
  int                   width, height;

  DBGMSG( "dfbtInit: \"%s\"", resourcePath );

/*------------------------------------------------------------------------*\
    Save resource path
\*------------------------------------------------------------------------*/
  resPath = strdup( resourcePath );

/*------------------------------------------------------------------------*\
    Get super interface
\*------------------------------------------------------------------------*/
  drc = DirectFBCreate( &dfb );
  if( drc!=DFB_OK ) {
    logerr( "dfbtInit: could not create direct fb super interface (%s).",
            DirectFBErrorString(drc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Get primary surface
\*------------------------------------------------------------------------*/
  sdsc.flags       = DSDESC_CAPS | DSDESC_PIXELFORMAT;
  sdsc.caps        = DSCAPS_PRIMARY | DSCAPS_FLIPPING;
  sdsc.pixelformat = DSPF_ARGB;
  drc = dfb->CreateSurface( dfb, &sdsc, &primary ) ;
  if( drc!=DFB_OK ) {
    logerr( "dfbtInit: could not create surface (%s).", DirectFBErrorString(drc) );
    dfb->Release( dfb );
    return -1;
  }
  drc = primary->GetSize( primary, &width, &height ) ;
  if( drc!=DFB_OK ) {
    logerr( "dfbtInit: could not get screen size (%s).",
             DirectFBErrorString(drc) );
    DFBRELEASE( primary );
    DFBRELEASE( dfb );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Create screen widget
\*------------------------------------------------------------------------*/
  screen = _dfbtNewWidget( DfbtScreen, width, height );
  if( !screen ) {
    DFBRELEASE( primary );
    DFBRELEASE( dfb );
    return -1;
  }
  screen->surface = primary;

/*------------------------------------------------------------------------*\
    First draw
\*------------------------------------------------------------------------*/
  return _redraw( screen, NULL );
}


/*=========================================================================*\
    Shutdown toolkit
\*=========================================================================*/
void dfbtShutdown( void )
{
  DBGMSG( "dfbtShutdown: " );

/*------------------------------------------------------------------------*\
    Release all screen content
\*------------------------------------------------------------------------*/
  dfbtContainerRemove( screen, NULL );
  dfbtRelease( screen );
  screen = NULL;

/*------------------------------------------------------------------------*\
  Free dfb main interface
\*------------------------------------------------------------------------*/
  DFBRELEASE( primary );
  DFBRELEASE( dfb );

/*------------------------------------------------------------------------*\
  Free resource path
\*------------------------------------------------------------------------*/
  Sfree( resPath );
}


/*=========================================================================*\
    Get direct frame buffer super interface
\*=========================================================================*/
IDirectFB *dfbtGetDdb( void )
{
  return dfb;
}


/*=========================================================================*\
    Get root widget
\*=========================================================================*/
DfbtWidget *dfbtGetScreen( void )
{
  return screen;
}


/*=========================================================================*\
    Get size of widget
\*=========================================================================*/
void dfbtGetSize( DfbtWidget *widget, int *width, int *height )
{
  if( width )
    *width = widget->size.w;

  if( height )
    *height = widget->size.h;
}


/*=========================================================================*\
    Get offest of widget
\*=========================================================================*/
void dfbtGetOffset( DfbtWidget *widget, int *x, int *y )
{
  if( x )
    *x = widget->offset.x;

  if( y )
    *y = widget->offset.y;
}


/*=========================================================================*\
    Increment release counter
\*=========================================================================*/
void dfbtRetain( DfbtWidget *widget )
{
  DBGMSG( "dfbtRetain (%p): refcount now %d", widget, widget->refCount+1 );

  widget->refCount++;
}


/*=========================================================================*\
    Decrement release counter
\*=========================================================================*/
void dfbtRelease( DfbtWidget *widget )
{
  DBGMSG( "dfbtRelease (%p): refcount now %d", widget, widget->refCount-1 );

/*------------------------------------------------------------------------*\
    Decrement counter and get rid of widget if necessary
\*------------------------------------------------------------------------*/
  if( !--widget->refCount ) {

    // First remove any depending objects (for screens and containers)
    if( widget->content )
      dfbtContainerRemove( widget, NULL );

    // Now call the actual object destructor
    _dfbtWidgetDestruct( widget );
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
    Set update flag for a widget.
      For containers all content will be flagged
\*=========================================================================*/
void dfbtSetNeedsUpdate( DfbtWidget *widget )
{
  DfbtWidget  *walk;

  DBGMSG( "dfbtSetNeedsUpdate (%p): ", widget );

/*------------------------------------------------------------------------*\
    Set flag
\*------------------------------------------------------------------------*/
  widget->needsUpdate = true;

/*------------------------------------------------------------------------*\
    Propagate to descendants
\*------------------------------------------------------------------------*/
  for( walk=widget->content; walk; walk=walk->next )
    dfbtSetNeedsUpdate( walk );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
    Set background color
\*=========================================================================*/
void dfbtSetBackground( DfbtWidget *widget, DFBColor *color )
{
  DBGMSG( "dfbtSetBackground (%p): %02x,%02x,%02x,%02x", widget,
          color->r, color->g, color->b, color->a );

  memcpy( &widget->background, color, sizeof(DFBColor) );
}



/*=========================================================================*\
    Do a screen redraw.
      Optionally force the update of all content
\*=========================================================================*/
int dfbtRedrawScreen( bool force )
{
  DFBResult drc;

  DBGMSG( "dfbtRedrawScreen: %s", force?"forced":"not forced" );

/*------------------------------------------------------------------------*\
    Set redraw flags if forced
\*------------------------------------------------------------------------*/
  if( force )
    dfbtSetNeedsUpdate( screen );

/*------------------------------------------------------------------------*\
    else mark all elements that need to be redrawn
\*------------------------------------------------------------------------*/
  else
    _checkUpdates( screen );

/*------------------------------------------------------------------------*\
    Now redraw all marked elements
\*------------------------------------------------------------------------*/
  if( _redraw(screen,NULL) )
    return -1;

/*------------------------------------------------------------------------*\
    Flip screen, that's all
\*------------------------------------------------------------------------*/
  drc = screen->surface->Flip( screen->surface, NULL, 0 );
  if( drc!=DFB_OK ) {
    logerr( "dfbtRedrawScreen: could not flip screen (%s).",
             DirectFBErrorString(drc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}



/*=========================================================================*\
    Get or Create an image instance
\*=========================================================================*/
DfbtWidget *_dfbtNewWidget( DfbtWidgetType type, int w, int h )
{
  DFBResult              drc;
  DFBSurfaceDescription  sdsc;
  IDirectFBSurface      *surf;
  DfbtWidget            *widget;
  pthread_mutexattr_t    attr;

  DBGMSG( "_newWidget: type %d, size %dx%d", type, w, h );

/*------------------------------------------------------------------------*\
    Use primary surface for a screen
\*------------------------------------------------------------------------*/
  if( type==DfbtScreen ) {
    surf = primary;
  }

/*------------------------------------------------------------------------*\
    else create new surface
\*------------------------------------------------------------------------*/
  {
    //sdsc.flags = DSDESC_CAPS | DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT;
    //sdsc.caps        = DSCAPS_PREMULTIPLIED;
    sdsc.flags = DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT;
    primary->GetPixelFormat( primary, &sdsc.pixelformat );
    sdsc.width       = w;
    sdsc.height      = h;

    drc= dfb->CreateSurface( dfb, &sdsc, &surf );
    if( drc!=DFB_OK ) {
      logerr( "_newWidget: could not get surface (%s).", DirectFBErrorString(drc) );
      return NULL;
    }
  }

/*------------------------------------------------------------------------*\
    We want to use alpha blending
\*------------------------------------------------------------------------*/
  surf->SetBlittingFlags( surf, DSBLIT_BLEND_ALPHACHANNEL);

/*------------------------------------------------------------------------*\
    Create object
\*------------------------------------------------------------------------*/
  widget = calloc( 1, sizeof(DfbtWidget) );
  if( !widget ) {
    logerr( "_newWidget: out of memory!" );
    return NULL;
  }
  widget->type        = type;
  widget->size.w      = w;
  widget->size.h      = h;
  widget->surface     = surf;
  widget->needsUpdate = true;
  widget->refCount    = 1;

/*------------------------------------------------------------------------*\
    Init mutex
\*------------------------------------------------------------------------*/
  pthread_mutexattr_init( &attr );
  pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_ERRORCHECK );
  pthread_mutex_init( &widget->mutex, &attr );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return widget;
}


/*=========================================================================*\
    Delete an widget
\*=========================================================================*/
int _dfbtWidgetDestruct( DfbtWidget *widget )
{
  DBGMSG( "_dfbtWidgetDelete (%p): type %d", widget, widget->type );

/*------------------------------------------------------------------------*\
    Call type dependent destructors
\*------------------------------------------------------------------------*/
  switch( widget->type ) {

    // Container: check if impty
    case DfbtScreen:
    case DfbtContainer:
      if( widget->content )
        logerr( "_dfbtWidgetDelete: screen or container not empty" );
      break;

    case DfbtText:
      _dfbtTextDestruct( widget );
      break;

    case DfbtImage:
      // fixme
      break;

    default:
      logerr( "_dfbtWidgetDelete: unknown widget type %d", widget->type );
      return -1;
  }

/*------------------------------------------------------------------------*\
    Release surface
\*------------------------------------------------------------------------*/
  if( widget->type!=DfbtScreen )
    DFBRELEASE( widget->surface );

/*------------------------------------------------------------------------*\
    Delete mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &widget->mutex );

/*------------------------------------------------------------------------*\
    Release object
\*------------------------------------------------------------------------*/
  Sfree( widget );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Propagate update flags in tree hierarchy
\*=========================================================================*/
static bool _checkUpdates( DfbtWidget *widget )
{
  DfbtWidget  *walk;

  DBGMSG( "_checkUpdates (%p): ", widget );

/*------------------------------------------------------------------------*\
    Flag set?
\*------------------------------------------------------------------------*/
  if( widget->needsUpdate )
    return true;

/*------------------------------------------------------------------------*\
    Check descendents
\*------------------------------------------------------------------------*/
  for( walk=widget->content; walk; walk=walk->next ) {
    if( _checkUpdates(walk) )
      return true;
  }

/*------------------------------------------------------------------------*\
    No update necessary for this branch
\*------------------------------------------------------------------------*/
  return false;
}


/*=========================================================================*\
    (Re)draw a widget and all it's content
\*=========================================================================*/
static int _redraw( DfbtWidget *widget, IDirectFBSurface *surf )
{
  DFBResult    drc;
  DfbtWidget  *walk;

  DBGMSG( "_redraw (%p): type %d, (%d,%d) - %dx%d",
          widget, widget->type,
          widget->offset.x, widget->offset.y,
          widget->size.w, widget->size.h );

/*------------------------------------------------------------------------*\
    Do we need to regenerate this item?
\*------------------------------------------------------------------------*/
  if( widget->needsUpdate ) {

/*------------------------------------------------------------------------*\
    Prepare background
\*------------------------------------------------------------------------*/
    widget->surface->SetColor( widget->surface,
                               widget->background.r, widget->background.g,
                               widget->background.b, widget->background.a );
    widget->surface->FillRectangle( widget->surface, 0, 0, widget->size.w, widget->size.h );

/*------------------------------------------------------------------------*\
    switch on type
\*------------------------------------------------------------------------*/
    switch( widget->type ) {

      // Container: rebuild from content
      case DfbtScreen:
      case DfbtContainer:
        for( walk=widget->content; walk; walk=walk->next ) {
          if( _redraw(walk,widget->surface) )
            return -1;
        }
        break;

      case DfbtText:
        _dfbtTextDraw( widget );
        break;

      case DfbtImage:
        // fixme
        break;

      default:
        logerr( "_redraw: unknown widget type %d", widget->type );
        return -1;
    }
  }

/*------------------------------------------------------------------------*\
    Draw this widget in parent (surf is NULL for screen as there is no parent)
\*------------------------------------------------------------------------*/
  if( surf ) {
    drc = surf->Blit( surf, widget->surface, NULL, widget->offset.x, widget->offset.y );
    if( drc!=DFB_OK ) {
      logerr( "_redraw: could not blit widget to parent (%s).",
               DirectFBErrorString(drc) );
      return -1;
    }
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/




