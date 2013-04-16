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
#include <jansson.h>
#include <ickDiscovery.h>

#include "utils.h"
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
void _handleGetServiceInformation( const char *szDeviceId, json_t *jCmd, json_t *jResult );


/*=========================================================================*\
        Handle device announcements
\*=========================================================================*/
void ickDevice( const char *szDeviceId, enum ickDiscovery_command cmd, 
                 enum ickDevice_servicetype type )
{
  
  switch( cmd ) {

    case ICKDISCOVERY_ADD_DEVICE:
      loginfo( "ickDevice %s (type %d) added", szDeviceId, type );

      // New server found: request service descriptor
      if( type==ICKDEVICE_SERVER_GENERIC ) {
        sendIckCommand( szDeviceId, "getServiceInformation", NULL, NULL, 
                                    &_handleGetServiceInformation );
      }

      // New controller found: send current player state
      if( type==ICKDEVICE_CONTROLLER )
        ickMessageNotifyPlayerState( szDeviceId );

      break;

    case ICKDISCOVERY_REMOVE_DEVICE:
      loginfo( "ickDevice %s (type %d) removed", szDeviceId, type );

      // Remove service(s) for this device
      ickServiceRemove( szDeviceId, NULL, ServiceDevice );

      break;

    case ICKDISCOVERY_UPDATE_DEVICE:
      loginfo( "ickDevice %s (type %d) updated", szDeviceId, type );

      // Remove service(s) for this device
      ickServiceRemove( szDeviceId, NULL, ServiceDevice );

      // If (still) server: request service descriptor
      if( type==ICKDEVICE_SERVER_GENERIC ) {
        sendIckCommand( szDeviceId, "getServiceInformation", NULL, NULL,
                                    &_handleGetServiceInformation );
      }


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
void _handleGetServiceInformation( const char *szDeviceId, json_t *jCmd, json_t *jResult)
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
                                    END OF FILE
\*=========================================================================*/


