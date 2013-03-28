/*$*********************************************************************\

Name            : -

Source File     : hmi.h

Description     : Main include file for hmi.c 

Comments        : -

Date            : 17.03.2013 

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


#ifndef __HMI_H
#define __HMI_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <stdbool.h>
#include <jansson.h>
#include "audio.h"
#include "player.h"


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/
// none


/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
// none


/*=========================================================================*\
       Prototypes 
\*=========================================================================*/

int  hmiInit( void );
void hmiShutdown( void );
void hmiNewItem( Playlist *plst, PlaylistItem *item );
void hmiNewState( PlayerState state );
void hmiNewRepeatMode( PlayerRepeatMode mode );
void hmiNewVolume( double volume, bool muted );
void hmiNewFormat( AudioFormat *format );
void hmiNewPosition( double seekPos );

/*=========================================================================*\
       Dummies if no HMI is used
\*=========================================================================*/
#ifdef ICK_NOHMI
#define hmiInit()           0
#define hmiShutdown()       {}
#define hmiNewItem(a,b)     {}
#define hmiNewState(a)      {}
#define hmiNewRepeatMode(a) {}
#define hmiNewVolume(a,b)   {}
#define hmiNewFormat(a)     {}
#define hmiNewPosition(a)   {}
#endif

#endif  /* __HMI_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

