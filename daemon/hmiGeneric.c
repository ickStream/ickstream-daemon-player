/*$*********************************************************************\

Name            : -

Source File     : hmiGeneric.c

Description     : Minimal HMI implementation 

Comments        : usefull for debugging only.
                  Own implementations hould be named hmiXXXX.c and 
                  selected by supplying a "hmi=XXXX" option to the configure script

Called by       : ickstream player

Calls           : 

Error Messages  : -
  
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

#include <stdio.h>
#include <jansson.h>

#include "utils.h"
#include "playlist.h"
#include "player.h"

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
static PlaylistItem *currentItem;


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
// None


/*=========================================================================*\
      Init HMI module
\*=========================================================================*/
int  hmiInit( void )
{
  DBGMSG( "Initializing HMI module..." );
  return 0;
}


/*=========================================================================*\
      Close HMI module
\*=========================================================================*/
void hmiShutdown( void )
{
  DBGMSG( "Shutting down HMI module..." );
}


/*=========================================================================*\
      Queue cursor is pointing to a new item
        item might be NULL, if no current track is defined...
\*=========================================================================*/
void hmiNewItem( Playlist *plst, PlaylistItem *item )
{
  DBGMSG( "new track: %p", item );
  currentItem = item;

  printf( "new track: %s\n", item?item->text:"<None>" );
}


/*=========================================================================*\
      Player state has changed
\*=========================================================================*/
void hmiNewState( PlayerState state )
{
  DBGMSG( "new playback state: %d", state );

  char *stateStr = "Unknown";
  switch( state ) {
    case PlayerStateStop:  stateStr = "Stopped"; break;	
    case PlayerStatePlay:  stateStr = "Playing"; break;
    case PlayerStatePause: stateStr = "Paused"; break;
  }

  printf( "playback: %s\n", stateStr );
}


/*=========================================================================*\
      Volume and muting setting has changed
\*=========================================================================*/
void hmiNewVolume( double volume, bool muted )
{
  DBGMSG( "new volume: %.2lf (muted: %s)", volume, muted?"On":"Off" );

  printf( "new volume: %.2lf (%s)\n", volume, muted?"muted":"not muted" );
}


/*=========================================================================*\
      New seek Position
\*=========================================================================*/
void hmiNewPosition( double seekPos )
{
  DBGMSG( "new seek position: %.2lf", seekPos );

  printf( "position: %.2lf\n", seekPos );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/




