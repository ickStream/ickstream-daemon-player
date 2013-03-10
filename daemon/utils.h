/*$*********************************************************************\

Name            : -

Source File     : utils.h

Description     : Main include file for fifo.c 

Comments        : -

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
#define Sfree(p) ((p)?free(p),(p)=NULL:NULL)

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))

#define srvmsg(prio, args...) _srvlog( NULL, 0, prio, args )

#ifdef DEBUG
#define DBGMSG( args... ) _srvlog( __FILE__, __LINE__, LOG_DEBUG, args )
#else
#define DBGMSG( args... ) { ;}
#endif


/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
extern int  srvloglevel;


/*========================================================================*\
   Prototypes
\*========================================================================*/
double srvtime( void );
void   _srvlog( const char *file, int line, int prio, const char *fmt, ... );



#endif  /* __UTILS_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

