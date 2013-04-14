/*$*********************************************************************\

Name            : -

Source File     : persist.h

Description     : Main include file for persist.c 

Comments        : Manager for persistent value storage

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


#ifndef __PERSIST_H
#define __PERSIST_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <jansson.h>
#include <stdbool.h>


/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
int  persistSetFilename( const char *name );
void persistShutdown( void );

int  persistSetJSON( const char *key, json_t *jObj );
int  persistSetJSON_new( const char *key, json_t *value );
int  persistSetString( const char *key, const char *value );
int  persistSetInteger( const char *key,  int value );
int  persistSetReal( const char *key, double value );
int  persistSetBool( const char *key, bool value );
int  persistRemove( const char *key );

json_t     *persistGetJSON( const char *key );
const char *persistGetString( const char *key );
int         persistGetInteger( const char *key );
double      persistGetReal( const char *key );
bool        persistGetBool( const char *key );

#endif  /* __PERSIST_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

