/*$*********************************************************************\

Name            : -

Source File     : audio.c

Description     : audio control 

Comments        : -

Called by       : ickstream protocol 

Calls           : 

Error Messages  : -
  
Date            : 20.02.2013

Updates         : 24.02.2023  make valume and mute flag persistent //MAF
                  
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
#include <strings.h>

#include "ickpd.h"
#include "audio.h"
#include "persist.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static double playerLastChange;
static double playerVolume;
static bool   playerMuted;
static bool   playerPlaying;
static double playerSeekPos;

/*=========================================================================*\
      init audio module 
\*=========================================================================*/
int audioInit( void )
{

/*------------------------------------------------------------------------*\
    Get persistence values, safe default is volume 0 and unmuted 
\*------------------------------------------------------------------------*/
  playerVolume = persistGetReal( "AudioVolume" );
  playerMuted  = persistGetBool( "AudioMuted" );  	

/*------------------------------------------------------------------------*\
    That's it 
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      shutdown audio module 
\*=========================================================================*/
void audioShutdown( void )
{
  srvmsg( LOG_INFO, "Shutting down audio module...");
}


/*=========================================================================*\
      Get Timestamp of last config change 
\*=========================================================================*/
double audioGetLastChange( void )
{
  return playerLastChange;    
}

/*=========================================================================*\
      Get Volume 
\*=========================================================================*/
double audioGetVolume()
{
  return playerVolume;
}


/*=========================================================================*\
      Get Muting state 
\*=========================================================================*/
bool  audioGetMuting()
{
  return playerMuted;
}


/*=========================================================================*\
      Get playback state 
\*=========================================================================*/
bool   audioGetPlayingState( void )
{
  return playerPlaying;	
}

/*=========================================================================*\
      Get playback state 
\*=========================================================================*/
double audioGetSeekPos( void )
{
  return playerSeekPos;	
}

/*=========================================================================*\
      Set Volume 
\*=========================================================================*/
double audioSetVolume( double volume )
{
  srvmsg( LOG_INFO, "Setting Volume to %lf", volume );

/*------------------------------------------------------------------------*\
    Clip value 
\*------------------------------------------------------------------------*/
  if( volume<0 )
    volume = 0;
  if( volume>1 )
    volume = 1;

/*------------------------------------------------------------------------*\
    Update timestamp 
\*------------------------------------------------------------------------*/
  playerLastChange = srvtime( );

/*------------------------------------------------------------------------*\
    Store and return new volume 
\*------------------------------------------------------------------------*/
  playerVolume = volume;
  persistSetReal( "AudioVolume", volume );
  return playerVolume;
}


/*=========================================================================*\
      Set Muting 
\*=========================================================================*/
bool audioSetMuting( bool muted )
{
  srvmsg( LOG_INFO, "Setting Muting to %s", muted?"On":"Off");

/*------------------------------------------------------------------------*\
    Update timestamp 
\*------------------------------------------------------------------------*/
  playerLastChange = srvtime( );

/*------------------------------------------------------------------------*\
    Store and return new state 
\*------------------------------------------------------------------------*/
  playerMuted = muted;
  persistSetBool( "AudioMuted", muted );
  return playerMuted;
}





/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
