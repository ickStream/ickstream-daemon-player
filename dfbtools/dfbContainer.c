/*$*********************************************************************\

Name            : -

Source File     : dfbContainer.c

Description     : container widget of dfb toolkit

Comments        : -

Called by       : -

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
// none


/*=========================================================================*\
    Private prototypes
\*=========================================================================*/
// none


/*=========================================================================*\
    Create a container
\*=========================================================================*/
DfbtWidget *dfbtContainer( int width, int height )
{
  DfbtWidget *widget;
  DBGMSG( "dfbtContainer: \%dx%d", width, height );

/*------------------------------------------------------------------------*\
    Create widget
\*------------------------------------------------------------------------*/
  widget = _dfbtNewWidget( DfbtContainer, width, height );
  if( !widget )
    return NULL;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return widget;
}


/*=========================================================================*\
    Add a widget to a container
\*=========================================================================*/
int dfbtContainerAdd( DfbtWidget *container, DfbtWidget *new, int x, int y, DfbtAlign align )
{
  DfbtWidget    *walk;

  DBGMSG( "dfbtContainerAdd (%p): Adding widget %p at (%d,%d)",
          container, new, x, y );

/*------------------------------------------------------------------------*\
    Check type
\*------------------------------------------------------------------------*/
  if( container->type!=DfbtScreen && container->type!=DfbtContainer ) {
    logerr( "dfbtContainerAdd: called for wrong widget type %d",
            container->type );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Nothing to do?
\*------------------------------------------------------------------------*/
  if( !new ) {
    logerr( "dfbtContainerAdd: called with nil argument" );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Already linked?
\*------------------------------------------------------------------------*/
#ifdef ICKDEBUG
  if( new->parent ) {
 // if( dfbtContainerFind(dfbtGetScreen(),new) ) {
    logerr( "dfbtContainerAdd: target already linked" );
    return -1;
  }
#endif


/*------------------------------------------------------------------------*\
    Avoid loops
\*------------------------------------------------------------------------*/
#ifdef ICKDEBUG
  if( (new->type==DfbtScreen||new->type==DfbtContainer) && dfbtContainerFind(new,container) ) {
 // if( dfbtContainerFind(dfbtGetScreen(),new) ) {
    logerr( "dfbtContainerAdd: would create loop reference to self" );
    return -1;
  }
#endif

/*------------------------------------------------------------------------*\
    Increment retain counter for target
\*------------------------------------------------------------------------*/
  dfbtRetain( new );

/*------------------------------------------------------------------------*\
    Set offset of element
\*------------------------------------------------------------------------*/
  if( dfbtContainerSetPosition(container,new,x,y,align) )
    return -1;

/*------------------------------------------------------------------------*\
    Add to content list
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &container->mutex );
  if( !container->content )
    container->content = new;
  else {
    for( walk=container->content; walk->next; walk=walk->next ) {
      if( walk==new ) {
        logerr( "dfbtContainerAdd: widget is already content element" );
        pthread_mutex_unlock( &container->mutex );
        return -1;
      }
    }
    walk->next = new;
  }
  new->parent = container;
  pthread_mutex_unlock( &container->mutex );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Reposition a widget in a container
\*=========================================================================*/
int dfbtContainerSetPosition( DfbtWidget *container, DfbtWidget *widget, int x, int y, DfbtAlign align )
{
  IDirectFBFont *font;
  int            asc;

/*------------------------------------------------------------------------*\
    Adjust offset according to alignment
\*------------------------------------------------------------------------*/
  switch( align ) {
    case DfbtAlignTopLeft:
    case DfbtAlignTopCenter:
    case DfbtAlignTopRight:
      break;

    case DfbtAlignCenterLeft:
    case DfbtAlignCenterCenter:
    case DfbtAlignCenterRight:
      y -= widget->size.h/2;
      break;

    case DfbtAlignBottomLeft:
    case DfbtAlignBottomCenter:
    case DfbtAlignBottomRight:
      y -= widget->size.h;
      break;

    case DfbtAlignBaseLeft:
    case DfbtAlignBaseCenter:
    case DfbtAlignBaseRight:
      font = dfbtTextGetFont( widget );
      if( !font ) {
        logerr( "dfbtContainerAdd: text alignment for wrong widget type %d",
                widget->type );
        return -1;
      }
      asc = 0;
      font->GetAscender( font, &asc );
      y -= asc;
      break;

    default:
      logerr( "dfbtContainerAdd: unknown alignment type %d", align );
      return -1;
  }
  switch( align ) {
    case DfbtAlignTopLeft:
    case DfbtAlignCenterLeft:
    case DfbtAlignBaseLeft:
    case DfbtAlignBottomLeft:
      break;

    case DfbtAlignTopCenter:
    case DfbtAlignCenterCenter:
    case DfbtAlignBaseCenter:
    case DfbtAlignBottomCenter:
      x -= widget->size.w/2;
      break;

    case DfbtAlignTopRight:
    case DfbtAlignCenterRight:
    case DfbtAlignBaseRight:
    case DfbtAlignBottomRight:
      x -= widget->size.w;
      break;
  }

/*------------------------------------------------------------------------*\
    Set new offset of element
\*------------------------------------------------------------------------*/
  widget->offset.x = x;
  widget->offset.y = y;

/*------------------------------------------------------------------------*\
    Container needs redraw
\*------------------------------------------------------------------------*/
  dfbtSetNeedsUpdate( container );
  return 0;
}


/*=========================================================================*\
    Move a widget in a container
\*=========================================================================*/
void dfbtContainerMovePosition( DfbtWidget *container, DfbtWidget *widget, int dx, int dy )
{

/*------------------------------------------------------------------------*\
    Adjust offset of element
\*------------------------------------------------------------------------*/
  widget->offset.x += dx;
  widget->offset.y += dy;

/*------------------------------------------------------------------------*\
    Container needs redraw
\*------------------------------------------------------------------------*/
  dfbtSetNeedsUpdate( container );
}


/*=========================================================================*\
    Remove one or all widgets from a container
\*=========================================================================*/
int dfbtContainerRemove( DfbtWidget *container, DfbtWidget *widget )
{
  DfbtWidget  *walk;

  DBGMSG( "dfbtContainerRemove (%p): removing widget %p", container, widget );

/*------------------------------------------------------------------------*\
    Check type
\*------------------------------------------------------------------------*/
  if( container->type!=DfbtScreen && container->type!=DfbtContainer ) {
    logerr( "dfbtContainerRemove: called for wrong widget type %d",
            container->type );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Find and unlink target
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &container->mutex );
  if( widget ) {
    if( container->content==widget )
      container->content = container->content->next;
    else {
      for( walk=container->content; walk&&walk->next; walk=walk->next ) {
        if( walk->next==widget ) {
          walk->next = widget->next;
          break;
        }
      }
      if( !walk ) {
        logerr( "dfbtContainerRemove: widget is no content element" );
        pthread_mutex_unlock( &container->mutex );
        return -1;
      }
    }
    widget->parent = NULL;
    dfbtRelease( widget );
  }

/*------------------------------------------------------------------------*\
    Remove all
\*------------------------------------------------------------------------*/
  else {
    DfbtWidget *next = NULL;
    for( walk=container->content; walk; walk=next ) {
      next = walk->next;
      walk->parent = NULL;
      dfbtRelease( walk );
    }
    container->content = NULL;
  }

/*------------------------------------------------------------------------*\
    Need an redraw
\*------------------------------------------------------------------------*/
  container->needsUpdate = true;
  pthread_mutex_unlock( &container->mutex );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Find the container that owns a widget
      return NULL, if widget is not linked or equal to root
\*=========================================================================*/
DfbtWidget *dfbtContainerFind( DfbtWidget *root, DfbtWidget *widget )
{
  DfbtWidget *walk;
  DfbtWidget *result = NULL;

  DBGMSG( "dfbtContainerFind (%p): find widget %p", root, widget );

/*------------------------------------------------------------------------*\
    Check type
\*------------------------------------------------------------------------*/
  if( root->type!=DfbtScreen && root->type!=DfbtContainer ) {
    logerr( "dfbtContainerFind: called for wrong widget type %d",
            root->type );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    NIll is contained nowhere
\*------------------------------------------------------------------------*/
  if( !widget ) {
    logerr( "dfbtContainerFind: called for nil target" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Check all members
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &root->mutex );
  for( walk=root->content; walk&&!result; walk=walk->next ) {

    // Check member
    if( walk==widget )
      result = root;

    // Do recursion if member is a container
    else if( walk->type==DfbtScreen  || walk->type==DfbtContainer )
      result = dfbtContainerFind( walk, widget );

  }
  pthread_mutex_unlock( &root->mutex );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  if( result )
    DBGMSG( "dfbtContainerFind (%p): widget %p found in %p", root, widget, result );
  return result;
}




/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/




