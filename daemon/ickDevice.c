/*$*********************************************************************\

Name            : -

Source File     : ickDevice.c

Description     : implement ickstream device management protocol 

Comments        : -

Called by       : ickstream wrapper 

Calls           : 

Error Messages  : -
  
Date            : 20.02.2013

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
#include <jansson.h>
#include <ickP2p.h>

#include "ickutils.h"
#include "hmi.h"
#include "ickDevice.h"
#include "ickMessage.h"
#include "ickService.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static void _handleGetServiceInformation( const char *szDeviceId, json_t *jCmd, json_t *jResult );
static const char *_ickDeviceServiceTypeToStr( ickP2pServicetype_t type );


/*=========================================================================*\
        Handle device announcements
\*=========================================================================*/
void ickDevice( ickP2pContext_t *ictx, const char *uuid,
                ickP2pDeviceState_t cmd, ickP2pServicetype_t type )
{
  
  loginfo( "ickDevice \"%s\" (type %d: %s) %s",
            uuid, type, _ickDeviceServiceTypeToStr(type), ickLibDeviceState2Str(cmd) );

  switch( cmd ) {

    case ICKP2P_CONNECTED:

      // New server found: request service descriptor
      if( type==ICKP2P_SERVICE_SERVER_GENERIC ) {
        sendIckCommand( ictx, uuid, "getServiceInformation", NULL, NULL,
                                    &_handleGetServiceInformation );
      }

      // New controller found: send current player state
      if( type==ICKP2P_SERVICE_CONTROLLER )
        ickMessageNotifyPlayerState( uuid );

      break;

    case ICKP2P_DISCONNECTED:

      // Remove service(s) for this device
      ickServiceRemove( uuid, NULL, ServiceDevice );

      break;

    case ICKP2P_INITIALIZED:
    case ICKP2P_ERROR:
    case ICKP2P_INVENTORY:
    case ICKP2P_DISCOVERED:
    case ICKP2P_BYEBYE:
    case ICKP2P_EXPIRED:
    case ICKP2P_TERMINATE:
      break;

    default:
      logwarn( "ickDeviceCallback: Unknown message %d", cmd );
      break;
  }

/*------------------------------------------------------------------------*\
    That's it.
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
        Call back for get service info
\*=========================================================================*/
static void _handleGetServiceInformation( const char *szDeviceId, json_t *jCmd, json_t *jResult)
{ 
  json_t *jObj;

/*------------------------------------------------------------------------*\
    Check for error
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jResult, "error" );
  if( jObj ) {
    logwarn( "getServiceInformation from %s: %s.", szDeviceId, json_rpcerrstr(jObj) );
    return;
  }

/*------------------------------------------------------------------------*\
    Get result object conten, which is the service definition
\*------------------------------------------------------------------------*/
  jObj = json_object_get( jResult, "result" );
  if( !jObj || !json_is_object(jObj) ) {
    logerr( "getServiceInformation from %s: no result field", szDeviceId );
    return;
  } 

/*------------------------------------------------------------------------*\
    Add service
\*------------------------------------------------------------------------*/
  ickServiceAdd( jObj, ServiceDevice );

/*------------------------------------------------------------------------*\
    Inform HMI
\*------------------------------------------------------------------------*/
  hmiNewConfig( );
}


/*=========================================================================*\
    Convert service bit vector to string
\*=========================================================================*/
static const char *_ickDeviceServiceTypeToStr( ickP2pServicetype_t type )
{
  static char buffer[128];
  *buffer = 0;

  if( type==ICKP2P_SERVICE_ANY )
    return "any";
  if( type==ICKP2P_SERVICE_NONE )
    return "none";
  if( type==ICKP2P_SERVICE_GENERIC )
    return "generic";

  if( type&ICKP2P_SERVICE_PLAYER )
    strcat( buffer, "player" );

  if( type&ICKP2P_SERVICE_CONTROLLER ) {
    if( *buffer )
      strcat( buffer, "," );
    strcat( buffer, "controller" );
  }

  if( type&ICKP2P_SERVICE_SERVER_GENERIC ) {
    if( *buffer )
      strcat( buffer, "," );
    strcat( buffer, "server-generic" );
  }

  if( type&ICKP2P_SERVICE_DEBUG ) {
    if( *buffer )
      strcat( buffer, "," );
    strcat( buffer, "debug" );
  }

  return buffer;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/


