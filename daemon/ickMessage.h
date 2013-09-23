/*$*********************************************************************\

Name            : -

Source File     : ickMessage.h

Description     : Main include file for ickMessage.c 

Comments        : -

Date            : 20.02.2013 

Updates         : 03.04.2013 added Json RPC error codes   //MAF

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


#ifndef __ICKMESSAGE_H
#define __ICKMESSAGE_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <ickP2p.h>


/*=========================================================================*\
       Macro and type definitions
\*=========================================================================*/

// Some Json-RPC error codes
#define RPC_NO_ERROR              0
#define RPC_PARSE_ERROR      -32700
#define RPC_INVALID_REQUEST  -32600
#define RPC_METHOD_NOT_FOUND -32601
#define RPC_INVALID_PARAMS   -32602
#define RPC_INTERNAL_JSONRPC -32603
#define RPC_GENERIC_ERROR    -32000


/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
typedef void (*IckCmdCallback)(const char *szDeviceId, json_t *jCmd, json_t *jResult);  


/*========================================================================*\
   Prototypes
\*========================================================================*/
void  ickMessage( ickP2pContext_t *ictx, const char *sourceUuid, ickP2pServicetype_t sourceService,
                  ickP2pServicetype_t targetServices, const char *message, size_t mSize, ickP2pMessageFlag_t mFlags );
ickErrcode_t sendIckMessage( ickP2pContext_t *ictx, const char *szDeviceId, json_t *jMsg );
ickErrcode_t sendIckCommand( ickP2pContext_t *ictx, const char *szDeviceId, const char *method, json_t *jParams, int *requestID, IckCmdCallback callBack );

void ickMessageNotifyPlaylist( const char *szDeviceId );
void ickMessageNotifyPlayerState( const char *szDeviceId );

#endif  /* __ICKMESSAGE_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

