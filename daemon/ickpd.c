/*$*********************************************************************\

Name            : -

Source File     : ickpd.c

Description     : Main module for the ickstream player daemon 

Syntax          : standard unix command execution

Parameters      : various, use --help

Returncode      : non-zero on error

Comments        : -

Called by       : OS wraping code

Calls           : ickstream functions

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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <ickDiscovery.h>

#include "ickpd.h"
#include "utils.h"
#include "config.h"
#include "persist.h"
#include "hmi.h"
#include "ickDevice.h"
#include "ickMessage.h"
#include "audio.h"
#include "player.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static volatile int stop_signal;
static void sigHandler( int sig, siginfo_t *siginfo, void *context );


/*========================================================================*\
    Main function
\*========================================================================*/
int main( int argc, char *argv[] )
{
  int         help_flag    = 0;
  int         daemon_flag  = 0;
  const char *cfg_fname    = NULL;
  const char *pers_fname   = ".ickpd_persist";
  const char *pid_fname    = "/var/run/ickpd.pid";
  const char *player_uuid  = NULL;
  const char *player_name  = NULL;
  const char *if_name      = NULL;
  const char *verb_arg     = "4";
  const char *vers_flag    = NULL;
  const char *adev_name    = NULL;
  const char *adev_flag    = NULL;
  char       *eptr;
  int         cpid;
  int         fd;
  FILE       *fp;
  int         retval = 0;
  
/*-------------------------------------------------------------------------*\
	Set up commandline switches (leading * flags availability in config file)
\*-------------------------------------------------------------------------*/
  addarg( "help",     "-?",  &help_flag,   NULL,       "Show this help message and quit" );
  addarg( "devices",  "-al", &adev_flag,   NULL,       "List audio devices and quit" );
  addarg( "config",   "-c",  &cfg_fname,   "filename", "Set name of configuration file" );
  addarg( "*pers",    "-p",  &pers_fname,  "filename", "Set name of persistency file" );
#ifdef ICK_DEBUG
  addarg( "*uuid",    "-u",  &player_uuid, "uuid",     "Init/change UUID for this player" );
#endif
  addarg( "*name",    "-n",  &player_name, "name",     "Init/change Name for this player" );
  addarg( "*idev",    "-i",  &if_name,     "interface","Init/change network interface" );
  addarg( "*adevice", "-ad", &adev_name,   "name",     "Init/change audio device name" );
  addarg( "daemon",   "-d",  &daemon_flag, NULL,       "Start in daemon mode" );
  addarg( "*pfile",   "-pid",&pid_fname,   "filename", "Filename to store process ID" );
  addarg( "*verbose", "-v",  &verb_arg,    "level",    "Set logging level (0-7)" );
  addarg( "Version",  "-V",  &vers_flag,   NULL,       "Show programm version" );

/*-------------------------------------------------------------------------*\
	Parse the arguments
\*-------------------------------------------------------------------------*/
  if( getargs(argc,argv) ) {
    usage( argv[0], 1 );
    return 1;
  }

/*-------------------------------------------------------------------------*\
    Show version and/or help
\*-------------------------------------------------------------------------*/
  if( vers_flag ) {
    printf( "%s version %g (using ickstream lib version %s)\n", 
      argv[0], ICKPD_VERSION, "??" );
    printf( "<c> 2013 by //MAF, ickStream GmbH\n\n" );
  }
  if( help_flag ) {
    usage( argv[0], 0 );
    return 0;
  }
  
/*------------------------------------------------------------------------*\
    Set verbosity level 
\*------------------------------------------------------------------------*/  
  if( verb_arg ) {
    streamloglevel = (int) strtol( verb_arg, &eptr, 10 );
    while( isspace(*eptr) )
      eptr++;
    if( *eptr || streamloglevel<0 || streamloglevel>7 ) {
      fprintf( stderr, "Bad verbosity level: '%s'\n", verb_arg );
      return 1;
    }
  }
#ifndef ICK_DEBUG
  if( streamloglevel>=LOG_DEBUG ) {
     fprintf( stderr, "%s: binary not compiled for debugging, loglevel %d might be too high!\n", 
                      argv[0], streamloglevel );
  }
#endif

/*-------------------------------------------------------------------------*\
    List available audio devices and exit
\*-------------------------------------------------------------------------*/
  if( adev_flag ) {
    const AudioBackend *backend;
    int                 tot = 0;
    int                 devWidth = 0;
    if( audioInit(NULL) ) {
      printf( "Could not init audio module for testing devices.\n" );
      return -1;
    }
    backend = audioBackendsRoot( );
    while( backend ) {
      char **devList;
      int    i, n;
      n = audioGetDeviceList( backend, &devList, NULL );
      if( n<0 )
        return -1;
      for( i=0; i<n; i++ ) {
        int len = strlen( backend->name )+1;
        if( devList[i] )
          len += strlen( devList[i] );
        if( len>devWidth )
          devWidth = len;
      }
      backend = backend->next;
    }
    backend = audioBackendsRoot( );
    while( backend ) {
      char **devList, **dscrList, *res;
      int    i, n;
      n = audioGetDeviceList( backend, &devList, &dscrList );       
      if( n<0 )
        return -1;
      for( i=0; i<n; i++ ) {
        char *descrLine = strtok( dscrList[i], "\n" );
        if( !devList[i] ) 
          res = strdup( backend->name );
        else {
          res = malloc( strlen(backend->name)+2+strlen(devList[i]) );
          sprintf( res, "%s:%s", backend->name, devList[i] );
        }
        printf( "%-*s - %s\n", devWidth, res, descrLine );
        Sfree( res );
        while( (descrLine=strtok(NULL,"\n")) )
          printf( "%*s%s\n", devWidth+3, "", descrLine );
      }
      audioFreeStringList( devList );
      audioFreeStringList( dscrList );
      tot += n;
      backend = backend->next;
    }
    if( !tot )
      printf( "No audio devices found.\n" );
    return 0;
  }

/*------------------------------------------------------------------------*\
    Read configuration 
\*------------------------------------------------------------------------*/  
  if( cfg_fname && readconfig(cfg_fname) )
    return -1;

/*------------------------------------------------------------------------*\
    Set persistence filename 
\*------------------------------------------------------------------------*/  
  if( persistSetFilename(pers_fname) )
    return -1;
      
/*------------------------------------------------------------------------*\
    Interface changed or unavailabale ?
\*------------------------------------------------------------------------*/
  if( if_name )
    playerSetInterface( if_name );
  if_name = playerGetInterface();
  if( !if_name ) {
    if_name = "eth0";
    logwarn( "Need interface name, using \"%s\" as default...",
                         if_name );
    playerSetInterface( if_name );
  }
  loginfo( "Using interface: \"%s\"", if_name );

/*------------------------------------------------------------------------*\
    Uuid changed or unavilabale ?
\*------------------------------------------------------------------------*/  
#ifdef ICK_DEBUG
  if( player_uuid )                  // command line or config file argument
    playerSetUUID( player_uuid );
#endif
  player_uuid = playerGetUUID();
  if( !player_uuid ) {
     fprintf( stderr, "NO player UUID!\n" );
     return 1;
  }
  loginfo( "Using uuid     : \"%s\"", player_uuid );
  
/*------------------------------------------------------------------------*\
    Player name changed or unavailabale ?
\*------------------------------------------------------------------------*/  
  if( player_name )
    playerSetName( player_name, false );
  player_name = playerGetName();  
  if( !player_name ) {
    char buf[128], hname[100];
    if( gethostname(hname,sizeof(hname)) )
      strcpy( buf, "ickpd" );
    else 
      sprintf( buf, "ickpd @ %s", hname );
    logwarn( "Need player name, using \"%s\" as default...",
                         buf );
    playerSetName( buf, false );
    player_name = playerGetName();
  }
  loginfo( "Using name     : \"%s\"", player_name );  

/*------------------------------------------------------------------------*\
    Player device changed or unavailable ?
\*------------------------------------------------------------------------*/  
  if( adev_name )
    playerSetAudioDevice( adev_name );
  adev_name = playerGetAudioDevice();  
  if( !adev_name ) {
    adev_name = "null";
    logwarn( "Need audio device name, using \"%s\" as default...",
                         adev_name );
    playerSetAudioDevice( adev_name );
  }
  loginfo( "Using audio dev: \"%s\"", adev_name );

/*------------------------------------------------------------------------*\
    Init audio module: check for interface
\*------------------------------------------------------------------------*/
  if( audioInit(adev_name) )
    return -1;

/*------------------------------------------------------------------------*\
    Add default audio format (fixme: this should part of the player state)
\*------------------------------------------------------------------------*/
  AudioFormat format;
  audioStrFormat( &format, "2x44100x16S" );
  playerAddDefaultAudioFormat( &format );

/*------------------------------------------------------------------------*\
    Init HMI
\*------------------------------------------------------------------------*/
  if( !daemon_flag && hmiInit() )
    return -1;

/*------------------------------------------------------------------------*\
    Goto background
\*------------------------------------------------------------------------*/
  if( daemon_flag ) {
    if( streamloglevel>=LOG_DEBUG ) 
      fprintf( stderr, "%s: loglevel %d might be too high for syslog (daemon mode). Run in foreground to get all messages!\n",
                       argv[0], streamloglevel );
    sysloglevel = streamloglevel;
    streamloglevel = 0;
    cpid = fork();
    if( cpid==-1 ) {
      perror( "Could not fork" );
      return -2;
    }
    if( cpid )   /* Parent process exits ... */
      return 0;
    if( setsid()==-1 ) {
      perror( "Could not create new session" );
      return -2; 	
    }
    fd = open( "/dev/null", O_RDWR, 0 );
    if( fd!=-1) {
      dup2(fd, STDIN_FILENO);
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
    }
  } /* end of: if( daemon_flag )*/

/*------------------------------------------------------------------------*\
    OK, from here on we catch some terminating signals and ignore others
\*------------------------------------------------------------------------*/  
  struct sigaction act;
  memset( &act, 0, sizeof(act) );
  act.sa_sigaction = &sigHandler;
  act.sa_flags     = SA_SIGINFO;
  sigaction( SIGINT, &act, NULL );
  sigaction( SIGTERM, &act, NULL );

/*------------------------------------------------------------------------*\
    Setup PID file, ignore errors...
\*------------------------------------------------------------------------*/
  fp = fopen( pid_fname, "w" );
  if( fp ) {
    fprintf( fp, "%d\n", getpid() );
    fclose( fp );
  }

/*------------------------------------------------------------------------*\
    Initalize ickstream environment...
\*------------------------------------------------------------------------*/
  ickDeviceRegisterMessageCallback( &ickMessage );
  ickDeviceRegisterDeviceCallback( &ickDevice );
  ickInitDiscovery( player_uuid, if_name, NULL );
  ickDiscoverySetupConfigurationData( player_name, NULL );
  ickDiscoveryAddService( ICKDEVICE_PLAYER );

/*------------------------------------------------------------------------*\
    Init player and announce state
\*------------------------------------------------------------------------*/
  playerInit();
  ickMessageNotifyPlaylist( NULL );
  ickMessageNotifyPlayerState( NULL );
  
/*------------------------------------------------------------------------*\
    Mainloop:
\*------------------------------------------------------------------------*/  
 while( !stop_signal ) {

/*------------------------------------------------------------------------*\
   Just sleep... 
\*------------------------------------------------------------------------*/  
   sleep( 1000 );

 } /* end of: while( !stopflag ) */
 if( stop_signal )
   loginfo( "Exiting due to signal %d ...", stop_signal );

/*------------------------------------------------------------------------*\
    Stop player and close ickstream environment...
\*------------------------------------------------------------------------*/
  playerSetState( PlayerStateStop, false );
  ickEndDiscovery( 1 );

/*------------------------------------------------------------------------*\
    ... and other modules.
\*------------------------------------------------------------------------*/
  hmiShutdown();
  playerShutdown();
  audioShutdown( AudioDrain );
  persistShutdown();

/*------------------------------------------------------------------------*\
    Cleanup PID file
\*------------------------------------------------------------------------*/
  unlink( pid_fname );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/  
  return retval;
}


/*=========================================================================*\
        Handle signals
\*=========================================================================*/
static void sigHandler( int sig, siginfo_t *siginfo, void *context )
{

/*------------------------------------------------------------------------*\
    What sort of signal is to be processed ?
\*------------------------------------------------------------------------*/  
  switch( sig ) {

/*------------------------------------------------------------------------*\
    A normal termination request
\*------------------------------------------------------------------------*/  
    case SIGINT:
    case SIGTERM:
      stop_signal = sig;
      break;

/*------------------------------------------------------------------------*\
    Ignore broken pipes
\*------------------------------------------------------------------------*/
    case SIGPIPE:
      break;
  }
  
/*------------------------------------------------------------------------*\
    That's it.
\*------------------------------------------------------------------------*/  
} 


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/


