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
#include <sys/select.h>
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
typedef struct {
  SNDFILE    *sf;        // strong
  SF_INFO     sfinfo;
  char       *frameData;   // strong
  size_t      frameDateSize;
} SndFileDscr;


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static bool   _codecCheckType(const char *type, const AudioFormat *format );
static int    _codecNewInstance( CodecInstance *instance ); 
static int    _codecDeleteInstance( CodecInstance *instance ); 
static int    _codecDeliverOutput( CodecInstance *instance, void *data, size_t maxLength, size_t *realSize );
static void   _copyInteger_le( void *dst, int data, int bytes );
static int    _translateSndInfoFormat( const SF_INFO *sfinfo, AudioFormat *format );


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
  codec.name           = "sndfile";
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
  if( strcmp(type,"audio/wav") && strcmp(type,"audio/x-aiff") )
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
  SndFileDscr *sfd;
  AudioFormat sfformat;
  memset( &sfformat, 0, sizeof(sfformat) );

  DBGMSG( "sndfile (%p): init instance (%s)", instance, instance->type );

/*------------------------------------------------------------------------*\
  Create descriptor
\*------------------------------------------------------------------------*/
  sfd = calloc( 1, sizeof(SndFileDscr) );
  if( !sfd ) {
    logerr( "sndfile: Out of memory." );
    codecInstanceIsInitialized( instance, CodecTerminatedError );
    return -1;
  }

/*------------------------------------------------------------------------*\
  Try to open stream in readonly mode
\*------------------------------------------------------------------------*/
  sfd->sf = sf_open_fd( instance->fdIn, SFM_READ, &sfd->sfinfo, false ) ;
  if( !sfd->sf ) {
    logerr( "sndfile: could not open sound file (%s).", sf_strerror(NULL) );
    Sfree( sfd );
    codecInstanceIsInitialized( instance, CodecTerminatedError );
    return -1;
  }
  DBGMSG( "sndfile (%p): format is 0x%x", instance, sfd->sfinfo.format );
  DBGMSG( "sndfile (%p): %d channels @ %d Hz", instance, sfd->sfinfo.channels, sfd->sfinfo.samplerate );
  DBGMSG( "sndfile (%p): %ld frames, %d sections", instance, sfd->sfinfo.frames, sfd->sfinfo.sections );

/*------------------------------------------------------------------------*\
  Do we actually support this format?
\*------------------------------------------------------------------------*/
  if( _translateSndInfoFormat(&sfd->sfinfo,&sfformat) ) {
    logerr( "sndfile: format 0x%x not supported.", sfd->sfinfo.format );
    sf_close( sfd->sf );
    Sfree( sfd );
    codecInstanceIsInitialized( instance, CodecTerminatedError );
    return -1;
  }
  DBGMSG( "sndfile (%p): Item audio format information: \"%s\"",
          instance, audioFormatStr(NULL,&instance->format) );
  DBGMSG( "sndfile (%p): Stream audio format information: \"%s\"",
          instance, audioFormatStr(NULL,&sfformat) );

/*------------------------------------------------------------------------*\
  Check format consistency
\*------------------------------------------------------------------------*/
  // fixme; we might complete the instance->format here
  // (and do not even need to call instance->formatCallback)
  if( sfformat.channels != instance->format.channels ) {
    logerr( "sndfile: channel mismatch (header %d, item descriptor %d).",
        sfformat.channels, instance->format.channels );
    sf_close( sfd->sf );
    Sfree( sfd );
    codecInstanceIsInitialized( instance, CodecTerminatedError );
    return -1;
  }
  if( sfformat.sampleRate != instance->format.sampleRate ) {
    logerr( "sndfile: sample rate mismatch (header %d, item descriptor %d).",
        sfformat.sampleRate, instance->format.sampleRate );
    sf_close( sfd->sf );
    Sfree( sfd );
    codecInstanceIsInitialized( instance, CodecTerminatedError );
    return -1;
  }
  // fixme: float data is not handled right and probably triggers a bitwidth mismatch
  if( sfformat.bitWidth != instance->format.bitWidth ) {
    logerr( "sndfile: bitwidth mismatch (header %d, item descriptor %d).",
        sfformat.bitWidth, instance->format.bitWidth );
    sf_close( sfd->sf );
    Sfree( sfd );
    codecInstanceIsInitialized( instance, CodecTerminatedError );
    return -1;
  }

/*------------------------------------------------------------------------*\
  Allocate buffer for frame data
\*------------------------------------------------------------------------*/
  sfd->frameData = malloc( instance->format.channels*instance->format.bitWidth/8 );

/*------------------------------------------------------------------------*\
  Store sound file library descriptor
\*------------------------------------------------------------------------*/
  instance->instanceData = sfd;

/*------------------------------------------------------------------------*\
    Signal that codec is up and running and return
\*------------------------------------------------------------------------*/
  codecInstanceIsInitialized( instance, CodecRunning );
  return 0;
}


/*=========================================================================*\
      Get rid of a codec instance 
\*=========================================================================*/
static int _codecDeleteInstance( CodecInstance *instance )
{
  int rc = 0;
  SndFileDscr *sfd = instance->instanceData;

/*------------------------------------------------------------------------*\
    Close sound file library descriptor, if any
\*------------------------------------------------------------------------*/
  if( sfd  ) {
    if( sfd->sf )
      sf_close( sfd->sf );
    if( sfd->frameData )
      Sfree( sfd->frameData );
    Sfree( sfd );
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
               -1  on error
                1  when end of track is reached
\*=========================================================================*/
static int _codecDeliverOutput( CodecInstance *instance, void *data, size_t maxLength, size_t *realSize )
{
  SndFileDscr *sfd = instance->instanceData;
  int          i;
  int          perr;
  sf_count_t   samples;
  int          sampleBuff[16];  // max 16 channels...

  DBGMSG( "sndfile (%p): data requested (max. %ld bytes).",
          instance, (long)maxLength );

/*------------------------------------------------------------------------*\
    Calculate size of a sample and collect data
\*------------------------------------------------------------------------*/
  *realSize  = 0;
  while( instance->state==CodecRunning ) {
    fd_set         rfds;
    struct timeval tv;
    int            retval;

    // Process data in frame buffer
    if( sfd->frameDateSize ) {
      size_t i;
      size_t len = MIN( maxLength, sfd->frameDateSize );
      if( len )
        memcpy( data, sfd->frameData, len );

      // Adapt pointer and counters
      data = ((char*)data) + len;
      sfd->frameDateSize -= len;
      maxLength -= len;
      *realSize += len;

      // Shift any remaining frame data to start of buffer
      for( i=0; len && i<sfd->frameDateSize; i++ )
        sfd->frameData[i] = sfd->frameData[i+len];

      // Still data left in frame buffer: this is probably due to a fifo flip over
      if( sfd->frameDateSize )
        return 0;
    }

    // Wait 500ms for input data
    FD_ZERO( &rfds );
    FD_SET( instance->fdIn, &rfds );
    tv.tv_sec  = 0;
    tv.tv_usec = 500*1000;
    retval     = select( instance->fdIn+1, &rfds, NULL, NULL, &tv );
    if( retval<0 ) {
      logerr( "sndfile: select returned %s", strerror(errno) );
      return -1;
    }
    else if( !retval ) {
      DBGMSG( "sndfile (%p): waiting for pipe to be readable...",
               instance );
      return 0;
    }

    // get sample vector for next frame
    perr = pthread_mutex_lock( &instance->mutex_access );
    if( perr )
      logerr( "_codecDeliverOutput: locking codec access mutex: %s", strerror(perr) );
    samples = sf_read_int( sfd->sf, sampleBuff, instance->format.channels );
    perr = pthread_mutex_unlock( &instance->mutex_access );
    if( perr )
      logerr( "_codecDeliverOutput: unlocking codec access mutex: %s", strerror(perr) );

    // End of file or error?
    if( !samples ) {
      DBGMSG( "sndfile (%p): sf_read:int() returned 0.", instance );
      instance->state = CodecEndOfTrack;
      pthread_cond_signal( &instance->condEndOfTrack );
      return 0;
    }

    // Transfer sample for each channel to buffer
    for( i=0; i<instance->format.channels; i++ ) {
      // convert to original bitwidth
      //DBGMSG( "sndfile (%p): channel %d: %10d 0x%08x", instance, i, sampleBuff[i], sampleBuff[i] );
      _copyInteger_le( sfd->frameData+sfd->frameDateSize, sampleBuff[i], instance->format.bitWidth/8 );
      sfd->frameDateSize += instance->format.bitWidth/8;
    }

    // next frame
  }

  DBGMSG( "sndfile (%p): delivered data (%ld bytes).",
          instance, (long)*realSize );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
       Copy integer data with defined byte width to output buffer (le)
\*=========================================================================*/
static void _copyInteger_le( void *dst, int data, int bytes )
{
  while( bytes-- ) {
    *(unsigned char*)dst = (data>>((sizeof(int)-bytes-1)*8))&0xff;
    // DBGMSG( "sndfile: byte %d: %d, 0x%02x", bytes, (int)*(unsigned char*)dst, (int)*(unsigned char*)dst );
    dst = ((char*)dst)+1;
  }
}


/*=========================================================================*\
       Translate sndfile info to internal audio format representation
\*=========================================================================*/
static int _translateSndInfoFormat( const SF_INFO *sfinfo, AudioFormat *format )
{

#if 0
  enum
       {   /* Major formats. */
           SF_FORMAT_WAV          = 0x010000,     /* Microsoft WAV format (little endian). */
           SF_FORMAT_AIFF         = 0x020000,     /* Apple/SGI AIFF format (big endian). */
           SF_FORMAT_AU           = 0x030000,     /* Sun/NeXT AU format (big endian). */
           SF_FORMAT_RAW          = 0x040000,     /* RAW PCM data. */
           SF_FORMAT_PAF          = 0x050000,     /* Ensoniq PARIS file format. */
           SF_FORMAT_SVX          = 0x060000,     /* Amiga IFF / SVX8 / SV16 format. */
           SF_FORMAT_NIST         = 0x070000,     /* Sphere NIST format. */
           SF_FORMAT_VOC          = 0x080000,     /* VOC files. */
           SF_FORMAT_IRCAM        = 0x0A0000,     /* Berkeley/IRCAM/CARL */
           SF_FORMAT_W64          = 0x0B0000,     /* Sonic Foundry's 64 bit RIFF/WAV */
           SF_FORMAT_MAT4         = 0x0C0000,     /* Matlab (tm) V4.2 / GNU Octave 2.0 */
           SF_FORMAT_MAT5         = 0x0D0000,     /* Matlab (tm) V5.0 / GNU Octave 2.1 */
           SF_FORMAT_PVF          = 0x0E0000,     /* Portable Voice Format */
           SF_FORMAT_XI           = 0x0F0000,     /* Fasttracker 2 Extended Instrument */
           SF_FORMAT_HTK          = 0x100000,     /* HMM Tool Kit format */
           SF_FORMAT_SDS          = 0x110000,     /* Midi Sample Dump Standard */
           SF_FORMAT_AVR          = 0x120000,     /* Audio Visual Research */
           SF_FORMAT_WAVEX        = 0x130000,     /* MS WAVE with WAVEFORMATEX */
           SF_FORMAT_SD2          = 0x160000,     /* Sound Designer 2 */
           SF_FORMAT_FLAC         = 0x170000,     /* FLAC lossless file format */
           SF_FORMAT_CAF          = 0x180000,     /* Core Audio File format */
           SF_FORMAT_WVE          = 0x190000,     /* Psion WVE format */
           SF_FORMAT_OGG          = 0x200000,     /* Xiph OGG container */
           SF_FORMAT_MPC2K        = 0x210000,     /* Akai MPC 2000 sampler */
           SF_FORMAT_RF64         = 0x220000,     /* RF64 WAV file */

           /* Subtypes from here on. */

           SF_FORMAT_PCM_S8       = 0x0001,       /* Signed 8 bit data */
           SF_FORMAT_PCM_16       = 0x0002,       /* Signed 16 bit data */
           SF_FORMAT_PCM_24       = 0x0003,       /* Signed 24 bit data */
           SF_FORMAT_PCM_32       = 0x0004,       /* Signed 32 bit data */

           SF_FORMAT_PCM_U8       = 0x0005,       /* Unsigned 8 bit data (WAV and RAW only) */

           SF_FORMAT_FLOAT        = 0x0006,       /* 32 bit float data */
           SF_FORMAT_DOUBLE       = 0x0007,       /* 64 bit float data */

           SF_FORMAT_ULAW         = 0x0010,       /* U-Law encoded. */
           SF_FORMAT_ALAW         = 0x0011,       /* A-Law encoded. */
           SF_FORMAT_IMA_ADPCM    = 0x0012,       /* IMA ADPCM. */
           SF_FORMAT_MS_ADPCM     = 0x0013,       /* Microsoft ADPCM. */

           SF_FORMAT_GSM610       = 0x0020,       /* GSM 6.10 encoding. */
           SF_FORMAT_VOX_ADPCM    = 0x0021,       /* Oki Dialogic ADPCM encoding. */

           SF_FORMAT_G721_32      = 0x0030,       /* 32kbs G721 ADPCM encoding. */
           SF_FORMAT_G723_24      = 0x0031,       /* 24kbs G723 ADPCM encoding. */
           SF_FORMAT_G723_40      = 0x0032,       /* 40kbs G723 ADPCM encoding. */

           SF_FORMAT_DWVW_12      = 0x0040,       /* 12 bit Delta Width Variable Word encoding. */
           SF_FORMAT_DWVW_16      = 0x0041,       /* 16 bit Delta Width Variable Word encoding. */
           SF_FORMAT_DWVW_24      = 0x0042,       /* 24 bit Delta Width Variable Word encoding. */
           SF_FORMAT_DWVW_N       = 0x0043,       /* N bit Delta Width Variable Word encoding. */

           SF_FORMAT_DPCM_8       = 0x0050,       /* 8 bit differential PCM (XI only) */
           SF_FORMAT_DPCM_16      = 0x0051,       /* 16 bit differential PCM (XI only) */

           SF_FORMAT_VORBIS       = 0x0060,       /* Xiph Vorbis encoding. */

           /* Endian-ness options. */

           SF_ENDIAN_FILE         = 0x00000000,   /* Default file endian-ness. */
           SF_ENDIAN_LITTLE       = 0x10000000,   /* Force little endian-ness. */
           SF_ENDIAN_BIG          = 0x20000000,   /* Force big endian-ness. */
           SF_ENDIAN_CPU          = 0x30000000,   /* Force CPU endian-ness. */

           SF_FORMAT_SUBMASK      = 0x0000FFFF,
           SF_FORMAT_TYPEMASK     = 0x0FFF0000,
           SF_FORMAT_ENDMASK      = 0x30000000
       } ;
#endif

  switch( sfinfo->format&SF_FORMAT_SUBMASK ) {
    case SF_FORMAT_PCM_S8:
      format->isFloat  = false;
      format->isSigned = true;
      format->bitWidth = 8;
      break;
    case SF_FORMAT_PCM_16:
      format->isFloat  = false;
      format->isSigned = true;
      format->bitWidth = 16;
      break;
    case SF_FORMAT_PCM_24:
      format->isFloat  = false;
      format->isSigned = true;
      format->bitWidth = 24;
      break;
    case SF_FORMAT_PCM_32:
      format->isFloat  = false;
      format->isSigned = true;
      format->bitWidth = 32;
      break;
    case SF_FORMAT_PCM_U8:
      format->isFloat  = false;
      format->isSigned = false;
      format->bitWidth = 8;
      break;
    case SF_FORMAT_FLOAT:
      format->isFloat  = true;
      format->isSigned = true;
      format->bitWidth = sizeof(float)*8;
      break;
    case SF_FORMAT_DOUBLE:
      format->isFloat  = true;
      format->isSigned = true;
      format->bitWidth = sizeof(double)*8;
      break;

    default:
      return -1;
  }

  format->channels   = sfinfo->channels;
  format->sampleRate = sfinfo->samplerate;

  return 0;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/

