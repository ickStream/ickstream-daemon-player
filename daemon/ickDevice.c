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
#include "ickpd.h"

#include <ickDiscovery.h>
#include <jansson.h>

#include "ickDevice.h"

/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
// none

/*=========================================================================*\
        Handle device annuncements
\*=========================================================================*/
void ickDevice( const char *szDeviceId, enum ickDiscovery_command cmd, 
                 enum ickDevice_servicetype type )
{

  switch( cmd ) {

    case ICKDISCOVERY_ADD_DEVICE:
      srvmsg( LOG_INFO, "ickDevice %s (type %d) added", szDeviceId, type );
      break;

    case ICKDISCOVERY_REMOVE_DEVICE:
      srvmsg( LOG_INFO, "ickDevice %s (type %d) removed", szDeviceId, type );
      break;

    case ICKDISCOVERY_UPDATE_DEVICE:
      srvmsg( LOG_INFO, "ickDevice %s (type %d) updated", szDeviceId, type );
      break;

    default:
      srvmsg(LOG_WARNING, "ickDeviceCallback: Unknown message %d", cmd );
      break;
  }

/*------------------------------------------------------------------------*\
    That's it.
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
