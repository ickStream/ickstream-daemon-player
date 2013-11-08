/*$*********************************************************************\

Name            : -

Source File     : ickCloud.h

Description     : Main include file for ickCloud.c

Comments        : -

Date            : 10.04.2013

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


#ifndef __ICKCLOUD_H
#define __ICKCLOUD_H

/*=========================================================================*\
  Includes needed by definitions from this file
\*=========================================================================*/
#include <jansson.h> 

/*========================================================================n
  Macro and type definitions
\*========================================================================*/
#define IckCloudCoreURI "http://api.ickstream.com/ickstream-cloud-core/jsonrpc"

/*------------------------------------------------------------------------*\
    Signatures for function pointers
\*------------------------------------------------------------------------*/
typedef void (*IckCloudCb)( const char *method, json_t *jParams, json_t *jResult, int rc, int httpCode, void *userData  );


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
int         ickCloudInit( void );
void        ickCloudShutdown( void );
int         ickCloudSetCoreUrl( const char *url );
const char *ickCloudGetCoreUrl( void );
int         ickCloudRegisterDevice( const char *token );
//int         ickCloudSetAccessToken( const char *token );
const char *ickCloudGetAccessToken( void );

int         ickCloudSetDeviceAddress( void );

json_t *ickCloudRequestSync( const char *uri, const char *oAuthToken, const char *method, json_t *jParams, int *httpCode );
int     ickCloudNotify( const char *uri, const char *oAuthToken, const char *method, json_t *jParams );
int     ickCloudRequestAsync( const char *uri, const char *oAuthToken, const char *method,
                              json_t *jParams, IckCloudCb callback, void *userData );
int     jsonRpcTransact( const char *uri, const char *oAuthToken, int id,
                         const char *method, json_t *jParams, json_t **jResult, int *httpCode );


#endif  /* __ICKCLOUD_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

