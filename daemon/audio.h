/*$*********************************************************************\

Name            : -

Source File     : audio.h

Description     : Main include file for audio.c 

Comments        : -

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


#ifndef __AUDIO_H
#define __AUDIO_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <stdbool.h>
#include "fifo.h"

/*=========================================================================*\
       Constants 
\*=========================================================================*/
#define AudioFifoDefaultSize 8192


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/
struct  _audioBackend;


typedef struct _audioFormat {
  int  sampleRate;   // 1/s (<0: undefined)
  int  channels;     // (<0: undefined)
  int  bitWidth;     // per channel (<0: undefined)
  bool isSigned;       // Valid only if bitWith>0
  bool isFloat;        // valid only if bitWith>0
} AudioFormat;

typedef enum {
  AudioDrain,
  AudioDrop,
  AudioForce
} AudioTermMode;

typedef enum {
  AudioIfInitialized,
  AudioIfRunning,
  AudioIfTerminating,
  AudioIfTerminatedOk,
  AudioIfTerminatedError
} AudioIfState;

typedef struct _audioIf {
  AudioIfState                  state;
  const struct _audioBackend   *backend;          // weak
  char                         *devName;          // strong
  Fifo                         *fifoIn;           // strong
  AudioFormat                   format;
  bool                          canPause;
  int                           framesize;
  pthread_t                     thread;
  void                         *ifData;           // handled by individual backend
} AudioIf;

typedef int    (*AudioBackendInit)( void );
typedef int    (*AudioBackendShutdown)( AudioTermMode mode );
typedef int    (*AudioBackendGetDevs)( char ***deviceListPtr, char ***descrListPtr );
typedef int    (*AudioIfNew)( AudioIf *aif, const char *device );
typedef int    (*AudioIfDelete)( AudioIf *aif, AudioTermMode mode );
typedef int    (*AudioIfPlay)( AudioIf *aif, AudioFormat *format );
typedef int    (*AudioIfStop)( AudioIf *aif, AudioTermMode mode );
typedef int    (*AudioIfPause)( AudioIf *aif, bool pause );


typedef struct _audioBackend {
  struct _audioBackend *next;
  char                 *name;
  AudioBackendInit      init;              // optional
  AudioBackendShutdown  shutdown;          // optional
  AudioBackendGetDevs   getDevices;
  AudioIfNew            newIf;             
  AudioIfDelete         deleteIf;
  AudioIfPlay           play;
  AudioIfStop           stop;
  AudioIfPause          pause;             // optional
} AudioBackend;



/*=========================================================================*\
       Prototypes 
\*=========================================================================*/
int                 audioInit( const char *deviceName );
void                audioShutdown( AudioTermMode mode );
int                 audioRegister( AudioBackend *audioBackend );
const AudioBackend *audioBackendsRoot( void );
const AudioBackend *audioBackendByDeviceString( const char *str, const char **device );
int                 audioGetDeviceList( const AudioBackend *backend, char ***deviceListPtr, char ***descrListPtr );
void                audioFreeStringList( char **stringList );
int                 audioCheckDevice( const char *device );

const char         *audioFormatStr( char *buffer, const AudioFormat *format );
int                 audioStrFormat( AudioFormat *format, const char *str );
int                 audioFormatCompare( AudioFormat *format1, AudioFormat *format2 );

AudioIf            *audioIfNew( const AudioBackend *backend, const char *device );
int                 audioIfDelete( AudioIf *aif, AudioTermMode mode );
Fifo               *audioIfPlay( AudioIf *aif, AudioFormat *format );
int                 audioIfStop( AudioIf *aif, AudioTermMode mode );
int                 audioIfSetPause( AudioIf *aif, bool pause );


#endif  /* __AUDIO_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

