/*$*********************************************************************\

Name            : -

Source File     : player.h

Description     : Main include file for player.c 

Comments        : -

Date            : 24.02.2013 

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


#ifndef __PLAYER_H
#define __PLAYER_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <stdbool.h>
#include "playlist.h"
#include "audio.h"


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/
typedef enum {
  PlayerStateStop,
  PlayerStatePlay,
  PlayerStatePause
} PlayerState;

typedef enum {
  PlaybackQueue,
  PlaybackShuffle,
  PlaybackRepeatQueue,
  PlaybackRepeatItem,
  PlaybackRepeatShuffle,
  PlaybackDynamic
} PlayerPlaybackMode;


/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
// none


/*=========================================================================*\
       Prototypes 
\*=========================================================================*/

int                 playerInit( void );
void                playerShutdown( void );
const char         *playerPlaybackModeToStr( PlayerPlaybackMode mode );
PlayerPlaybackMode  playerPlaybackModeFromStr( const char *str );
Playlist           *playerGetQueue( void );
void                playerResetQueue( void );
PlayerState         playerGetState( void );
PlayerPlaybackMode  playerGetPlaybackMode( void );
double              playerGetLastChange( void );
const AudioFormat  *playerGetDefaultAudioFormat( void );
const char         *playerGetHWID( void );
const char         *playerGetIpAddress( void );
const char         *playerGetUUID( void );
const char         *playerGetName( void );
const char         *playerGetInterface( void );
const char         *playerGetAudioDevice( void );
const char         *playerGetModel( void );
double              playerGetVolume( void );
bool                playerGetMuting( void );
double              playerGetSeekPos( void );
int                 playerSetDefaultAudioFormat( const char *format );
void                playerSetUUID( const char *name );
void                playerSetInterface( const char *name );
void                playerSetAudioDevice( const char *name );
void                playerSetModel( const char *name );
void                playerSetName( const char *name, bool broadcast );
double              playerSetVolume( double volume, bool muted, bool broadcast );
int                 playerSetPlaybackMode( PlayerPlaybackMode state, bool broadcast );
int                 playerSetState( PlayerState state, bool broadcast );


#endif  /* __PLAYER_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

