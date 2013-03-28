/*$*********************************************************************\

Name            : -

Source File     : utils.h

Description     : Main include file for fifo.c 

Comments        : Posix log levels:
                   LOG_EMERG    0   system is unusable
                   LOG_ALERT    1   action must be taken immediately
                   LOG_CRIT     2   critical conditions
                   LOG_ERR      3   error conditions
                   LOG_WARNING  4   warning conditions
                   LOG_NOTICE   5   normal but significant condition
                   LOG_INFO     6   informational    
                   LOG_DEBUG    7   debug-level messages

Date            : 08.03.2013 

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


#ifndef __UTILS_H
#define __UTILS_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include "stdio.h"
#include "syslog.h"


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#define srvmsg(prio, args...) _srvlog( NULL, 0, prio, args )

#ifdef ICK_DEBUG
#define DBGMSG( args... ) _srvlog( __FILE__, __LINE__, LOG_DEBUG, args )
//#define DBGMSG( args... ) printf( args );
#else
#define DBGMSG( args... ) { ;}
#endif

#define logerr( args... )     _srvlog( __FILE__, __LINE__, LOG_ERR, args )
#define logwarn( args... )    _srvlog( __FILE__, __LINE__, LOG_WARNING, args )
#define lognotice( args... )  _srvlog( __FILE__, __LINE__, LOG_NOTICE, args )
#define loginfo( args... )    _srvlog( __FILE__, __LINE__, LOG_INFO, args )

#if MEMDEBUG
#define Sfree(p) {DBGMSG("free %p", (p)); if(p)free(p); (p)=NULL;}
#define malloc(s)    _smalloc( __FILE__, __LINE__, (s) )
#define calloc(n,s)  _scalloc( __FILE__, __LINE__, (n), (s) )
#define realloc(p,s) _smalloc( __FILE__, __LINE__, (p), (s) )
#define strdup( s )  _sstrdup( __FILE__, __LINE__, (s) )
#else
#define Sfree(p) {if(p)free(p); (p)=NULL;}
#endif

/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
extern int  streamloglevel;
extern int  sysloglevel;


/*========================================================================*\
   Prototypes
\*========================================================================*/
double srvtime( void );
long   rndInteger( long min, long max );
void   _srvlog( const char *file, int line, int prio, const char *fmt, ... );
void  *_smalloc( const char *file, int line, size_t s );
void  *_scalloc( const char *file, int line, size_t n, size_t s );
void  *_srealloc( const char *file, int line, void *p, size_t s );
char  *_sstrdup( const char *file, int line, const char *s );

#endif  /* __UTILS_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

