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
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/time.h>

#include "utils.h"

#define DUMPCOLS 16


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static int              streamloglevel = LOG_NOTICE;
static int              sysloglevel = LOG_ALERT;
static pthread_mutex_t  loggerMutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  counterMutex = PTHREAD_MUTEX_INITIALIZER;


void logSetStreamLevel( int prio )
{
  streamloglevel = prio;
}

void logSetSyslogLevel( int prio )
{
  sysloglevel = prio;
}

int logGetStreamLevel( void )
{
  return streamloglevel;
}

int    logGetSyslogLevel( void )
{
  return sysloglevel;
}


/*========================================================================*\
   Logging facility
\*========================================================================*/
void _mylog( const char *file, int line,  int prio, const char *fmt, ... )
{

/*------------------------------------------------------------------------*\
    Init arguments, lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &loggerMutex );
  va_list a_list;
  va_start( a_list, fmt );
  
/*------------------------------------------------------------------------*\
   Log to stream 
\*------------------------------------------------------------------------*/
  if( prio<=streamloglevel) {

    // select stream due to priority
    //FILE *f = (prio<LOG_INFO) ? stderr : stdout;
    FILE *f = stderr;

    // print timestamp, prio and thread info
    fprintf( f, "%.4f %d [%p]", srvtime(), prio, (void*)pthread_self() );

    // prepend location to message (if available)
    if( file )
      fprintf( f, " %s,%d: ", file, line );
    else
      fprintf( f, ": " );

    // the message itself
    vfprintf( f, fmt, a_list );
  
    // New line and flush stream buffer
    fprintf( f, "\n" );
    fflush( f );
  }

/*------------------------------------------------------------------------*\
    use syslog facility, hide debugging messages from syslog
\*------------------------------------------------------------------------*/
  if( prio<=sysloglevel && prio<LOG_DEBUG)
    vsyslog( prio, fmt, a_list );
  
/*------------------------------------------------------------------------*\
    Clean variable argument list, unlock mutex
\*------------------------------------------------------------------------*/
  va_end ( a_list );
  pthread_mutex_unlock( &loggerMutex );
}


/*========================================================================*\
   Dump memory area (stream only)
\*========================================================================*/
void _mydump( const char *file, int line, int prio, const char *title, const void *ptr, size_t size )
{
  char  prefix[100];
  char  dline[4*DUMPCOLS+10];
  int   col;
  FILE *f;
  const unsigned char *cptr = ptr;

/*------------------------------------------------------------------------*\
   Nothing to do
\*------------------------------------------------------------------------*/
  if( prio>streamloglevel )
    return;

/*------------------------------------------------------------------------*\
   Select stream
\*------------------------------------------------------------------------*/
  //FILE *f = (prio<LOG_INFO) ? stderr : stdout;
  f = stderr;

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &loggerMutex );

/*------------------------------------------------------------------------*\
    Create prefix
\*------------------------------------------------------------------------*/
  if( !file )
    sprintf( prefix, "%.4f %d [%p]:", srvtime(), prio, (void*)pthread_self() );
  else
    sprintf( prefix, "%.4f %d [%p] %s,%d:", srvtime(), prio, (void*)pthread_self(), file, line );

/*------------------------------------------------------------------------*\
    Print header
\*------------------------------------------------------------------------*/
  fprintf( f, "%s %s - Dumping %ld bytes starting at %p\n",
           prefix, title, (long)size, ptr );

/*------------------------------------------------------------------------*\
    loop over lines
\*------------------------------------------------------------------------*/
  while( size ) {

    // Init line to space characters
    memset( dline, ' ', sizeof(dline) );
    dline[sizeof(dline)-1]   = 0;

    // Build line: loop over bytes
    for( col=0,ptr=cptr; size && col<DUMPCOLS; size--,cptr++,col++ ) {
      char buf[4];
      // hex
      sprintf( buf, "%02x", *cptr );
      strncpy( dline+3*col, buf, 2 );
      // printable
      dline[3*DUMPCOLS+2+col] = isprint(*cptr)?*cptr:'.';
    }

    // Print line
    fprintf( f, "%s %p - %s\n", prefix, ptr, dline );
  }  // Next line

/*------------------------------------------------------------------------*\
    Flush output and unlock mutex
\*------------------------------------------------------------------------*/
  fflush( f );
  pthread_mutex_unlock( &loggerMutex );
}


/*========================================================================*\
   Draw a random nuber from range [min, max] (both included)
\*========================================================================*/
long rndInteger( long min, long max )
{
  static unsigned int seed = 0;

  // Init with seed?
  if( !seed ) {
    seed = (unsigned int) srvtime();
    DBGMSG( "rndSeed = %u", seed );
    srandom( seed );
  }

  // Use simple random facility
  long rnd = random();

  // Scale to requested range
  rnd =  min + rnd%(max-min+1L);

  DBGMSG( "rnd[%ld,%ld] = %ld", min, max, rnd );
  return rnd;
}

/*========================================================================*\
   Get time including frational seconds
\*========================================================================*/
double srvtime( void )
{
  struct timeval tv;
  gettimeofday( &tv, NULL );
  return tv.tv_sec+tv.tv_usec*1E-6;
}


/*========================================================================*\
   Recursively merge a JSON hierarchy into a target one
     Arrays are handled as basic types, i.e. not deeply merged
\*========================================================================*/
int json_object_merge( json_t *target, json_t *source )
{
  void *iter;

/*------------------------------------------------------------------------*\
    Both arguments need to be objects
\*------------------------------------------------------------------------*/
  if( !json_is_object(target) || !json_is_object(source) )
    return -1;

/*------------------------------------------------------------------------*\
    Loop over all elements in source
\*------------------------------------------------------------------------*/
  for( iter=json_object_iter(source); iter; iter=json_object_iter_next(source,iter) ) {
    const char *key     = json_object_iter_key( iter );
    json_t     *srcElem = json_object_iter_value( iter );
    json_t     *trgElem = json_object_get( target, key );

    // Do recursion if element exists in target and is an object
    if( trgElem && json_is_object(trgElem) && json_is_object(srcElem) ) {
      if( json_object_merge(trgElem,srcElem) )
        return -1;
    }

    // Add or replace if target element does not exist or one element is of basic type
    else if( json_object_set_nocheck(target,key,srcElem) )
      return -1;
  }

/*------------------------------------------------------------------------*\
    That's all (no more elements in source)
\*------------------------------------------------------------------------*/
  return 0;
}


/*========================================================================*\
   Get a string description for an JSON-RPC error object
     This is a not reentrant function for debugging only.
\*========================================================================*/
const char *json_rpcerrstr( json_t *jError )
{
  static char *str;

  // Free result of last call
  Sfree( str );

  // Use library function (fixme with formated output)
  str = json_dumps( jError, JSON_PRESERVE_ORDER | JSON_COMPACT | JSON_ENSURE_ASCII );

  // Return result
  return str;
}


/*========================================================================*\
   Get and increment a global counter in an atomic way
\*========================================================================*/
long getAndIncrementCounter( void )
{
  static long counter = 0;
  long        retval;

/*------------------------------------------------------------------------*\
    Init counter with persisted last value
\*------------------------------------------------------------------------*/
  // Fixme

/*------------------------------------------------------------------------*\
    Increment counter
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &counterMutex );
  retval = ++counter;
  pthread_mutex_unlock( &counterMutex );

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return retval;
}


/*========================================================================*\
   Check if a string starts with a given prefix.
     Returns 0 if so non-zero otherwise.
\*========================================================================*/
int strcmpprefix( const char *str, const char *prefix )
{
  return strncmp( str, prefix, strlen(prefix) );
}

/*========================================================================*\
   Convert a ISO-8859-1 coded ASCII string to UTF-8
     Returns an allocated converted string (needs to be freed) or
             NULL on error.
     Note: this will not work with windows CP-1252 strings...
\*========================================================================*/
char *strIso88591toUtf8( const char *str, const char *pbrk )
{
  char *result = malloc( 2*strlen(str)+1 );
  const unsigned char *orig = (const unsigned char*)str;
  unsigned char *dest;

  if( !result ) {
    logerr( "strIso88591toUtf8: Out of memory." );
    return NULL;
  }

  for( dest=(unsigned char *)result;
       *orig && (!pbrk || orig<(unsigned char *)pbrk); orig++ ) {
    if( *orig<128 )
      *dest++ = *orig;
    else {
      *dest++ = (*orig>0xbf)? 0xc3 : 0xc2;
      *dest++ = 0x80 + (*orig&0x3f);
    }
  }
  *dest = 0;

  DBGMSG( "strIso88591toUtf8: \"%.*s\" -> \"%s\"",
           pbrk?pbrk-str:strlen(str), str, result );

  return realloc( result, strlen(result)+1 );
}



/*========================================================================*\
   Log memory usage (by this code only)
\*========================================================================*/
#undef malloc
void *_smalloc( const char *file, int line, size_t s )
{
  void *ptr = malloc( s );
  _mylog( file, line, LOG_DEBUG, "malloc(%ld) = %p", (long) s, ptr );
  return ptr;
}

#undef calloc
void  *_scalloc( const char *file, int line, size_t n, size_t s )
{
  void *ptr = calloc( n, s );
  _mylog( file, line, LOG_DEBUG, "calloc(%ld,%ld) = %p", (long)n, (long)s, ptr );
  return ptr;
}

#undef realloc
void  *_srealloc( const char *file, int line, void *optr, size_t s )
{
  void *ptr = realloc( optr, s );
  _mylog( file, line, LOG_DEBUG, "realloc(%p,%ld) = %p", optr, (long)s, ptr );
  return ptr;
}

#undef strdup
char  *_sstrdup( const char *file, int line, const char *s )
{
  void *ptr = strdup( s );
  _mylog( file, line, LOG_DEBUG, "strdup(%s (%p)) = %p", s, s, ptr );
  return ptr;
}



/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
