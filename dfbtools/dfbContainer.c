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
  IDirectFBFont *font;
  int            asc;
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
    Adjust offset according to lignment
\*------------------------------------------------------------------------*/
  switch( align ) {
    case DfbtAlignTopLeft:
    case DfbtAlignTopCenter:
    case DfbtAlignTopRight:
      break;

    case DfbtAlignCenterLeft:
    case DfbtAlignCenterCenter:
    case DfbtAlignCenterRight:
      y -= new->size.h/2;
      break;

    case DfbtAlignBottomLeft:
    case DfbtAlignBottomCenter:
    case DfbtAlignBottomRight:
      y -= new->size.h;
      break;

    case DfbtAlignBaseLeft:
    case DfbtAlignBaseCenter:
    case DfbtAlignBaseRight:
      font = dfbtTextGetFont( new );
      if( !font ) {
        logerr( "dfbtContainerAdd: text alignment for wrong widget type %d",
                container->type );
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
      x -= new->size.w/2;
      break;

    case DfbtAlignTopRight:
    case DfbtAlignCenterRight:
    case DfbtAlignBaseRight:
    case DfbtAlignBottomRight:
      x -= new->size.w;
      break;

  }

/*------------------------------------------------------------------------*\
    Increment retain counter for target
\*------------------------------------------------------------------------*/
  dfbtRetain( new );

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

/*------------------------------------------------------------------------*\
    Set offset of element
\*------------------------------------------------------------------------*/
  new->offset.x = x;
  new->offset.y = y;
  pthread_mutex_unlock( &container->mutex );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
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
    logerr( "dfbtContainerAdd: called for wrong widget type %d",
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
      for( walk=container->content; walk->next; walk=walk->next ) {
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
    dfbtRelease( widget );
  }

/*------------------------------------------------------------------------*\
    Remove all
\*------------------------------------------------------------------------*/
  else {
    for( walk=container->content; walk; walk=walk->next )
      dfbtRelease( walk );
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
                                    END OF FILE
\*=========================================================================*/




