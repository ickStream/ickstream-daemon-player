/*$*********************************************************************\

Name            : -

Source File     : codec.h

Description     : Main include file for codecs 

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


#ifndef __CODEC_H
#define __CODEC_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <stdbool.h>
#include <jansson.h>
#include "audio.h"
#include "fifo.h"

/*========================================================================*\
       Macro and type definitions
\*========================================================================*/

// A codec
struct  _codec;
typedef struct _codec Codec;

// A codec instance
struct  _codecinstance;
typedef struct _codecInstance CodecInstance;

// State of codec instances
typedef enum {
  CodecInitialized,
  CodecRunning,
  CodecEndOfTrack,
  CodecTerminating,
  CodecTerminatedOk,
  CodecTerminatedError
} CodecInstanceState;

// Types of meta data
typedef enum {
  CodecMetaID3V1,
  CodecMetaID3V2,
  CodecMetaICY
} CodecMetaType;

/*------------------------------------------------------------------------*\
    Signatures for function pointers
\*------------------------------------------------------------------------*/
typedef int    (*CodecInit)( Codec *codec );
typedef int    (*CodecShutdown)( Codec *codec, bool force );
typedef bool   (*CodecCheckType)(const char *type, const AudioFormat *format );
typedef int    (*CodecInstanceNew)( CodecInstance *instance ); 
typedef int    (*CodecInstanceDelete)( CodecInstance *instance ); 
typedef int    (*CodecOutput)( CodecInstance *instance, void *data, size_t maxLength, size_t *realSize );  
typedef int    (*CodecVolume)( CodecInstance *instance, double volume, bool muted );  
typedef int    (*CodecGetSeekTime)( CodecInstance *instance, double *pos );  

typedef int    (*CodecFormatCallback)( CodecInstance *instance, void *userData );
typedef void   (*CodecMetaCallback)( CodecInstance *instance, CodecMetaType mType, json_t *jMeta, void *userData );


/*------------------------------------------------------------------------*\
    The follwing needs to be public, since direct access by codec modules 
    seems to be more convenient
\*------------------------------------------------------------------------*/
struct _codecInstance {
  CodecInstance               *next;
  volatile CodecInstanceState  state;
  char                        *type;                   // strong
  const Codec                 *codec;                  // weak
  void                        *instanceData;           // handled by individual codec
  int                          fdIn;
  Fifo                        *fifoOut;                // weak
  long                         bytesDelivered;
  CodecFormatCallback          formatCallback;
  void                        *formatCallbackUserData; // weak
  CodecMetaCallback            metaCallback;
  void                        *metaCallbackUserData;   // weak
  AudioFormat                  format;
  long                         icyInterval;
  pthread_t                    thread;
  pthread_mutex_t              mutex_access;
  pthread_mutex_t              mutex_state;
  pthread_cond_t               condIsReady;
  pthread_cond_t               condEndOfTrack;
};
 

struct _codec {
  Codec               *next;
  char                *name;
  size_t               feedChunkSize;       // optional
  CodecInit            init;                // optional
  CodecShutdown        shutdown;            // optional
  CodecCheckType       checkType;
  CodecInstanceNew     newInstance; 
  CodecInstanceDelete  deleteInstance;
  CodecOutput          deliverOutput;       // optional
  CodecVolume          setVolume;           // optional
  CodecGetSeekTime     getSeekTime;
};


/*=========================================================================*\
       Prototypes 
\*=========================================================================*/
int    codecRegister( Codec *codec );
void   codecShutdown( bool force );
Codec *codecFind( const char *type, AudioFormat *format, Codec *codec );

CodecInstance      *codecNewInstance( const Codec *codec, const char *type, const AudioFormat *format, int fd, Fifo *fifo );
void                codecSetFormatCallback( CodecInstance *instance, CodecFormatCallback callback, void *userData );
void                codecSetIcyInterval( CodecInstance *instance, long icyInterval );
void                codecSetMetaCallback( CodecInstance *instance, CodecMetaCallback callback, void *userData );
int                 codecStartInstance( CodecInstance *instance );
int                 codecDeleteInstance(CodecInstance *instance, bool wait );
int                 codecWaitForEnd( CodecInstance *instance, int timeout );
int                 codecSetVolume( CodecInstance *instance, double volume, bool muted );
int                 codecGetSeekTime( CodecInstance *instance, double *pos );
const AudioFormat  *codecGetAudioFormat( CodecInstance *instance );

void                codecInstanceIsInitialized( CodecInstance *instance, CodecInstanceState state );

#endif  /* __CODEC_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

