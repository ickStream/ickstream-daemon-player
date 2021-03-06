/*$*********************************************************************\

Name            : -

Source File     : ickService.h

Description     : Main include file for ickService.c 

Comments        : -

Date            : 01.03.2013 

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


#ifndef __ICKSERVICE_H
#define __ICKSERVICE_H

/*=========================================================================*\
 *Includes needed by definitions from this file
\*=========================================================================*/
#include <stdbool.h>
#include <jansson.h> 

#include "playlist.h"


/*========================================================================n
  Macro and type definitions
\*========================================================================*/

// Where is a service located
typedef enum {
  ServiceDevice = 0x01,
  ServiceCloud  = 0x02
} ServiceOrigin;

// A service list item
struct _serviceListItem;
typedef struct _serviceListItem ServiceListItem;

#define IckServiceSchemePrefix   "service://"


/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
int              ickServiceAdd( json_t *jService, ServiceOrigin origin );
int              ickServiceAddFromCloud( const char *type, bool reset );
void             ickServiceRemove( const char *id, const char *type, ServiceOrigin origin );
ServiceListItem *ickServiceFind( ServiceListItem *item, const char *id, const char *type, ServiceOrigin origin );

char            *ickServiceResolveURI( const char* uri, const char* type );
json_t          *ickServiceGetStreamingRef( PlaylistItem *item );


json_t          *ickServiceGetJSON( const ServiceListItem *item );
const char      *ickServiceGetId( const ServiceListItem *item );
const char      *ickServiceGetName( const ServiceListItem *item );
const char      *ickServiceGetType( const ServiceListItem *item );
const char      *ickServiceGetURI( const ServiceListItem *item );
const char      *ickServiceGetServiceURI( const ServiceListItem *item );

#endif  /* __ICKSERVICE_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

