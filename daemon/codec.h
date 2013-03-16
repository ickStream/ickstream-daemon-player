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

/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/
struct  _codec;
struct  _codecinstance;

typedef enum {
  CodecInitialized,
  CodecRunning,
  CodecEndOfTrack,
  CodecTerminating,
  CodecTerminatedOk,
  CodecTerminatedError
} CodecInstanceState;

typedef int    (*CodecMetaCallback)( struct _codecinstance *instance, AudioFormat *format, json_t *meta );

typedef struct _codecInstance {
  struct _codecinstance  *next;
  CodecInstanceState      state;
  struct _codec          *codec;            // weak
  void                   *instanceData;     // handled by individual codec
  Fifo                   *fifoOut;          // weak
  int                     endOfInput;
  CodecMetaCallback       metaCallback;
  AudioFormat             format;
  pthread_t               thread;
  pthread_mutex_t         mutex;
  pthread_cond_t          condEndOfTrack;
} CodecInstance;
 
typedef int    (*CodecInit)( void );
typedef int    (*CodecShutdown)( bool force );
typedef bool   (*CodecCheckType)(const char *type, const AudioFormat *format );
typedef int    (*CodecInstanceNew)( CodecInstance *instance ); 
typedef int    (*CodecInstanceDelete)( CodecInstance *instance ); 
typedef int    (*CodecInput)( CodecInstance *instance, void *data, size_t length, size_t *accepted );  
typedef int    (*CodecOutput)( CodecInstance *instance, void *data, size_t maxLength, size_t *realSize );  
typedef int    (*CodecVolume)( CodecInstance *instance, double volume );  
typedef int    (*CodecGetSeekTime)( CodecInstance *instance, double *pos );  


typedef struct _codec {
  struct _codec       *next;
  char                *name;
  size_t               feedChunkSize;     // optional
  CodecInit            init;              // optional
  CodecShutdown        shutdown;          // optional
  CodecCheckType       checkType;
  CodecInstanceNew     newInstance; 
  CodecInstanceDelete  deleteInstance;
  CodecInput           acceptInput;
  CodecOutput          deliverOutput;
  CodecVolume          setVolume;
  CodecGetSeekTime     getSeekTime;
} Codec;


/*=========================================================================*\
       Prototypes 
\*=========================================================================*/
int    codecRegister( Codec *codec );
void   codecShutdown( bool force );
Codec *codecFind( const char *type, const AudioFormat *format, Codec *codec );

CodecInstance *codecNewInstance( Codec *codec, Fifo *fifo, AudioFormat *format );
int            codecDeleteInstance(CodecInstance *instance, bool wait );
int            codecFeedInput( CodecInstance *instance, void *content, size_t size, size_t *accepted );
void           codecSetEndOfInput( CodecInstance *instance );
int            codecWaitForEnd( CodecInstance *instance, int timeout );
int            codecSetVolume( CodecInstance *instance, double volume );
int            codecGetSeekTime( CodecInstance *instance, double *pos );

#endif  /* __CODEC_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/
