/*$*********************************************************************\

Name            : -

Source File     : playlist.h

Description     : Main include file for playlist.c 

Comments        : -

Date            : 21.02.2013 

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
 * this SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS for A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE for ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF this SOFTWARE, 
 * EVEN if ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\************************************************************************/


#ifndef __PLAYLIST_H
#define __PLAYLIST_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <stdbool.h>
#include <pthread.h>
#include <jansson.h>

/*=========================================================================*\
    Constants 
\*=========================================================================*/
// none


/*========================================================================n
    Macro and type definitions 
\*========================================================================*/

// A playlist
struct _playlist;
typedef struct _playlist Playlist;

// A playlist item (aka track) 
struct _playlistItem;
typedef struct _playlistItem  PlaylistItem;


/*=========================================================================*\
    Global symbols 
\*=========================================================================*/
// none


/*========================================================================*\
    Prototypes
\*========================================================================*/

Playlist     *playlistNew( void );
Playlist     *playlistFromJSON( json_t *jObj );
void          playlistDelete( Playlist *plst );
void          playlistSetId( Playlist *plst, const char *id );
void          playlistSetName( Playlist *plst, const char *name );
const char   *playlistGetId( Playlist *plst );
const char   *playlistGetName( Playlist *plst );
int           playlistGetLength( Playlist *plst );
double        playlistGetLastChange( Playlist *plst );
int           playlistGetCursorPos( Playlist *plst );
PlaylistItem *playlistSetCursorPos( Playlist *plst, int pos );
PlaylistItem *playlistIncrCursorItem( Playlist *plst );
int           playlistTranspose( Playlist *plst, int pos1, int pos2, bool moveCursor );
PlaylistItem *playlistShuffle( Playlist *plst, int startPos, int endPos, bool moveCursorToStart );

json_t       *playlistGetJSON( Playlist *plst, int offset, int count );

PlaylistItem *playlistItemFromJSON( json_t *jItem );
void          playlistItemDelete( PlaylistItem *pItem );
const char   *playlistItemGetText( PlaylistItem *pItem );
const char   *playlistItemGetId( PlaylistItem *pItem );
json_t       *playlistItemGetJSON( PlaylistItem *pItem );
json_t       *playlistItemGetStreamingRefs( PlaylistItem *pItem );

PlaylistItem *playlistGetItem( Playlist *plst, int pos );
PlaylistItem *playlistGetItemById( Playlist *plst, const char *id );
PlaylistItem *playlistGetCursorItem( Playlist *plst );
int           playlistAddItems( Playlist *plst, int pos, json_t *jItems, bool resetFlag );
int           playlistDeleteItems( Playlist *plst, json_t *jItems );
int           playlistMoveItems( Playlist *plst, int pos, json_t *jItems );


void          playlistAddItemBefore( Playlist *plst, PlaylistItem *anchorItem, PlaylistItem *newItem );
void          playlistAddItemAfter( Playlist *plst, PlaylistItem *anchorItem, PlaylistItem *newItem );
void          playlistUnlinkItem( Playlist *plst, PlaylistItem *pItem );

#endif  /* __PLAYLIST_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

