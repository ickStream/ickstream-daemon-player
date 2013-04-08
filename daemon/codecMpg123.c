/*$*********************************************************************\

Name            : -

Source File     : codecMpg123.c

Description     : Wrapper for mpg123 codec library

Comments        : -

Called by       : audio and feeder module 

Calls           : 

Error Messages  : -
  
Date            : 02.03.2013

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
#include <mpg123.h>

#include "utils.h"
#include "audio.h"
#include "codec.h"
#include "codecMpg123.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
	Private symbols
\*=========================================================================*/
#define MPG123ERRSTR(rc,mh) (((rc)==MPG123_ERR&&(mh))?mpg123_strerror(mh):mpg123_plain_strerror(rc))


/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static int    _codecInit( Codec *codec );
static int    _codecShutdown( Codec *codec, bool force );
static bool   _codecCheckType(const char *type, const AudioFormat *format );
static int    _codecNewInstance( CodecInstance *instance ); 
static int    _codecDeleteInstance( CodecInstance *instance ); 
static int    _codecDeliverOutput( CodecInstance *instance, void *data, size_t maxLength, size_t *realSize );
static int    _codecSetVolume( CodecInstance *instance, double volume, bool muted );
static int    _codecGetSeekTime( CodecInstance *instance, double *pos );  

static enum mpg123_enc_enum _getMpg123Format( const AudioFormat *format );
static int _translateMpg123Format( int encoding, AudioFormat *format );


/*=========================================================================*\
      return descriptor for this codec 
\*=========================================================================*/
Codec *mpg123Descriptor( void )
{
  static Codec codec;
  
/*------------------------------------------------------------------------*\
    Setup codec descriptor
\*------------------------------------------------------------------------*/
  codec.next           = NULL;
  codec.name           = "mpg123";
  codec.feedChunkSize  = 0;
  codec.init           = &_codecInit;
  codec.shutdown       = &_codecShutdown;
  codec.checkType      = &_codecCheckType;
  codec.newInstance    = &_codecNewInstance; 
  codec.deleteInstance = &_codecDeleteInstance;
  codec.deliverOutput  = &_codecDeliverOutput;
  codec.setVolume      = &_codecSetVolume;
  codec.getSeekTime    = &_codecGetSeekTime;
  
/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return &codec;
}

/*=========================================================================*\
      Global init for codec lib 
        return false on error
\*=========================================================================*/
static int _codecInit( Codec *codec )
{
  int rc;
  
/*------------------------------------------------------------------------*\
    Try to init lib 
\*------------------------------------------------------------------------*/
  rc = mpg123_init();
  if( rc!=MPG123_OK ) {
    logerr( "mpg123: could not init lib: %s", mpg123_plain_strerror(rc) );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Global shutdown for codec lib 
\*=========================================================================*/
static int _codecShutdown( Codec *codec, bool force )
{

/*------------------------------------------------------------------------*\
    Shut down library
\*------------------------------------------------------------------------*/
  mpg123_exit();

/*------------------------------------------------------------------------*\
    That#s all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Check if codec supports audio type and format
        Only check valid components of format
\*=========================================================================*/
static bool _codecCheckType(const char *type, const AudioFormat *format )
{

  // type not supported?
  if( strcmp(type,"mp3") && strcmp(type,"audio/mpeg") )
    return false;

  // Check number of channels (only mono and stereo)
  if( format->channels>0 && format->channels!=1 && format->channels!=2 )
    return false;

  // Check sample rate
  if( format->sampleRate>0 ) {
    const long *rateList;
    size_t      listLen, i;

    mpg123_rates( &rateList, &listLen );
    for( i=0; i<listLen && rateList[i]!=format->sampleRate; i++ )
      ;
    if( i==listLen )
      return false;
  }

  // Check encoding
  if( format->bitWidth>0 ) {
    enum mpg123_enc_enum  mpg123Format;
    const int            *encList;
    size_t                listLen, i;

    mpg123Format = _getMpg123Format( format );
    mpg123_encodings( &encList, &listLen );
    for( i=0; i<listLen && encList[i]!=mpg123Format; i++ )
      ;
    if( i==listLen )
      return false;
  }

  // type and format is supported
  return true;    
}


/*=========================================================================*\
      Get a new codec instance 
\*=========================================================================*/
static int _codecNewInstance( CodecInstance *instance )
{
  mpg123_handle *mh;
  int            rc = MPG123_OK;
  
  DBGMSG( "mpg123 (%p): init instance", instance  );

/*------------------------------------------------------------------------*\
    Get libarary handle 
\*------------------------------------------------------------------------*/
  mh = mpg123_new( NULL, &rc );   
  if( !mh ) {
    logerr( "mpg123: could not init instance: %s", mpg123_plain_strerror(rc) );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Set debug mode
\*------------------------------------------------------------------------*/
#ifdef ICK_DEBUG
  DBGMSG( "mpg123 (%p): setting verbosity to %d", instance, streamloglevel );
  rc = mpg123_param( mh, MPG123_VERBOSE, streamloglevel, 0);
  if( rc!=MPG123_OK ) {
    logerr( "mpg123: could not set verbosity level (%d): %d, %s",
            streamloglevel, rc, MPG123ERRSTR(rc,mh) );
    mpg123_delete( mh );
    return -1;
  }
#endif

/*------------------------------------------------------------------------*\
    Icy mode?
\*------------------------------------------------------------------------*/
  if( instance->icyInterval ) {
    DBGMSG( "mpg123 (%p): icy enabled (interval %ld)",
            instance, instance->icyInterval );
    if( !mpg123_feature(MPG123_FEATURE_PARSE_ICY) ) {
      logerr( "mpg123: icy not supported by this library version." );
      mpg123_delete( mh );
      return -1;
    }
    rc = mpg123_param( mh, MPG123_ICY_INTERVAL, instance->icyInterval, 0);
    if( rc!=MPG123_OK ) {
      logerr( "mpg123: could not set icy interval (%ld): %d, %s",
              instance->icyInterval, rc, MPG123ERRSTR(rc,mh) );
      mpg123_delete( mh );
      return -1;
    }
    // mpg123_param( mh, MPG123_ADD_FLAGS, MPG123_IGNORE_STREAMLENGTH, 0);
    // mpg123_param( mh, MPG123_REMOVE_FLAGS, MPG123_GAPLESS, 0);
  }

/*------------------------------------------------------------------------*\
    Start decoder 
\*------------------------------------------------------------------------*/
  rc = mpg123_open_fd( mh, instance->fdIn );
  if( rc!=MPG123_OK ) {
    logerr( "mpg123: could not open file handle %d: %d, %s",
            instance->fdIn, rc, MPG123ERRSTR(rc,mh) );
    mpg123_delete( mh );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Store auxiliary data in instance and return 
\*------------------------------------------------------------------------*/
  instance->instanceData = mh;
  return 0;    
}


/*=========================================================================*\
      Get rid of a codec instance 
\*=========================================================================*/
static int _codecDeleteInstance( CodecInstance *instance )
{
  mpg123_handle *mh = (mpg123_handle *)instance->instanceData;    
  int            rc = 0;
  
/*------------------------------------------------------------------------*\
    No library handle?
\*------------------------------------------------------------------------*/
  if( !mh )
    return 0;
  instance->instanceData = NULL;
      
/*------------------------------------------------------------------------*\
    Close data source (reading pipe end)
\*------------------------------------------------------------------------*/
  if( close(instance->fdIn)<0 ) {
    logerr( "mpg123: could not close file handle %d: %s", instance->fdIn,
            strerror(errno) );
    rc = -1;
  }

/*------------------------------------------------------------------------*\
    Delete decoder
\*------------------------------------------------------------------------*/  
  mpg123_delete( mh );
  
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/  
  return rc;
}


/*=========================================================================*\
      Write data to output 
        return  0  on succes
               -1 on error
                1  when end of track is reached
\*=========================================================================*/
static int _codecDeliverOutput( CodecInstance *instance, void *data, size_t maxLength, size_t *realSize )
{
  mpg123_handle *mh = (mpg123_handle*)instance->instanceData;
  long           rate;
  int            channels, encoding;
  int            metaVector;
  int            rc;
  int            err = 0;
  
/*------------------------------------------------------------------------*\
    Get data from decoder
\*------------------------------------------------------------------------*/  
  *realSize = 0;
  pthread_mutex_lock( &instance->mutex );
  rc         = mpg123_read( mh, data, maxLength, realSize );
  metaVector = mpg123_meta_check( mh );
  pthread_mutex_unlock( &instance->mutex );
  
  DBGMSG( "mpg123 (%p): delivered data (%ld/%ld bytes), meta=%d: %s",
          instance, (long)*realSize, (long)maxLength, metaVector, MPG123ERRSTR(rc,mh) );

/*------------------------------------------------------------------------*\
    New meta data?
\*------------------------------------------------------------------------*/
  if( metaVector&(MPG123_NEW_ID3|MPG123_NEW_ICY) ) {

    // Decode ID3 data
    if( metaVector&MPG123_NEW_ID3 ) {
      mpg123_id3v1 *id3V1;
      mpg123_id3v2 *id3V2;
      mpg123_id3( mh, &id3V1, &id3V2 );
      DBGMSG( "New ID3 data: "  );
      // Fixme.
    }

    // Decode ICY data
    if( metaVector&MPG123_NEW_ICY ) {
      char *icyString;
      mpg123_icy( mh, &icyString );
      DBGMSG( "New ICY data: \"%s\"", icyString );
      // Fixme.
    }

    // Reset Data in stream
    mpg123_meta_free( mh );
  }

/*------------------------------------------------------------------------*\
    Interpret result
\*------------------------------------------------------------------------*/  
  switch( rc ) {
  
    // Everything fine
    case MPG123_OK:
      break;

    // Waiting for more data
    // Ignore if not at end of input, else treat like MPG123_DONE
    case MPG123_NEED_MORE:
//     if( !instance->endOfInput )
        break;

    // End of track: set status and call player callback
    case MPG123_DONE:
      instance->state = CodecEndOfTrack;
      pthread_cond_signal( &instance->condEndOfTrack );
      break;

    // Format detected or changed: inform player via call back
    case MPG123_NEW_FORMAT:
      mpg123_getformat( mh, &rate, &channels, &encoding );
      instance->format.sampleRate = rate;
      instance->format.channels   = channels;
      if( _translateMpg123Format(encoding,&instance->format) )
        logwarn( "mpg123: encoding %d not supported (ignored)", encoding );
      DBGMSG( "mpg123 (%p): New stream format: \"%s\"",
              instance, audioFormatStr(NULL,&instance->format) );
      if( instance->formatCallback )
        instance->formatCallback( instance, instance->formatCallbackUserData );
      break;

    // Report real error 
    default:
      logerr( "mpg123: could not deliver data (avail %ld bytes): %s", 
              (long)maxLength, MPG123ERRSTR(rc,mh) );
      err = -1;
      break;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/  
  return err; 
}


/*=========================================================================*\
      Set output volume
\*=========================================================================*/
static int _codecSetVolume( CodecInstance *instance, double volume, bool muted )
{
  mpg123_handle *mh = (mpg123_handle*)instance->instanceData;
  int            rc;

/*------------------------------------------------------------------------*\
    Call library
\*------------------------------------------------------------------------*/ 
  pthread_mutex_lock( &instance->mutex ); 
  rc = mpg123_volume( mh, muted?0.0:volume );
  pthread_mutex_unlock( &instance->mutex );
  if( rc )
    logerr( "mpg123: could not set volume to %f %s: %s", 
             volume, muted?"(muted)":"(unmuted)", MPG123ERRSTR(rc,mh) );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/  
  return rc ? -1 : 0;
}


/*=========================================================================*\
      Get seek position (in seconds)
\*=========================================================================*/
static int _codecGetSeekTime( CodecInstance *instance, double *pos )
{
  mpg123_handle *mh = (mpg123_handle*)instance->instanceData;
  off_t          samples;

/*------------------------------------------------------------------------*\
    Call library
\*------------------------------------------------------------------------*/ 
  pthread_mutex_lock( &instance->mutex ); 
  samples = mpg123_tell( mh );
  pthread_mutex_unlock( &instance->mutex );
                       
/*------------------------------------------------------------------------*\
    return calculated value (samples/samplerate)
\*------------------------------------------------------------------------*/  
  *pos = samples/(double)instance->format.sampleRate;
  return 0; 
}


/*=========================================================================*\
       Translate audio format to mpg123 library standard 
\*=========================================================================*/
static enum mpg123_enc_enum _getMpg123Format( const AudioFormat *format )
{
  DBGMSG( "_getMpg123Format: %s", audioFormatStr(NULL,format) ); 

/*------------------------------------------------------------------------*\
    Relevant float formats  
\*------------------------------------------------------------------------*/
  if( format->isFloat ) {
    if( format->bitWidth==32 )
      return MPG123_ENC_FLOAT_32;
    if( format->bitWidth==64 )
      return MPG123_ENC_FLOAT_64;
    return 0;
  }

/*------------------------------------------------------------------------*\
    Relevant signed formats  
\*------------------------------------------------------------------------*/
  if( format->isSigned ) {
    if( format->bitWidth==8 )
      return MPG123_ENC_SIGNED_8;
    if( format->bitWidth==16 )
      return MPG123_ENC_SIGNED_16;
    if( format->bitWidth==24 )
      return MPG123_ENC_SIGNED_24;
    if( format->bitWidth==32 )
      return MPG123_ENC_SIGNED_32;
    return 0;
  }

/*------------------------------------------------------------------------*\
    Relevant unsigned formats  
\*------------------------------------------------------------------------*/
  if( format->bitWidth==8 )
    return MPG123_ENC_UNSIGNED_8;
  if( format->bitWidth==16 )
    return MPG123_ENC_UNSIGNED_16;
  if( format->bitWidth==24 )
    return MPG123_ENC_UNSIGNED_24;
  if( format->bitWidth==32 )
    return MPG123_ENC_UNSIGNED_32;

/*------------------------------------------------------------------------*\
    Not known  
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
       Translate mpg123 format to internal audio format representation
\*=========================================================================*/
static int _translateMpg123Format( int encoding, AudioFormat *format )
{

  switch( encoding ) {
    case MPG123_ENC_FLOAT_32:
      format->isFloat = true;
      format->bitWidth = 32;
      break;

    case MPG123_ENC_FLOAT_64:
      format->isFloat = true;
      format->bitWidth = 64;
      break;

    case MPG123_ENC_SIGNED_8:
      format->isFloat = false;
      format->isSigned = true;
      format->bitWidth = 8;
      break;

    case MPG123_ENC_SIGNED_16:
      format->isFloat = false;
      format->isSigned = true;
      format->bitWidth = 16;
      break;

    case MPG123_ENC_SIGNED_24:
      format->isFloat = false;
      format->isSigned = true;
      format->bitWidth = 24;
      break;

    case MPG123_ENC_SIGNED_32:
      format->isFloat = false;
      format->isSigned = true;
      format->bitWidth = 32;
      break;

    case MPG123_ENC_UNSIGNED_8:
      format->isFloat = false;
      format->isSigned = false;
      format->bitWidth = 8;
      break;

    case MPG123_ENC_UNSIGNED_16:
      format->isFloat = false;
      format->isSigned = false;
      format->bitWidth = 16;
      break;

    case MPG123_ENC_UNSIGNED_24:
      format->isFloat = false;
      format->isSigned = false;
      format->bitWidth = 24;
      break;

    case MPG123_ENC_UNSIGNED_32:
      format->isFloat = false;
      format->isSigned = false;
      format->bitWidth = 32;
      break;

    default:
      return -1;
  }

  return 0;
}

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/

