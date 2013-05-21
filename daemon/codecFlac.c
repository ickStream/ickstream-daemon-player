/*$*********************************************************************\

Name            : -

Source File     : codecFlac.c

Description     : Wrapper for flac codec library

Comments        : -

Called by       : audio and feeder module 

Calls           : 

Error Messages  : -
  
Date            : 12.05.2013

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
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <jansson.h>
#include <FLAC/format.h>
#include <FLAC/stream_decoder.h>

#include "utils.h"
#include "audio.h"
#include "metaIcy.h"
#include "codec.h"
#include "codecFlac.h"


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
static int    _codecGetSeekTime( CodecInstance *instance, double *pos );  

static FLAC__StreamDecoderReadStatus _read_callback( const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data );
static FLAC__StreamDecoderWriteStatus _write_callback( const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data );
static int _fifo_write_le( CodecInstance *instance, FLAC__int32 sample, unsigned int bytes );

static void _metadata_callback( const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data );
static void _error_callback( const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data );


/*=========================================================================*\
      return descriptor for this codec 
\*=========================================================================*/
Codec *flacDescriptor( void )
{
  static Codec codec;
  
/*------------------------------------------------------------------------*\
    Setup codec descriptor
\*------------------------------------------------------------------------*/
  codec.next           = NULL;
  codec.name           = "flac";
  codec.feedChunkSize  = 0;
  codec.init           = NULL;
  codec.shutdown       = NULL;
  codec.checkType      = &_codecCheckType;
  codec.newInstance    = &_codecNewInstance; 
  codec.deleteInstance = &_codecDeleteInstance;
  codec.deliverOutput  = NULL;
  codec.setVolume      = NULL;
  codec.getSeekTime    = &_codecGetSeekTime;
  
/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return &codec;
}



/*=========================================================================*\
      Check if codec supports audio type and format
        Only check valid components of format
\*=========================================================================*/
static bool _codecCheckType(const char *type, const AudioFormat *format )
{

  // type not supported?
  if( strcmp(type,"flac") && strcmp(type,"audio/flac") )
    return false;

  // Check number of channels (only mono and stereo)
  if( format->channels>0 && format->channels!=1 && format->channels!=2 )
    return false;

  // Check sample rate
  if( format->sampleRate>0 ) {
    if( !FLAC__format_sample_rate_is_valid(format->sampleRate) )
      return false;
  }

  // No checks for encoding

  // type and format is supported
  return true;
}


/*=========================================================================*\
      Get a new codec instance 
\*=========================================================================*/
static int _codecNewInstance( CodecInstance *instance )
{
  FLAC__StreamDecoder           *decoder;
  FLAC__StreamDecoderInitStatus  rc;
  
  DBGMSG( "flac (%p): init instance.", instance );

/*------------------------------------------------------------------------*\
    Get library handle
\*------------------------------------------------------------------------*/
  decoder = FLAC__stream_decoder_new();
  if( !decoder ) {
    logerr( "flac: could not allocate decoder." );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Store auxiliary data in instance and return
\*------------------------------------------------------------------------*/
  instance->instanceData = decoder;


/*------------------------------------------------------------------------*\
    Set md5 checking
\*------------------------------------------------------------------------*/
  FLAC__stream_decoder_set_md5_checking(decoder, true);

/*------------------------------------------------------------------------*\
    Init decoder
\*------------------------------------------------------------------------*/
  rc = FLAC__stream_decoder_init_stream( decoder,
      _read_callback, NULL, NULL, NULL, NULL,
      _write_callback, _metadata_callback, _error_callback, instance );
  if( rc!=FLAC__STREAM_DECODER_INIT_STATUS_OK ) {
    logerr( "flac: could not allocate decoder (%s).",
            FLAC__StreamDecoderInitStatusString[rc] );
    FLAC__stream_decoder_delete( decoder );
    return -1;
}

/*------------------------------------------------------------------------*\
    Execute decoder,
    this will block till end of stream or termination or error
\*------------------------------------------------------------------------*/
  if( !FLAC__stream_decoder_process_until_end_of_stream(decoder) ) {
    FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state( decoder );
    logerr( "flac: decoder returend with error (%s).",
            FLAC__StreamDecoderStateString[state] );
    return -1;
  }
#ifdef ICK_DEBUG
  FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state( decoder );
  DBGMSG( "flac (%p): decoder returned (%s).",
           instance, FLAC__StreamDecoderStateString[state] );
#endif

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Get rid of a codec instance 
\*=========================================================================*/
static int _codecDeleteInstance( CodecInstance *instance )
{
  FLAC__StreamDecoder *decoder = (FLAC__StreamDecoder*)instance->instanceData;
  int                  rc      = 0;
/*------------------------------------------------------------------------*\
    No library handle?
\*------------------------------------------------------------------------*/
  if( !decoder )
    return 0;
  instance->instanceData = NULL;

/*------------------------------------------------------------------------*\
    Close data source (reading pipe end)
\*------------------------------------------------------------------------*/
  if( close(instance->fdIn)<0 ) {
    logerr( "flac: could not close feeder file handle %d (%s).", instance->fdIn,
            strerror(errno) );
    rc = -1;
  }

/*------------------------------------------------------------------------*\
    Delete decoder
\*------------------------------------------------------------------------*/
  FLAC__stream_decoder_finish( decoder );
  FLAC__stream_decoder_delete( decoder );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/  
  return rc;
}


/*=========================================================================*\
      Get seek position (in seconds)
\*=========================================================================*/
static int _codecGetSeekTime( CodecInstance *instance, double *pos )
{
  FLAC__StreamDecoder *decoder = (FLAC__StreamDecoder*)instance->instanceData;
  FLAC__bool    rc;
  FLAC__uint64 bytepos;

/*------------------------------------------------------------------------*\
    Get position from library
\*------------------------------------------------------------------------*/ 
  pthread_mutex_lock( &instance->mutex ); 
  rc = FLAC__stream_decoder_get_decode_position( decoder, &bytepos );
  pthread_mutex_unlock( &instance->mutex );
  if( !rc ) {
    logerr( "flac: could not get decoder position." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Not yet started?
\*------------------------------------------------------------------------*/
  if( !audioFormatIsComplete(&instance->format) ) {
    *pos = 0;
    return 0;
  }

/*------------------------------------------------------------------------*\
    Calculate value (samples/samplerate)
\*------------------------------------------------------------------------*/  
  *pos = bytepos/(instance->format.channels*(instance->format.bitWidth/8))
         / (double)instance->format.sampleRate;
  return 0; 
}


/*=========================================================================*\
       Callback for data input to codec
\*=========================================================================*/
static FLAC__StreamDecoderReadStatus _read_callback( const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data )
{
  CodecInstance *instance = (CodecInstance *) client_data;
  ssize_t        result   = (ssize_t) *bytes;

  DBGMSG( "flac (%p): request %ld bytes from input stream.", instance, (long)*bytes );

/*------------------------------------------------------------------------*\
    Shall we terminate?
\*------------------------------------------------------------------------*/
  if( instance->state!=CodecRunning ) {
    DBGMSG( "flac (%p): detected cancellation.", instance );
    return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
  }

/*------------------------------------------------------------------------*\
    Nothing requested? strange...
\*------------------------------------------------------------------------*/
  if( !result ) {
    logerr( "flac: input request with zero buffer size." );
    return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
  }

/*------------------------------------------------------------------------*\
    Try to read from pipe
\*------------------------------------------------------------------------*/
  result = read( instance->fdIn, buffer, result );

/*------------------------------------------------------------------------*\
    Any error?
\*------------------------------------------------------------------------*/
  if( result<0 ) {
    logerr( "flac: error reading from input stream (%s).", strerror(errno) );
    *bytes = 0;
    return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
  }

/*------------------------------------------------------------------------*\
    End of stream?
\*------------------------------------------------------------------------*/
  if( !result ) {
    DBGMSG( "flac (%p): reached end of input stream.", instance );
    *bytes = 0;
    return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
  }

/*------------------------------------------------------------------------*\
    Everything is fine - continue processing
\*------------------------------------------------------------------------*/
  DBGMSG( "flac (%p): read %ld bytes from input stream.", instance, result );
  *bytes = (size_t) result;
  return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}


/*=========================================================================*\
       Callback for writing data
\*=========================================================================*/
static FLAC__StreamDecoderWriteStatus _write_callback( const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data )
{
  CodecInstance *instance = (CodecInstance *) client_data;
  unsigned int   i;
  unsigned int   bps = frame->header.bits_per_sample;

  DBGMSG( "flac (%p): writing %u samples @ %d bits per samples, %d channels.",
          instance, frame->header.blocksize, bps, frame->header.channels );

/*------------------------------------------------------------------------*\
    Shall we terminate?
\*------------------------------------------------------------------------*/
  if( instance->state!=CodecRunning ) {
    DBGMSG( "flac (%p): detected cancellation.", instance );
    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
  }

/*------------------------------------------------------------------------*\
    Calculate the number of bytes per sample
\*------------------------------------------------------------------------*/
  int bytes = bps/8;
  if( bps-bytes*8 )
    bytes++;

/*------------------------------------------------------------------------*\
    Loop over all samples
\*------------------------------------------------------------------------*/
  for( i=0; i<frame->header.blocksize; i++ ) {
    if( _fifo_write_le( instance, buffer[0][i] /* left channel  */, bytes ) ||
        _fifo_write_le( instance, buffer[1][i] /* right channel */, bytes ) ) {
      DBGMSG( "flac (%p): canceled or error on fifo output.", instance );
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
  }

/*------------------------------------------------------------------------*\
    That's all..
\*------------------------------------------------------------------------*/
  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}


/*=========================================================================*\
       Try to write a sample to the output fifo.
\*=========================================================================*/
static int _fifo_write_le( CodecInstance *instance, FLAC__int32 sample, unsigned int bytes )
{
  int    rc;

/*------------------------------------------------------------------------*\
    Loop while fifo not ready
\*------------------------------------------------------------------------*/
  while( instance->state==CodecRunning ) {

    // Wait max. 500 ms for free space in output fifo
    rc = fifoLockWaitWritable( instance->fifoOut, 500 );
    if( rc==ETIMEDOUT ) {
      continue;
    }
    if( rc ) {
      logerr( "flac: Error while waiting for fifo (%s), terminating.",
              strerror(rc) );
      instance->state = CodecTerminatedError;
      return -1;
    }

    // Not enough space in fifo?
    size_t space = fifoGetSize( instance->fifoOut, FifoNextWritable );
    if( space<bytes ) {
      fifoUnlockAfterWrite( instance->fifoOut, 0 );
      DBGMSG( "flac(%p): not enough sapce in fifo %ld<%d bytes",
              instance, (long)space, bytes );
      continue;
    }

    // Be verbose
    DBGMSG( "flac(%p): writing %d bytes to output (space=%ld)",
            instance, bytes, (long)space );

    // Transcript data
    unsigned char *ptr = (unsigned char *) fifoGetWritePtr( instance->fifoOut );
    while( bytes ) {
      *ptr = sample&0xff;
      sample >>= 8;
    }

    // Unlock fifo and leave wait loop
    fifoUnlockAfterWrite( instance->fifoOut, bytes );
    break;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return 0;
}

/*=========================================================================*\
       Callback for handling meta data
\*=========================================================================*/
static void _metadata_callback( const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data )
{
  CodecInstance *instance = (CodecInstance *) client_data;

  DBGMSG( "flac (%p): init instance.", instance );

/*------------------------------------------------------------------------*\
    Info about stream content
\*------------------------------------------------------------------------*/
  if( metadata->type==FLAC__METADATA_TYPE_STREAMINFO ) {
    DBGMSG( "flac (%p): sample rate     %u Hz", metadata->data.stream_info.sample_rate );
    DBGMSG( "flac (%p): channels        %u", metadata->data.stream_info.channels );
    DBGMSG( "flac (%p): bits per sample %u", metadata->data.stream_info.bits_per_sample );
    DBGMSG( "flac (%p): total samples   %" PRIu64, metadata->data.stream_info.bits_per_sample );

    // Set data
    instance->format.sampleRate = metadata->data.stream_info.sample_rate;
    instance->format.channels   = metadata->data.stream_info.channels;
    instance->format.bitWidth   = metadata->data.stream_info.bits_per_sample;
    DBGMSG( "flac (%p): New stream format: \"%s\"",
            instance, audioFormatStr(NULL,&instance->format) );

    // Inform delegates
    if( instance->formatCallback )
      instance->formatCallback( instance, instance->formatCallbackUserData );
  }

}


/*=========================================================================*\
       Callback for error handling
\*=========================================================================*/
static void _error_callback( const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data )
{
  logerr( "flac: Got error (%s).", FLAC__StreamDecoderErrorStatusString[status] );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/

