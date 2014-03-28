/*$*********************************************************************\

Name            : -

Source File     : codecSndFile.c

Description     : Wrapper for libsndfile codec library

Comments        : -

Called by       : audio and feeder module 

Calls           : 

Error Messages  : -
  
Date            : 27.03.2014

Updates         : -
                  
Author          : //MAF 

Remarks         : -

*************************************************************************
 * Copyright (c) 2013,14 ickStream GmbH
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
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sndfile.h>

#include "ickutils.h"
#include "audio.h"
#include "codec.h"
#include "codecSndFile.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
	Private symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static bool   _codecCheckType(const char *type, const AudioFormat *format );
static int    _codecNewInstance( CodecInstance *instance ); 
static int    _codecDeleteInstance( CodecInstance *instance ); 
static int    _codecDeliverOutput( CodecInstance *instance, void *data, size_t maxLength, size_t *realSize );


/*=========================================================================*\
      return descriptor for this codec 
\*=========================================================================*/
Codec *sndFileDescriptor( void )
{
  static Codec codec;
  
/*------------------------------------------------------------------------*\
    Setup codec descriptor
\*------------------------------------------------------------------------*/
  codec.next           = NULL;
  codec.name           = "pcm";
  codec.feedChunkSize  = 0;
  codec.init           = NULL;
  codec.shutdown       = NULL;
  codec.checkType      = &_codecCheckType;
  codec.newInstance    = &_codecNewInstance; 
  codec.deleteInstance = &_codecDeleteInstance;
  codec.deliverOutput  = &_codecDeliverOutput;
  codec.setVolume      = NULL;
  codec.getSeekTime    = NULL;
  
/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return &codec;
}



/*=========================================================================*\
      Check if codec supports audio type and format
        Only check valid components of format
\*=========================================================================*/
static bool _codecCheckType( const char *type, const AudioFormat *format )
{

  // type not supported?
  if( strcmp(type,"wav") && strcmp(type,"audio/wav") )
    return false;

  // Check number of channels (only mono and stereo)
  if( format->channels>0 && format->channels!=1 && format->channels!=2 )
    return false;

  // No check for rate

  // No checks for encoding

  // type and format is supported
  return true;
}


/*=========================================================================*\
      Get a new codec instance 
\*=========================================================================*/
static int _codecNewInstance( CodecInstance *instance )
{
  SNDFILE *sf;
  SF_INFO  sfinfo;
  memset( &sfinfo, 0, sizeof(sfinfo) );

  DBGMSG( "sndfile (%p): init instance (%s)", instance, instance->type );

/*------------------------------------------------------------------------*\
  Try to open stream in readonly mode
\*------------------------------------------------------------------------*/
  sf = sf_open_fd( instance->fdIn, SFM_READ, &sfinfo, false ) ;
  if( !sf ) {
    logerr( "sndfile: could not open sound file (%s).", sf_strerror(NULL) );
    return -1;
  }
  DBGMSG( "sndfile (%p): format is %x", instance, sfinfo.format );
  DBGMSG( "sndfile (%p): %d channels @ %d Hz", instance, sfinfo.channels, sfinfo.samplerate );
  DBGMSG( "sndfile (%p): %ld frame, %d sections", instance, sfinfo.frames, sfinfo.sections );

/*------------------------------------------------------------------------*\
  Do we actually support this format?
\*------------------------------------------------------------------------*/
  // fixme

/*------------------------------------------------------------------------*\
  Check format consistency
\*------------------------------------------------------------------------*/
  if( sfinfo.channels != instance->format.channels ) {
    logerr( "sndfile: channel mismatch (header %d, item descriptor %d).",
            sfinfo.channels, instance->format.channels );
    return -1;
  }
  if( sfinfo.samplerate != instance->format.sampleRate ) {
    logerr( "sndfile: sample rate mismatch (header %d, item descriptor %d).",
            sfinfo.samplerate, instance->format.sampleRate );
    return -1;
  }
  // fixme: check bit width and signage, int/float type
  // we might complete the format here according

/*------------------------------------------------------------------------*\
  Store sound file library descriptor, that's all
\*------------------------------------------------------------------------*/
  instance->instanceData = sf;
  return 0;
}


/*=========================================================================*\
      Get rid of a codec instance 
\*=========================================================================*/
static int _codecDeleteInstance( CodecInstance *instance )
{
  int rc = 0;

/*------------------------------------------------------------------------*\
    Close sound file library descriptor, if any
\*------------------------------------------------------------------------*/
  if( instance->instanceData  ) {
    sf_close( instance->instanceData );
    instance->instanceData = NULL;
  }

/*------------------------------------------------------------------------*\
    Close data source (reading pipe end)
\*------------------------------------------------------------------------*/
  if( close(instance->fdIn)<0 ) {
    logerr( "sndfile: could not close feeder file handle %d (%s).", instance->fdIn,
            strerror(errno) );
    rc = -1;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/  
  return rc;
}


/*=========================================================================*\
      Write data to output
        return  0  on success
               -1 on error
                1  when end of track is reached
\*=========================================================================*/
static int _codecDeliverOutput( CodecInstance *instance, void *data, size_t maxLength, size_t *realSize )
{
//   SNDFILE *sf = instance->instanceData;
  ssize_t     bytes;

/*------------------------------------------------------------------------*\
    Get data from sound file library handle till buffer is full
\*------------------------------------------------------------------------*/
  *realSize = 0;
  // pthread_mutex_lock( &instance->mutex );
  // fixme: use sf_read functions!
  bytes = read( instance->fdIn, data, maxLength );
  // pthread_mutex_unlock( &instance->mutex );

/*------------------------------------------------------------------------*\
    Interpret result
\*------------------------------------------------------------------------*/
  if( bytes<0 ) {
    logerr( "sndfile: could not deliver data (avail %ld bytes): %s",
            (long)maxLength, strerror(errno) );
    return -1;
  }
  else if( !bytes ) {
    DBGMSG( "sndfile (%p): read returned 0.",
           instance );
    instance->state = CodecEndOfTrack;
    pthread_cond_signal( &instance->condEndOfTrack );
    return 0;
  }
  *realSize = (size_t)bytes;
  DBGMSG( "sndfile (%p): delivered data (%ld/%ld bytes).",
          instance, (long)*realSize, (long)maxLength );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/

