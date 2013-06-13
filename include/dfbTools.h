/*$*********************************************************************\

Name            : -

Source File     : dfbImage.h

Description     : Main include file for dfbImage.c

Comments        : -

Date            : 30.05.2013

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


#ifndef __DFBTTOOLS_H
#define __DFBTTOOLS_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <stdbool.h>
#include <directfb.h>


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/

#define DFBRELEASE(a) ((a)->Release(a),(a)=NULL)

// A toolkit widget
struct _dfbtwidget;
typedef struct _dfbtwidget DfbtWidget;

// Types of widgets
typedef enum {
  DfbtScreen = 1,
  DfbtContainer,
  DfbtImage,
  DfbtText
} DfbtWidgetType;

// Types of Alignment
typedef enum {
  DfbtAlignTopLeft,
  DfbtAlignTopCenter,
  DfbtAlignTopRight,
  DfbtAlignCenterLeft,
  DfbtAlignCenterCenter,
  DfbtAlignCenter = DfbtAlignCenterCenter,
  DfbtAlignCenterRight,
  DfbtAlignBaseLeft,
  DfbtAlignBaseCenter,
  DfbtAlignBaseRight,
  DfbtAlignBottomLeft,
  DfbtAlignBottomCenter,
  DfbtAlignBottomRight,
} DfbtAlign;

// State of images
typedef enum {
  DfbtImageInitialized,
  DfbtImageConnecting,
  DfbtImageLoading,
  DfbtImageComplete,
  DfbtImageError
} DfbtImageState;


// Signature for function pointer
//typedef int (*DfbImageCallback)( DfbImage *dfbi, void *usrData );


/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
// none


/*=========================================================================*\
       Prototypes 
\*=========================================================================*/
int               dfbtInit( const char *resourcePath );
void              dfbtShutdown( void );
IDirectFB        *dfbtGetDdb( void );
DfbtWidget       *dfbtGetScreen( void );
int               dfbtRedrawScreen( bool force );

void              dfbtRetain( DfbtWidget *widget );
void              dfbtRelease( DfbtWidget *widget );

void              dfbtGetSize( DfbtWidget *widget, int *width, int *height );
void              dfbtGetOffset( DfbtWidget *widget, int *x, int *y );
void              dfbtSetNeedsUpdate( DfbtWidget *widget );
void              dfbtSetBackground( DfbtWidget *widget, DFBColor *color );

DfbtWidget       *dfbtContainer( int width, int height );
int               dfbtContainerAdd( DfbtWidget *container, DfbtWidget *new, int x, int y, DfbtAlign align  );
int               dfbtContainerRemove( DfbtWidget *container, DfbtWidget *widget );
DfbtWidget       *dfbtContainerFind( DfbtWidget *root, DfbtWidget *widget );
int               dfbtContainerSetPosition( DfbtWidget *container, DfbtWidget *widget, int x, int y, DfbtAlign align );
void              dfbtContainerMovePosition( DfbtWidget *container, DfbtWidget *widget, int dx, int dy );

DfbtWidget       *dfbtText( const char *text, IDirectFBFont *font, DFBColor *color );
IDirectFBFont    *dfbtTextGetFont( DfbtWidget *widget );

DfbtWidget       *dfbtImage( int width, int height, const char *uri, bool isFile );
const char       *dfbtImageGetUri( DfbtWidget *widget );
DfbtImageState    dfbtImageGetState( DfbtWidget *widget );
int               dfbtImageWaitForComplete( DfbtWidget *widget, int timeout );

#endif  /* __DFBTTOOLS_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

