/*$*********************************************************************\

Name            : -

Source File     : config.c

Description     : Handle arguments and configuration 

Called by       : main module
  
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <stdbool.h>

#include "ickpd.h"
#include "config.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
/* none */

/*=========================================================================*\
	Private symbols
\*=========================================================================*/

/*------------------------------------------------------------------------*\
   Argument handling
\*------------------------------------------------------------------------*/
typedef struct _argdsc {
  struct _argdsc     *next;
  bool                inconfig;
  const char         *name;
  const char         *sname;
  void               *buffer;
  const char         *valname;
  const char         *help;
  const char         *defval;
} ARGDSC;
static ARGDSC *firstarg;


/*=========================================================================*\
        Read config file 
\*=========================================================================*/
int readconfig( const char *fname )
{
  FILE           *fp;
  int             line = 0;
  char            buffer[2048];
  char           *ptr;
  char           *keyword;
  ARGDSC         *argptr;
   
/*------------------------------------------------------------------------*\
    Try to open file
\*------------------------------------------------------------------------*/  
  if( !fname )
    return 0;
  fp = fopen( fname,"r" );
  if( !fp ) {
    srvmsg( LOG_ERR, "Could not open %s: %s", fname, strerror(errno) );
    return 1;
  }

/*------------------------------------------------------------------------*\
    Read line, ignore empty lines and comments, get keyword and 
    dispatch into handlers
\*------------------------------------------------------------------------*/  
  while( fgets(buffer,sizeof(buffer)-1,fp) ) {
    line++;
    for( ptr=buffer; isspace(*ptr); ptr++ );	
    if( *ptr=='#' || *ptr==0 )
      continue;
    keyword  = strtok(ptr," \t\n");

/*------------------------------------------------------------------------*\
    Try all switch names
\*------------------------------------------------------------------------*/
    for( argptr=firstarg; argptr; argptr=argptr->next )  {
      if( !argptr->inconfig || strcmp(argptr->name,keyword) )
        continue; 
      char *value = strtok(NULL,"");
      while( isspace(*value) )
        value++;
             
/*-------------------------------------------------------------------------*\
	Switch without value? 
\*-------------------------------------------------------------------------*/
      if( !argptr->valname ) {
        if( value )
          srvmsg( LOG_ERR, "%s line %d: value for parameter %s ignored!\n", 
                  fname, line, keyword );
        (*(int*)argptr->buffer)++;
      }
      
/*-------------------------------------------------------------------------*\
	Switch with value? 
\*-------------------------------------------------------------------------*/
      else if( value )
        *(char **)argptr->buffer = strdup( value );
      else
        srvmsg( LOG_ERR, "%s line %d: value for parameter %s expected (line ignored)!\n", 
                fname, line, keyword );
      
/*-------------------------------------------------------------------------*\
	No need to test the rest. 
\*-------------------------------------------------------------------------*/
      break;
    }

/*------------------------------------------------------------------------*\
    Unknown keyword
\*------------------------------------------------------------------------*/
    if( !argptr ) {
      srvmsg( LOG_ERR, "%s line %d: unknown parameter %s (line ignored)!\n", 
              fname, line, keyword );
    }
  } /* endof:  while( fgets(buffer,sizeof(buffer)-1,fp) ) */

/*------------------------------------------------------------------------*\
    That's it...
\*------------------------------------------------------------------------*/
  fclose( fp );	  
  return 0;
}


/*=========================================================================*\
	Add an argument
\*=========================================================================*/
int addarg( const char *name, const char *sname, void *buffer, const char *valname, const char *help )
{
  ARGDSC *newarg,
         *walk;

/*-------------------------------------------------------------------------*\
    Check for limit in arg name
\*-------------------------------------------------------------------------*/
  if( strlen(name)>120 ) {
    srvmsg( LOG_ERR, "addarg: argument name too long: %s!\n", name );
    return -1;
  }

/*-------------------------------------------------------------------------*\
    OK, allocate a new argument descriptor and link it
\*-------------------------------------------------------------------------*/
  newarg = calloc( 1, sizeof(*newarg) );
  if( !firstarg )
    firstarg = newarg;
  else {
    for( walk=firstarg; walk->next; walk=walk->next );
    walk->next = newarg;
  }

/*-------------------------------------------------------------------------*\
    Use argument also in config file interpreter?
\*-------------------------------------------------------------------------*/
  newarg->inconfig = ( *name=='*' );
  if( newarg->inconfig )
    name++;

/*-------------------------------------------------------------------------*\
    Store the values.
\*-------------------------------------------------------------------------*/
  newarg->name    = name;
  newarg->sname   = sname;
  newarg->buffer  = buffer;
  newarg->valname = valname;
  newarg->help    = help;
  newarg->defval  = (valname&&buffer&&*(char**)buffer)?strdup(*(char**)buffer):NULL;

/*-------------------------------------------------------------------------*\
    That's all ...
\*-------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
	Give some hints about the usage
\*=========================================================================*/
void usage( const char *prgname, int iserr )
{
  ARGDSC *arg;
  FILE* f = iserr ? stderr : stdout;

/*-------------------------------------------------------------------------*\
    Print header
\*-------------------------------------------------------------------------*/
  fprintf( f, "Usage  : %s [option [value]]\n", prgname ); 
  fprintf( f, "Options:\n" );

/*-------------------------------------------------------------------------*\
    Print each option
\*-------------------------------------------------------------------------*/
  for( arg=firstarg; arg; arg=arg->next ) {
    fprintf( f, "%c --%-10s %-4s %-10s - %s", 
                arg->inconfig?'c':' ',
                arg->name,
		       	arg->sname?arg->sname:"",
                arg->valname?arg->valname:"",
                arg->help );
    if( arg->defval )
      fprintf( f, " (default: \"%s\")\n", arg->defval );
    else
      fprintf( f, "\n" );
  }
  
  fprintf( f, "Options flagged by 'c' might be used in the config file.\n" );
  
/*-------------------------------------------------------------------------*\
    That's all.
\*-------------------------------------------------------------------------*/
}


/*=========================================================================*\
	Parse command line parameters
\*=========================================================================*/
int getargs( int argc, char *argv[] )
{
  int      arg;
  ARGDSC  *argptr = NULL;
  
/*-------------------------------------------------------------------------*\
	Loop over all arguments
\*-------------------------------------------------------------------------*/
  arg = 0;
  while( ++arg<argc ) { 

/*-------------------------------------------------------------------------*\
	Does this switch match ?
\*-------------------------------------------------------------------------*/
      for( argptr=firstarg; argptr; argptr=argptr->next )  {
        char str[128];
        sprintf( str, "--%s", argptr->name ); 
        if( !strcmp(argv[arg],str) || 
            (argptr->sname && !strcmp(argv[arg],argptr->sname)) ) {

/*-------------------------------------------------------------------------*\
	Switch without value? 
\*-------------------------------------------------------------------------*/
          if( !argptr->valname )
            (*(int*)argptr->buffer)++;

/*-------------------------------------------------------------------------*\
	Switch with value? 
\*-------------------------------------------------------------------------*/
          else if( ++arg==argc )
            return -1;
          else
            *(char **)argptr->buffer = argv[arg];
          
/*-------------------------------------------------------------------------*\
	No need to test the rest. 
\*-------------------------------------------------------------------------*/
          break;
        }
      } /* end of:  for( argptr=firstarg; argptr; argptr=argptr->next )  */
      
/*-------------------------------------------------------------------------*\
	Unknown option ?
\*-------------------------------------------------------------------------*/
      if( !argptr && *argv[arg] == '-' ) {
        fprintf( stderr, "%s: Unknown option '%s'!\n",
                 argv[0], argv[arg] );
        return 1;
      }
    
  } /* endof:  while( ++arg<argc ) */

/*-------------------------------------------------------------------------*\
        That's all.
\*-------------------------------------------------------------------------*/
  return 0;
}

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
