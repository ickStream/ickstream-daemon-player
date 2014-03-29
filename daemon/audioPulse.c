/*$*********************************************************************\

Name            : -

Source File     : audioPulse.c

Description     : interface to pulse audio API

Comments        : -

Called by       : audio module 

Calls           : 

Error Messages  : -
  
Date            : 27.03.2013

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

// #undef ICK_DEBUG

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>

#include "ickutils.h"
#include "audio.h"
#include "fifo.h"
#include "player.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
static pa_threaded_mainloop  *pulseMainLoop;           // Pulse audio main loop
static pa_context            *pulseContext;            // Pulse audio context
static pa_context_state_t     pulseContextState;       // Context State

typedef struct {
  pa_stream         *stream;            // PCM output
  pa_stream_state_t  streamState;
  pthread_mutex_t    mutex;             // For coupling with pulse audio thread
  pthread_cond_t     condIsWritable;
} PulseData;

typedef struct {
  size_t     elements;
  char    ***deviceListPtr;
  char    ***descrListPtr;
} DEVLISTROOT;



/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static int    _backendInit( void );
static void   _paContextStateCb( pa_context *ctx, void *userdata );
static int    _backendShutdown( AudioTermMode mode );
static int    _backendGetDeviceList( char ***deviceListPtr, char ***descrListPtr );
static void   _paSinklistCb( pa_context *ctx, const pa_sink_info *list, int eol, void *userdata );
static int    _ifNew( AudioIf *aif );
static int    _ifDelete( AudioIf *aif, AudioTermMode mode ); 
static int    _ifPlay( AudioIf *aif, AudioFormat *format );
static void   _paStreamStateCb( pa_stream *p, void *userdata );
static int    _ifStop( AudioIf *aif, AudioTermMode mode );
static void   _paStreamStopCb( pa_stream *s, int success, void *userdata );
static int    _ifSetPause( AudioIf *aif, bool pause );
static int    _ifSetVolume( AudioIf *aif, double volume, bool muted );

static pa_sample_format_t  _getPulseFormat( const AudioFormat *format );
static void               *_ifThread( void *arg );
static int                 _paLockWaitWritable( const AudioIf *aif, int timeout );
static void                _paUnlockWritable( const AudioIf *aif );
static void                _paStreamWriteCb( pa_stream *p, size_t nbytes, void *userdata );


/*=========================================================================*\
      Return descriptor for this backend 
\*=========================================================================*/
AudioBackend *audioPulseDescriptor( void )
{
  static AudioBackend backend;
  
  // Set name	
  backend.next           = NULL;
  backend.name           = "pulse";
  backend.init           = _backendInit;
  backend.shutdown       = _backendShutdown;
  backend.getDevices     = &_backendGetDeviceList;
  backend.newIf          = &_ifNew; 
  backend.deleteIf       = &_ifDelete;
  backend.play           = &_ifPlay;
  backend.stop           = &_ifStop;
  backend.pause          = &_ifSetPause;
  backend.setVolume      = &_ifSetVolume;

  return &backend;
}


/*=========================================================================*\
      Init the pulse audio module
\*=========================================================================*/
static int _backendInit( void )
{
  pa_mainloop_api    *api;
  int rc;

  DBGMSG( "Pulse Audio: init" );

/*------------------------------------------------------------------------*\
      Already initialized?
\*------------------------------------------------------------------------*/
  if( pulseContext ) {
    logerr( "Pulse Audio: Can only initialize once" );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Create a pulse audio main loop, and context
\*------------------------------------------------------------------------*/
  pulseMainLoop = pa_threaded_mainloop_new();
  if( !pulseMainLoop ) {
    logerr( "Pulse Audio: Can not create main loop.");
    return -1;
  }
  api = pa_threaded_mainloop_get_api( pulseMainLoop );
  if( !api ) {
    logerr( "Pulse Audio: Can not obtain api" );
    pa_threaded_mainloop_free( pulseMainLoop );
    return -1;
  }
  pulseContext = pa_context_new( api, playerGetName() );
  if( !pulseContext ) {
    logerr( "Pulse Audio: Can not create context" );
    pa_threaded_mainloop_free( pulseMainLoop );
    return -1;
  }
  rc = pa_threaded_mainloop_start( pulseMainLoop );
  if( rc<0 ) {
    logerr( "Pulse Audio: Can not start threaded main loop: %s", pa_strerror(rc) );
    pa_threaded_mainloop_free( pulseMainLoop );
    pa_context_unref( pulseContext );
    pulseContext = NULL;
    return -1;
  }

/*------------------------------------------------------------------------*\
    Connect to the default server
\*------------------------------------------------------------------------*/
  pa_threaded_mainloop_lock( pulseMainLoop );
  pulseContextState = pa_context_get_state( pulseContext );
  pa_context_set_state_callback( pulseContext, &_paContextStateCb, &pulseContextState );
  rc = pa_context_connect( pulseContext, NULL, 0, NULL );
  if( rc<0 ) {
    logerr( "Pulse Audio: Can not connect to server: %s", pa_strerror(rc) );
    pa_context_disconnect( pulseContext );
    pa_context_unref( pulseContext );
    pa_threaded_mainloop_unlock( pulseMainLoop );
    pa_threaded_mainloop_stop( pulseMainLoop );
    pa_threaded_mainloop_free( pulseMainLoop );
    pulseContext = NULL;
    return -1;
  }

/*------------------------------------------------------------------------*\
    Wait for connection
\*------------------------------------------------------------------------*/
  while( pulseContextState!=PA_CONTEXT_READY ) {
    DBGMSG( "Pulse Audio: connection state: %d", pulseContextState );

    switch( pulseContextState ) {

      // Transient states: wait for next event
      case PA_CONTEXT_UNCONNECTED:
      case PA_CONTEXT_CONNECTING:
      case PA_CONTEXT_AUTHORIZING:
      case PA_CONTEXT_SETTING_NAME:
      default:
        pa_threaded_mainloop_wait( pulseMainLoop );
        break;

      // Error...
      case PA_CONTEXT_FAILED:
      case PA_CONTEXT_TERMINATED:
        logerr( "Pulse Audio: Could not connect to server: %s",
                pa_strerror(pa_context_errno(pulseContext)));
        pa_context_disconnect( pulseContext );
        pa_context_unref( pulseContext );
        pa_threaded_mainloop_unlock( pulseMainLoop );

        pa_threaded_mainloop_free( pulseMainLoop );
        pulseContext = NULL;
        return -1;

      // Here we are
      case PA_CONTEXT_READY:
        DBGMSG( "Pulse Audio: connected." );
        break;
    }
  }
  pa_threaded_mainloop_unlock( pulseMainLoop );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}

static void _paContextStateCb( pa_context *ctx, void *userdata )
{
  pa_context_state_t *state = userdata;
  *state = pa_context_get_state( ctx );
  DBGMSG( "Pulse Audio Context State Callback: %d", *state );
  pa_threaded_mainloop_signal( pulseMainLoop, 0);
}

/*=========================================================================*\
      Shutdown the pulse audio module
\*=========================================================================*/
static int _backendShutdown( AudioTermMode mode )
{
  DBGMSG( "Pulse Audio: shutdown" );

/*------------------------------------------------------------------------*\
    Need to be connected
\*------------------------------------------------------------------------*/
  if( !pulseMainLoop ) {
    logerr( "Pulse Audio: not connected." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Disconnect from server and kill main loop
\*------------------------------------------------------------------------*/
//  pa_threaded_mainloop_lock( pulseMainLoop );
  pa_context_disconnect( pulseContext );
  pa_context_unref( pulseContext );
  pa_threaded_mainloop_stop( pulseMainLoop );
  pa_threaded_mainloop_free( pulseMainLoop );
  pulseContext = NULL;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Get all backend devices
        descrListPtr might be NULL;
\*=========================================================================*/
static int _backendGetDeviceList( char ***deviceListPtr, char ***descrListPtr )
{
  DEVLISTROOT   devListRoot;
  pa_operation *op;

  DBGMSG( "Pulse Audio: get device list from server" );

/*------------------------------------------------------------------------*\
    Need to be connected
\*------------------------------------------------------------------------*/
  if( !pulseContext ) {
    logerr( "Pulse Audio: not connected." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Init root structure
\*------------------------------------------------------------------------*/
  devListRoot.elements      = 0;
  devListRoot.deviceListPtr = deviceListPtr;
  devListRoot.descrListPtr  = descrListPtr;
  *devListRoot.deviceListPtr = NULL;
  if( devListRoot.descrListPtr )
    *devListRoot.descrListPtr  = NULL;

/*------------------------------------------------------------------------*\
    Initialize the server operation
\*------------------------------------------------------------------------*/
  pa_threaded_mainloop_lock( pulseMainLoop );
  op = pa_context_get_sink_info_list( pulseContext, &_paSinklistCb, &devListRoot );
  if( !op ) {
    logerr( "Pulse Audio: Could not request sink list from server: %s",
            pa_strerror( pa_context_errno(pulseContext)) );
    pa_threaded_mainloop_unlock( pulseMainLoop );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Wait for server operation to complete
\*------------------------------------------------------------------------*/
  while( pa_operation_get_state(op)==PA_OPERATION_RUNNING )
    pa_threaded_mainloop_wait( pulseMainLoop );
  if( pa_operation_get_state(op)==PA_OPERATION_CANCELLED )
    logerr( "Pulse Audio: Sink list request was canceled: %s",
            pa_strerror( pa_context_errno(pulseContext)) );
  pa_operation_unref( op );

/*------------------------------------------------------------------------*\
    Unlock mutex and return number of devices found with name
\*------------------------------------------------------------------------*/
  pa_threaded_mainloop_unlock( pulseMainLoop );
  return devListRoot.elements;
}


/*=========================================================================*\
      Callback for device discovery
\*=========================================================================*/
static void _paSinklistCb( pa_context *ctx, const pa_sink_info *list, int eol, void *userdata )
{
  DEVLISTROOT *deviceList = userdata;

  DBGMSG( "Pulse Audio sinklist callback (%p): list=%p, eol=%d", ctx, list, eol );

/*------------------------------------------------------------------------*\
    End of list?
\*------------------------------------------------------------------------*/
  if( eol>0 ) {
    pa_threaded_mainloop_signal( pulseMainLoop, 0);
    return;
  }

/*------------------------------------------------------------------------*\
    Allocate/extend NULL-terminated list of pointers
\*------------------------------------------------------------------------*/
  if( !deviceList->elements ) {
    deviceList->elements = 1;
    *deviceList->deviceListPtr = calloc( 2, sizeof(char*) );
    if( deviceList->descrListPtr )
      *deviceList->descrListPtr = calloc( 2, sizeof(char*) );
  }
  else {
    deviceList->elements++;
    *deviceList->deviceListPtr = realloc( *deviceList->deviceListPtr, (deviceList->elements+1)*sizeof(char*) );
    (*deviceList->deviceListPtr)[deviceList->elements] = NULL;
    if( deviceList->descrListPtr ) {
      *deviceList->descrListPtr = realloc( *deviceList->descrListPtr, (deviceList->elements+1)*sizeof(char*) );
      (*deviceList->descrListPtr)[deviceList->elements]   = NULL;
    }
  }

/*------------------------------------------------------------------------*\
    Append list element to list of pointers
\*------------------------------------------------------------------------*/
  (*deviceList->deviceListPtr)[deviceList->elements-1] = strdup( list->name );
  if( deviceList->descrListPtr )
    (*deviceList->descrListPtr)[deviceList->elements-1] = strdup( list->description );

}


/*=========================================================================*\
    Open device and start thread
\*=========================================================================*/
static int _ifNew( AudioIf *aif )
{
  PulseData *ifData;

  DBGMSG( "Pulse Audio (%s): new interface", aif->devName );

/*------------------------------------------------------------------------*\
    Need to be connected
\*------------------------------------------------------------------------*/
  if( !pulseContext ) {
    logerr( "Pulse Audio: not connected." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Create auxiliary data
\*------------------------------------------------------------------------*/
  ifData = calloc( 1, sizeof(PulseData) );
  if( !ifData ) {
    logerr( "Out of memory." );
    return -1;
  }
  aif->ifData = ifData;

/*------------------------------------------------------------------------*\
    We support pausing and value control
\*------------------------------------------------------------------------*/
  aif->canPause = true;
  aif->hasVolume = true;

/*------------------------------------------------------------------------*\
    Init mutex and conditions
\*------------------------------------------------------------------------*/
  ickMutexInit( &ifData->mutex );
  pthread_cond_init( &ifData->condIsWritable, NULL );

/*------------------------------------------------------------------------*\
    Set state, that's it
\*------------------------------------------------------------------------*/
  aif->state = AudioIfInitialized;
  return 0;
}


/*=========================================================================*\
    Close device
\*=========================================================================*/
static int _ifDelete( AudioIf *aif, AudioTermMode mode )
{
  PulseData *ifData = (PulseData*) aif->ifData;
  int rc = 0;

  DBGMSG( "Pulse Audio (%s): deleting interface", aif->devName );

/*------------------------------------------------------------------------*\
    Stop current playback
\*------------------------------------------------------------------------*/
  if( aif->state==AudioIfRunning ) {
    if( _ifStop(aif,mode) ) {
      logerr( "_ifDelete: Could not stop running playback." );
      rc = -1;
    }
  }

/*------------------------------------------------------------------------*\
    Delete mutex and conditions
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &ifData->mutex );
  pthread_cond_destroy( &ifData->condIsWritable );

/*------------------------------------------------------------------------*\
    Get rid of auxiliary data
\*------------------------------------------------------------------------*/
  Sfree( aif->ifData );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return rc;
}


/*=========================================================================*\
    Attach a queue to device and start playing
\*=========================================================================*/
static int _ifPlay( AudioIf *aif, AudioFormat *format )
{
  PulseData         *ifData = aif->ifData;
  pa_sample_spec     sampleSpec;
  pa_channel_map     chanMap;
  pa_cvolume         paVol;
  pa_stream_flags_t  cFlags;
  int                rc;

  DBGMSG( "Pulse Audio (%p,%s): start playback", aif, aif->devName );

/*------------------------------------------------------------------------*\
    Stop current playback
\*------------------------------------------------------------------------*/
  if( aif->state==AudioIfRunning ) {
    if( _ifStop(aif,AudioDrop) ) {
      logerr( "Pulse Audio (%p,%s): Could not stop running playback.",
              aif, aif->devName );
      return -1;
    }
  }

/*------------------------------------------------------------------------*\
    Check state
\*------------------------------------------------------------------------*/
  if( aif->state!=AudioIfInitialized ) {
    logerr( "Pulse Audio (%p,%s): Cannot start playback, wrong state: %d",
            aif, aif->devName, aif->state );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Set up pulse sample specification and get frame size
\*------------------------------------------------------------------------*/
  sampleSpec.format   = _getPulseFormat( format );
  sampleSpec.rate     = format->sampleRate;
  sampleSpec.channels = format->channels;
  if( !pa_sample_spec_valid(&sampleSpec) ) {
    logerr( "Pulse Audio (%p,%s): No sample specification for format: %s",
            aif, aif->devName, audioFormatStr(NULL,format) );
    return -1;
  }
  aif->framesize = pa_frame_size( &sampleSpec );

/*------------------------------------------------------------------------*\
    Set up default channel map
\*------------------------------------------------------------------------*/
  if( !pa_channel_map_init_auto(&chanMap,format->channels,PA_CHANNEL_MAP_DEFAULT) ) {
    logerr( "Pulse Audio (%p,%s): No channel map for format: %s",
                aif, aif->devName, audioFormatStr(NULL,format) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Set up volume
\*------------------------------------------------------------------------*/
  pa_cvolume_init( &paVol );
  pa_cvolume_set( &paVol, format->channels, pa_sw_volume_from_linear(aif->volume) );

/*------------------------------------------------------------------------*\
    Try to create stream client side
\*------------------------------------------------------------------------*/
  pa_threaded_mainloop_lock( pulseMainLoop );
  ifData->stream = pa_stream_new( pulseContext, playerGetName(), &sampleSpec, &chanMap );
  if( !ifData->stream ) {
    logerr( "Pulse Audio (%p): Could not create stream (%s): %s",
            aif, audioFormatStr(NULL,format),
            pa_strerror(pa_context_errno(pulseContext)) );
    pa_threaded_mainloop_unlock( pulseMainLoop );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Connect
\*------------------------------------------------------------------------*/
  ifData->streamState = pa_stream_get_state( ifData->stream );
  pa_stream_set_state_callback( ifData->stream, &_paStreamStateCb, &ifData->streamState );
  cFlags = aif->muted?PA_STREAM_START_MUTED:PA_STREAM_START_UNMUTED;
  rc = pa_stream_connect_playback( ifData->stream, aif->devName, NULL, cFlags,
                                   &paVol, NULL );
  if( rc<0 ) {
    logerr( "Pulse Audio (%p,%s): Can not connect: %s",
             aif, aif->devName, pa_strerror(rc) );
    pa_stream_disconnect( ifData->stream );
    ifData->stream = NULL;
    pa_threaded_mainloop_unlock( pulseMainLoop );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Wait for connection
\*------------------------------------------------------------------------*/
  while( ifData->streamState!=PA_STREAM_READY ) {
    DBGMSG( "Pulse Audio (%p,%s): stream state: %d", aif, aif->devName, ifData->streamState );

    switch( ifData->streamState ) {

      // Transient states: wait for next event
      case PA_STREAM_UNCONNECTED:
      case PA_STREAM_CREATING:
      default:
        pa_threaded_mainloop_wait( pulseMainLoop );
        break;

      // Error...
      case PA_STREAM_FAILED:
      case PA_STREAM_TERMINATED:
        logerr( "Pulse Audio (%p,%s): Could not connect stream: %s",
            aif, aif->devName, pa_strerror(pa_context_errno(pulseContext)));
        pa_stream_disconnect( ifData->stream );
        ifData->stream = NULL;
        pa_threaded_mainloop_unlock( pulseMainLoop );
        return -1;

      // Here we are
      case PA_STREAM_READY:
        DBGMSG( "Pulse Audio(%p,%s): stream connected.", aif, aif->devName );
        break;
    }
  }

/*------------------------------------------------------------------------*\
    Set callbacks and unlock mainloop
\*------------------------------------------------------------------------*/
  pa_stream_set_write_callback( ifData->stream, &_paStreamWriteCb, aif );
  pa_threaded_mainloop_unlock( pulseMainLoop );

  DBGMSG( "Pulse Audio (%p,%s): stream is corked: %d", aif, aif->devName, pa_stream_is_corked(ifData->stream) );

/*------------------------------------------------------------------------*\
    Fire up working thread
\*------------------------------------------------------------------------*/
  rc = pthread_create( &aif->thread, NULL, _ifThread, aif );
  if( rc ) {
    logerr( "Pulse Audio (%p,%s): Unable to start audio backend thread: %s",
            aif, aif->devName, strerror(rc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}

static void _paStreamStateCb( pa_stream *p, void *userdata )
{
  pa_stream_state_t  *state = userdata;
  *state=pa_stream_get_state( p );
  DBGMSG( "Pulse Audio Stream State Callback: %d", *state );
  pa_threaded_mainloop_signal( pulseMainLoop, 0);
}


/*=========================================================================*\
    Stop playback
\*=========================================================================*/
static int _ifStop( AudioIf *aif, AudioTermMode mode )
{
  PulseData    *ifData = aif->ifData;
  pa_operation *op;
  int           rc;

  DBGMSG( "Pulse Audio (%p,%s): stop (mode %d)", aif, aif->devName, mode );

/*------------------------------------------------------------------------*\
    No stream
\*------------------------------------------------------------------------*/
  if( !ifData->stream ) {
    DBGMSG( "Pulse Audio (%p,%s): stop (mode %d) called without stream.",
             aif, aif->devName, mode );
    aif->state = AudioIfInitialized;
    return 0;
  }

/*------------------------------------------------------------------------*\
    Clear callbacks
\*------------------------------------------------------------------------*/
  pa_threaded_mainloop_lock( pulseMainLoop );
  pa_stream_set_state_callback( ifData->stream, NULL, NULL );
  pa_stream_set_write_callback( ifData->stream, NULL, NULL );
  pa_threaded_mainloop_unlock( pulseMainLoop );

/*------------------------------------------------------------------------*\
    Stop thread and optionally wait for termination
\*------------------------------------------------------------------------*/
  if( aif->state==AudioIfRunning )
    aif->state = AudioIfTerminating;
  if( aif->state>AudioIfInitialized && mode!=AudioForce )
     pthread_join( aif->thread, NULL );

/*------------------------------------------------------------------------*\
    How to deal with data in buffer?
\*------------------------------------------------------------------------*/
  pa_threaded_mainloop_lock( pulseMainLoop );
  if( mode==AudioDrain )
    op = pa_stream_drain( ifData->stream, &_paStreamStopCb, NULL );
  else
    op = pa_stream_flush( ifData->stream, &_paStreamStopCb, NULL );
  if( !op ) {
    logerr( "Pulse Audio (%p,%s): Could not drain/flush stream: %s",
            aif, aif->devName, pa_strerror(pa_context_errno(pulseContext)) );
    pa_threaded_mainloop_unlock( pulseMainLoop );
    return -1;
  }

  while( pa_operation_get_state(op)==PA_OPERATION_RUNNING )
     pa_threaded_mainloop_wait( pulseMainLoop );
  pa_operation_unref( op );
  pa_threaded_mainloop_unlock( pulseMainLoop );

/*------------------------------------------------------------------------*\
    Disconnect stream
\*------------------------------------------------------------------------*/
  pa_threaded_mainloop_lock( pulseMainLoop );
  rc = pa_stream_disconnect( ifData->stream );
  pa_threaded_mainloop_unlock( pulseMainLoop );
  ifData->stream = NULL;
  if( rc<0 ) {
    logerr( "Pulse Audio (%p,%s): Could not disconnect: %s",
             aif, aif->devName, pa_strerror(rc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Error in thread termination?
\*------------------------------------------------------------------------*/
  if( mode!=AudioForce && aif->state!=AudioIfTerminatedOk )
    return -1;

/*------------------------------------------------------------------------*\
    Reset state to initialized, that's it
\*------------------------------------------------------------------------*/
  aif->state = AudioIfInitialized;
  return 0;
}

static void _paStreamStopCb( pa_stream *s, int success, void *userdata )
{
  DBGMSG( "Pulse Audio Stream Drain/Flush Callback: %d", success );
  pa_threaded_mainloop_signal( pulseMainLoop, 0);
}

/*=========================================================================*\
    Set pause mode
\*=========================================================================*/
static int _ifSetPause( AudioIf *aif, bool pause )
{
  PulseData    *ifData = aif->ifData;
  pa_operation *op;

  DBGMSG( "Pulse Audio (%p,%s): pause %s", aif, aif->devName, pause?"On":"Off" );

/*------------------------------------------------------------------------*\
    Request pause mode
\*------------------------------------------------------------------------*/
  pa_threaded_mainloop_lock( pulseMainLoop );
  op = pa_stream_cork( ifData->stream, pause?1:0, NULL, NULL );
  pa_threaded_mainloop_unlock( pulseMainLoop );
  if( op )
    pa_operation_unref( op );
  else {
    logerr( "Pulse Audio (%p,%s): Could not cork stream: %s",
            aif, aif->devName, pa_strerror(pa_context_errno(pulseContext)) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
    Set volume
\*=========================================================================*/
static int _ifSetVolume( AudioIf *aif, double volume, bool muted )
{
  PulseData    *ifData = aif->ifData;
  uint32_t      idx;
  pa_operation *op;
  int           rc = 0;

  DBGMSG( "Pulse Audio (%p,%s): set volume to %.2lf%% %s", aif, aif->devName,
           volume*100, muted?"(muted)":"(unmuted)" );

/*------------------------------------------------------------------------*\
    Stream not yet up: only store target values
\*------------------------------------------------------------------------*/
  if( !ifData->stream ) {
    aif->volume = volume;
    aif->muted  = muted;
    return 0;
  }

/*------------------------------------------------------------------------*\
    Get stream index
\*------------------------------------------------------------------------*/
  idx = pa_stream_get_index( ifData->stream );

/*------------------------------------------------------------------------*\
    Need to set volume
\*------------------------------------------------------------------------*/
  if( volume!=aif->volume ) {
    pa_cvolume paVol;
    pa_cvolume_init( &paVol );
    pa_cvolume_set( &paVol, aif->format.channels, pa_sw_volume_from_linear(volume) );

    // Request sink volume
    pa_threaded_mainloop_lock( pulseMainLoop );
    op = pa_context_set_sink_input_volume( pulseContext, idx, &paVol, NULL, NULL);
    pa_threaded_mainloop_unlock( pulseMainLoop );
    if( op ) {
      aif->volume = volume;
      pa_operation_unref( op );
    }
    else {
      logerr( "Pulse Audio (%p,%s): Could not request sink volume: %s",
              aif, aif->devName, pa_strerror(pa_context_errno(pulseContext)) );
      rc = -1;
    }
  }

/*------------------------------------------------------------------------*\
    Need to set muting
\*------------------------------------------------------------------------*/
  if( muted!=aif->muted ) {
    pa_threaded_mainloop_lock( pulseMainLoop );
    op = pa_context_set_sink_input_mute( pulseContext, idx, muted?1:0, NULL, NULL);
    pa_threaded_mainloop_unlock( pulseMainLoop );
    if( op ) {
      aif->muted  = muted;
      pa_operation_unref( op );
    }
    else  {
      logerr( "Pulse Audio (%p,%s): Could not request sink muting: %s",
              aif, aif->devName, pa_strerror(pa_context_errno(pulseContext)) );
      rc = -1;
    }
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return rc;
}


/*=========================================================================*\
       Translate audio format to Pulse Audio standard
\*=========================================================================*/
static pa_sample_format_t _getPulseFormat( const AudioFormat *format )
{
  DBGMSG( "_getPulseFormat: %s", audioFormatStr(NULL,format) );
  bool le = true;

/*------------------------------------------------------------------------*\
    Relevant float formats
\*------------------------------------------------------------------------*/
  if( format->isFloat ) {
    if( format->bitWidth==32 )
      return le ? PA_SAMPLE_FLOAT32LE : PA_SAMPLE_FLOAT32BE;
    return PA_SAMPLE_INVALID ;
  }

/*------------------------------------------------------------------------*\
    Relevant signed formats
\*------------------------------------------------------------------------*/
  if( format->isSigned ) {
    if( format->bitWidth==16 )
      return le ? PA_SAMPLE_S16LE : PA_SAMPLE_S16BE;
    if( format->bitWidth==24 )
      return le ? PA_SAMPLE_S24LE : PA_SAMPLE_S24BE;
    if( format->bitWidth==32 )
      return le ? PA_SAMPLE_S32LE : PA_SAMPLE_S32BE;
    return PA_SAMPLE_INVALID ;
  }

/*------------------------------------------------------------------------*\
    Relevant unsigned formats
\*------------------------------------------------------------------------*/
  if( format->bitWidth==8 )
    return PA_SAMPLE_U8;

/*------------------------------------------------------------------------*\
    Not known
\*------------------------------------------------------------------------*/
  return PA_SAMPLE_INVALID ;
}


/*=========================================================================*\
       An audio backend thread 
\*=========================================================================*/
static void *_ifThread( void *arg )
{
  AudioIf    *aif    = (AudioIf*)arg;
  PulseData  *ifData = aif->ifData;

  DBGMSG( "Pulse Audio thread (%s): starting.", aif->devName );
  PTHREADSETNAME( aif->backend->name );

/*------------------------------------------------------------------------*\
    Thread main loop  
\*------------------------------------------------------------------------*/
  aif->state = AudioIfRunning;
  pthread_cond_signal( &aif->condIsReady );
  while( aif->state==AudioIfRunning ) {
    int    rc;
    size_t bytesWritable, bytesReadable, nBytes;

    // Perform pulse audio main loop cycle
    rc = _paLockWaitWritable( aif, 250 );
    if( rc==ETIMEDOUT ) {
      DBGMSG( "Pulse Audio thread (%s): timeout while waiting for sink space.", aif->devName );
      continue;
    }
    if( rc ) {
      logerr( "Pulse Audio thread (%s): wait for sink space error, terminating: %s",
               aif->devName, strerror(rc) );
      aif->state = AudioIfTerminatedError;
      break;
    }
    _paUnlockWritable( aif );

    // Wait for data in fifo and lock it on success
    rc = fifoLockWaitReadable( aif->fifoIn, 250 );
    if( rc==ETIMEDOUT ) {
      DBGMSG( "Pulse Audio thread (%s): timeout while waiting for fifo data.", aif->devName );
      continue;
    }
    if( rc ) {
      logerr( "Pulse Audio thread (%s): wait for fifo error, terminating: %s",
               aif->devName, strerror(rc) );
      aif->state = AudioIfTerminatedError;
      break;
    }

    // How much data can be transfered?
    bytesWritable = pa_stream_writable_size( ifData->stream );
    bytesReadable = fifoGetSize( aif->fifoIn, FifoNextReadable );
    nBytes        = MIN( bytesWritable, bytesReadable );
    DBGMSG( "Pulse Audio thread (%s): consuming %ld bytes", aif->devName, (long)nBytes );
    if( !bytesWritable ) {
      logwarn( "Pulse Audio thread (%s): zero bytes to transfer", aif->devName );
      fifoUnlockAfterRead( aif->fifoIn, nBytes );
      continue;
    }

    // Transfer the data
    pa_threaded_mainloop_lock( pulseMainLoop );
    rc = pa_stream_write( ifData->stream, fifoGetReadPtr(aif->fifoIn), nBytes, NULL, 0, PA_SEEK_RELATIVE );
    pa_threaded_mainloop_unlock( pulseMainLoop );
    if( rc<0 ) {
      logerr( "Pulse Audio thread (%s): could not write %ld bytes to stream: %s",
               aif->devName, (long)nBytes, pa_strerror(rc) );
      aif->state = AudioIfTerminatedError;
    }

    // Check out accepted data and unlock fifo
    fifoUnlockAfterRead( aif->fifoIn, nBytes );

  }  // End of: Thread main loop
   
/*------------------------------------------------------------------------*\
    Fulfilled external termination request without error...  
\*------------------------------------------------------------------------*/
  DBGMSG( "Pulse Audio thread: terminating due to state %d", aif->state );
  if( aif->state==AudioIfTerminating )
    aif->state = AudioIfTerminatedOk;

/*------------------------------------------------------------------------*\
    That's it ...  
\*------------------------------------------------------------------------*/
  return NULL;
}


/*=========================================================================*\
       Wait for a pulse stream to be writable
\*=========================================================================*/
static int _paLockWaitWritable( const AudioIf *aif, int timeout )
{
  struct timeval  now;
  struct timespec abstime;
  int             err = 0;
  PulseData      *ifData = aif->ifData;

  DBGMSG( "_paLockWaitWritable (%s): waiting for writable: timeout %dms",
          aif->devName, timeout );

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   pthread_mutex_lock( &ifData->mutex );

/*------------------------------------------------------------------------*\
    Get absolute timestamp for timeout
\*------------------------------------------------------------------------*/
  if( timeout>0 ) {
    gettimeofday( &now, NULL );
    abstime.tv_sec  = now.tv_sec + timeout/1000;
    abstime.tv_nsec = now.tv_usec*1000UL +(timeout%1000)*1000UL*1000UL;
    if( abstime.tv_nsec>1000UL*1000UL*1000UL ) {
      abstime.tv_nsec -= 1000UL*1000UL*1000UL;
      abstime.tv_sec++;
    }
  }

/*------------------------------------------------------------------------*\
    Loop while condition is not met (cope with "spurious wakeups")
\*------------------------------------------------------------------------*/
  while( !pa_stream_writable_size(ifData->stream) ) {

    // wait for condition
    err = timeout>0 ? pthread_cond_timedwait( &ifData->condIsWritable, &ifData->mutex, &abstime )
                    : pthread_cond_wait( &ifData->condIsWritable, &ifData->mutex );

    // Break on errors
    if( err )
      break;
  }

/*------------------------------------------------------------------------*\
    In case of error: unlock mutex
\*------------------------------------------------------------------------*/
  if( err )
    pthread_mutex_unlock( &ifData->mutex );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  DBGMSG( "_paLockWaitWritable (%p): waiting for writable: %s",
          ifData->stream, err?strerror(err):"Locked" );
  return err;
}


/*=========================================================================*\
       Unlock a pulse stream that was writable
\*=========================================================================*/
static void _paUnlockWritable( const AudioIf *aif )
{
  PulseData *ifData = aif->ifData;
  DBGMSG( "_paUnlockWritable (%s): unlocked.", aif->devName );
  pthread_mutex_unlock( &ifData->mutex );
}


/*=========================================================================*\
       Pulse callback: stream has become writable
\*=========================================================================*/
static void _paStreamWriteCb( pa_stream *p, size_t nbytes, void *userdata )
{
  AudioIf    *aif    = (AudioIf*)userdata;
  PulseData  *ifData = aif->ifData;

  // this should not happen: artifacts from previous stream
  if( p!=ifData->stream ) {
    logerr( "Pulse Audio callback (%s): stream %p does not correspond with user data %p.",
        aif->devName, p, ifData->stream );
    pa_threaded_mainloop_signal( pulseMainLoop, 0);
    return;
  }

  DBGMSG( "Pulse Audio callback (%s): sink has space (%ld bytes)",
           aif->devName, (long) nbytes );

  // Set condition to signal our thread that there is space available
  if( nbytes>0 )
    pthread_cond_signal( &ifData->condIsWritable );

  // Signal pulse audio thread that we're done, this will unlock the main loop
  pa_threaded_mainloop_signal( pulseMainLoop, 0 );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/

