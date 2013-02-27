/*$*********************************************************************\

Name            : -

Source File     : player.c

Description     : ickstream player model 

Comments        : -

Called by       : ickstream protocols 

Calls           : 

Error Messages  : -
  
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

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>

#include "ickpd.h"
#include "persist.h"
#include "player.h"

/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static const char *playerUUID;  
static const char *playerInterface;  
static const char *playerAudioDevice;  
static const char *playerHWID;  
static const char *playerModel;  
static const char *playerName;
static const char *accessToken;


/*=========================================================================*\
    Shut down player
\*=========================================================================*/
void playerShutdown( void )
{
  srvmsg( LOG_INFO, "Shutting down player module..." );
}


/*=========================================================================*\
    Get player hardware ID
      return NULL if not suppored
      We use the MAC adress as default (network name must be set).
\*=========================================================================*/
const char *playerGetHWID( void )
{
  struct ifreq request;
  int    fd, i, retcode;
  char   buf[10], hwid[20];

/*------------------------------------------------------------------------*\
    Already initialized?
\*------------------------------------------------------------------------*/
  if( playerHWID )
    return playerHWID;
    
/*------------------------------------------------------------------------*\
    Create ioctl dtata structure and open an INET socket
\*------------------------------------------------------------------------*/
  const char *ifname = playerGetInterface();
  if( !ifname )
    return NULL;
  strncpy( request.ifr_name, ifname, IFNAMSIZ-1 );
  fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);

/*------------------------------------------------------------------------*\
    Perform request and close socket
\*------------------------------------------------------------------------*/
  retcode = ioctl( fd, SIOCGIFHWADDR, &request );
  close( fd );
  
/*------------------------------------------------------------------------*\
    Error?
\*------------------------------------------------------------------------*/
  if( retcode ) {
    srvmsg( LOG_ERR, "Error inquiring MAC from \"%s\": %s ", 
                     playerInterface, strerror(errno) );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Setup result
\*------------------------------------------------------------------------*/
  *hwid = 0;
  for( i=0; i<6; i++ ) {
    sprintf( buf, "%s%02X", i?":":"", (unsigned char)request.ifr_addr.sa_data[i] );
    strcat( hwid, buf );
  }
  
/*------------------------------------------------------------------------*\
    Store and return result
\*------------------------------------------------------------------------*/
  srvmsg( LOG_INFO, "Hardware ID is \"%s\"", hwid );
  playerHWID = strdup( hwid );
  return playerHWID;
}


/*=========================================================================*\
    Get player UUID
\*=========================================================================*/
const char *playerGetUUID( void )
{
  if( !playerUUID )
    playerUUID = persistGetString( "PlayerName" );
  return playerUUID;
}


/*=========================================================================*\
    Get player Model
\*=========================================================================*/
const char *playerGetModel( void )
{
  if( !playerModel )
    playerModel = "Generic UN*X Player";
  return playerModel;
}


/*=========================================================================*\
    Get network interface name
\*=========================================================================*/
const char *playerGetInterface( void )
{
  if( !playerInterface )
    playerInterface = persistGetString( "PlayerInterface" );
  return playerInterface;
}


/*=========================================================================*\
    Get audio device name
\*=========================================================================*/
const char *playerGetAudioDevice( void )
{
  if( !playerAudioDevice )
    playerAudioDevice = persistGetString( "PlayerAudioDevice" );
  return playerAudioDevice;
}


/*=========================================================================*\
    Get player name
\*=========================================================================*/
const char *playerGetName( void )
{
  if( !playerName )
    playerName = persistGetString( "PlayerName" );
  return playerName;
}


/*=========================================================================*\
    Get access token
\*=========================================================================*/
const char *playerGetToken( void )
{
  if( !accessToken )
    accessToken = persistGetString( "IckAccessToken" );
  return accessToken;
}


/*=========================================================================*\
    Set player UUID
\*=========================================================================*/
void playerSetUUID( const char *uuid )
{	
  srvmsg( LOG_INFO, "Setting player UUID to \"%s\"", uuid );
  playerUUID = uuid;  
  persistSetString( "DeviceUUID", uuid );
}

/*=========================================================================*\
    Set player Model
\*=========================================================================*/
void playerSetModel( const char *model )
{	
  srvmsg( LOG_INFO, "Setting player model to \"%s\"", model );
  if( playerModel ) {
    srvmsg( LOG_ERR, "Can set player model only once" );
    return;
  }
  playerModel = model;  
} 


/*=========================================================================*\
    Set player name
\*=========================================================================*/
void playerSetName( const char *name )
{
  srvmsg( LOG_INFO, "Setting player name to \"%s\"", name );
  playerName = name;  
  persistSetString( "PlayerName", name );
}


/*=========================================================================*\
    Set network interface name
\*=========================================================================*/
void playerSetInterface( const char *name )
{
  srvmsg( LOG_INFO, "Setting interface name to \"%s\"", name );
  if( playerInterface ) {
    srvmsg( LOG_ERR, "Can set interface name only once" );
    return;
  }
  playerInterface = name;  
  persistSetString( "PlayerInterface", name );
}


/*=========================================================================*\
    Set audio device name
\*=========================================================================*/
void playerSetAudioDevice( const char *name )
{
  srvmsg( LOG_INFO, "Setting audio device to \"%s\"", name );
  playerAudioDevice = name;  
  persistSetString( "PlayerAudioDevice", name );
}


/*=========================================================================*\
    Set access Token
\*=========================================================================*/
void playerSetToken( const char *token )
{
  srvmsg( LOG_INFO, "Setting ickstream access token to \"%s\"", token );
  accessToken = token;  
  persistSetString( "IckAccessToken", token );
}





/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
