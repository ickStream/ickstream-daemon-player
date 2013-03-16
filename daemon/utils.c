/*$*********************************************************************\

Name            : -

Source File     : utils.c

Description     : some utilities 

Comments        : -

Called by       : - 

Calls           : 

Error Messages  : -
  
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/time.h>

#include "utils.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
int    srvloglevel = LOG_NOTICE;

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
pthread_mutex_t  loggerMutex = PTHREAD_MUTEX_INITIALIZER;


/*========================================================================*\
   Logging facility
\*========================================================================*/
void _srvlog( const char *file, int line,  int prio, const char *fmt, ... )
{
	
/*------------------------------------------------------------------------*\
    Do nothing if prio is too low
\*------------------------------------------------------------------------*/
  if( srvloglevel<prio )
    return;
    
/*------------------------------------------------------------------------*\
    Init arguments, lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &loggerMutex );
  va_list a_list;
  va_start( a_list, fmt );
  
/*------------------------------------------------------------------------*\
    select stream due to priority
\*------------------------------------------------------------------------*/
  // FILE *f = (prio<LOG_INFO) ? stderr : stdout;
  FILE *f = stderr;
  
/*------------------------------------------------------------------------*\
    prepend location to message (if available)
\*------------------------------------------------------------------------*/
  if( file )
    fprintf( f, "%.4f [%p] %s,%d: ", srvtime(), (void*)pthread_self(), file, line );
  vfprintf( f, fmt, a_list);
  fprintf( f, "\n");
  fflush( f );

/*------------------------------------------------------------------------*\
    use syslog facility
\*------------------------------------------------------------------------*/
  vsyslog( prio, fmt, a_list );
  
/*------------------------------------------------------------------------*\
    Clean variable argument list, unlock mutex
\*------------------------------------------------------------------------*/
  va_end ( a_list );
  pthread_mutex_unlock( &loggerMutex );
}


/*========================================================================*\
   Get time including frational seconds
\*========================================================================*/
double srvtime( void )
{
  struct timeval tv;
  gettimeofday( &tv, NULL );
  return tv.tv_sec+10e-6*tv.tv_usec;
}

/*========================================================================*\
   Log memory usage (by this code only)
\*========================================================================*/
#undef malloc
void *_smalloc( const char *file, int line, size_t s )
{
  void *ptr = malloc( s );
  _srvlog( file, line, LOG_DEBUG, "malloc(%ld) = %p", (long) s, ptr );	
  return ptr;
}

#undef calloc
void  *_scalloc( const char *file, int line, size_t n, size_t s )
{
  void *ptr = calloc( n, s );
  _srvlog( file, line, LOG_DEBUG, "calloc(%ld,%ld) = %p", (long)n, (long)s, ptr );	
  return ptr;
}

#undef realloc
void  *_srealloc( const char *file, int line, void *optr, size_t s )
{
  void *ptr = realloc( optr, s );
  _srvlog( file, line, LOG_DEBUG, "realloc(%p,%ld) = %p", optr, (long)s, ptr );	
  return ptr;
}

#undef strdup
char  *_sstrdup( const char *file, int line, const char *s )
{
  void *ptr = strdup( s );
  _srvlog( file, line, LOG_DEBUG, "strdup(%s (%p)) = %p", s, s, ptr );	
  return ptr;
}



/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
