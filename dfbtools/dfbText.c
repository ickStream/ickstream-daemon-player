/*$*********************************************************************\

Name            : -

Source File     : dfbText.c

Description     : text widget of dfb toolkit

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
#include <string.h>
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
typedef struct {
  char          *text;
  IDirectFBFont *font;
  DFBColor       color;
} DfbtTextData;


/*=========================================================================*\
    Private symbols
\*=========================================================================*/
// none


/*=========================================================================*\
    Private prototypes
\*=========================================================================*/
// none


/*=========================================================================*\
    Create a text widget
\*=========================================================================*/
DfbtWidget *dfbtText( const char *text, IDirectFBFont *font, DFBColor *color )
{
  DFBResult     drc;
  int           width, height;
  DfbtWidget   *widget;
  DfbtTextData *textData;

  DBGMSG( "dfbtText: \"%s\"", text );

/*------------------------------------------------------------------------*\
    Get String dimensions
\*------------------------------------------------------------------------*/
  drc = font->GetHeight( font, &height );
  if( drc!=DFB_OK ) {
    logerr( "dfbtText: could not get font height (%s).",
            DirectFBErrorString(drc) );
    return NULL;
  }
  drc = font->GetStringWidth( font, text, -1, &width );
  if( drc!=DFB_OK ) {
    logerr( "dfbtText: could not get text width (%s).",
            DirectFBErrorString(drc) );
    return NULL;
  }
  DBGMSG( "dfbtText: size is %dx%d ", width, height );

/*------------------------------------------------------------------------*\
    Create widget
\*------------------------------------------------------------------------*/
  widget = _dfbtNewWidget( DfbtText, width, height );
  if( !widget )
    return NULL;

/*------------------------------------------------------------------------*\
    Allocate and init additional data
\*------------------------------------------------------------------------*/
  textData = calloc( 1, sizeof(DfbtTextData) );
  if( !textData ) {
    logerr( "dfbtText: out of memory!" );
    _dfbtWidgetDestruct( widget );
    return NULL;
  }
  textData->text  = strdup( text );
  textData->font  = font;
  memcpy( &textData->color, color, sizeof(DFBColor) );

  widget->data    = textData;

/*------------------------------------------------------------------------*\
    Retain font info
\*------------------------------------------------------------------------*/
  font->AddRef( font );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return widget;
}


/*=========================================================================*\
    Get font used for text widget
\*=========================================================================*/
IDirectFBFont *dfbtTextGetFont( DfbtWidget *widget )
{

/*------------------------------------------------------------------------*\
    Check type
\*------------------------------------------------------------------------*/
  if( widget->type!=DfbtText )
    return NULL;

/*------------------------------------------------------------------------*\
    Return data
\*------------------------------------------------------------------------*/
  DfbtTextData *textData = widget->data;
  return textData->font;
}


/*=========================================================================*\
    Destruct type specific part of a text widget
\*=========================================================================*/
void _dfbtTextDestruct( DfbtWidget *widget )
{
  DfbtTextData *textData = widget->data;

  DBGMSG( "_dfbtTextDestruct (%p): \"%s\"", widget, textData->text );

/*------------------------------------------------------------------------*\
    Free widget specific elements and release font
\*------------------------------------------------------------------------*/
  DFBRELEASE( textData->font );
  Sfree( textData->text );
  Sfree( widget->data );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
    Draw text widget
\*=========================================================================*/
void _dfbtTextDraw( DfbtWidget *widget )
{
  IDirectFBSurface  *surf     = widget->surface;
  DfbtTextData      *textData = widget->data;

  DBGMSG( "_dfbtTextDraw (%p): \"%s\" color %2xd,%2xd,%2xd,%2xd",
          widget, textData->text, textData->color.r, textData->color.g,
                                  textData->color.b, textData->color.a );

/*------------------------------------------------------------------------*\
    Draw text
\*------------------------------------------------------------------------*/
  surf->SetFont( surf, textData->font );
  surf->SetColor( surf, textData->color.r, textData->color.g,
                        textData->color.b, textData->color.a );
  surf->DrawString( surf, textData->text, -1, 0, 0, DSTF_TOPLEFT );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/




