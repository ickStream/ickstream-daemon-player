/*$*********************************************************************\

Name            : -

Source File     : dfbToolsInternal.h

Description     : Internals for direct frame buffer toolkit

Comments        : -

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


#ifndef __DFBTTOOLSINTERNAL_H
#define __DFBTTOOLSINTERNAL_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <stdbool.h>
#include <pthread.h>
#include <directfb.h>


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/

// Number of unused elements in image buffer
#define DfbtImageGarbageThreshold 100


// A toolkit widget
struct _dfbtwidget {
  DfbtWidgetType           type;
  char                    *name;
  int                      refCount;
  bool                     needsUpdate;
  DFBPoint                 offset;             // relative to container
  DFBDimension             size;
  DFBColor                 background;
  IDirectFB               *dfb;                // weak
  IDirectFBSurface        *surface;            // strong, might be null
  DfbtWidget              *next;               // strong
  DfbtWidget              *content;            // strong
  DfbtWidget              *parent;             // strong
  pthread_mutex_t          mutex;
  void                    *data;
};

/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
extern char              *_dfbtResourcePath;
extern DfbtRedrawRequest  _dfbtRedrawRequest;



/*=========================================================================*\
       Prototypes 
\*=========================================================================*/
DfbtWidget *_dfbtNewWidget( DfbtWidgetType type, int w, int h );
int         _dfbtWidgetDestruct( DfbtWidget *widget );

int         _dfbtSurfaceLock( IDirectFBSurface *surface );
int         _dfbtSurfaceUnlock( IDirectFBSurface *surface );

void        _dfbtTextDestruct( DfbtWidget *widget );
void        _dfbtTextDraw( DfbtWidget *widget );

void        _dfbtImageDestruct( DfbtWidget *widget );
void        _dfbtImageDraw( DfbtWidget *widget );
void        _dfbtImageFreeCache( void );


#endif  /* __DFBTTOOLSINTERNAL_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

