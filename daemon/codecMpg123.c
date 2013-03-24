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

#undef DEBUG

#include <stdio.h>
#include <string.h>
#include <strings.h>
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
const char *defaultFormatStr[] = {
  "2x44100x16S",
  NULL
};

/*=========================================================================*\
	Private prototypes
\*=========================================================================*/
static int    _codecInit( Codec *codec );
static int    _codecShutdown( Codec *codec, bool force );
static bool   _codecCheckType(const char *type, const AudioFormat *format );
static int    _codecNewInstance( CodecInstance *instance ); 
static int    _codecDeleteInstance( CodecInstance *instance ); 
static int    _codecAcceptInput( CodecInstance *instance, void *data, size_t length, size_t *accepted );  
static int    _codecDeliverOutput( CodecInstance *instance, void *data, size_t maxLength, size_t *realSize );
static int    _codecSetVolume( CodecInstance *instance, double volume, bool muted );
static int    _codecGetSeekTime( CodecInstance *instance, double *pos );  

static enum mpg123_enc_enum _getMpg123Format( const AudioFormat *format );


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
  codec.acceptInput    = &_codecAcceptInput;
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
  int i, rc;
  
/*------------------------------------------------------------------------*\
    Try to init lib 
\*------------------------------------------------------------------------*/
  rc = mpg123_init();
  if( rc!=MPG123_OK ) {
    logerr( "mpg123: could not init lib: %s", 
                      mpg123_plain_strerror(rc)  );
    return -1;	
  }

/*------------------------------------------------------------------------*\
    Add default audio formats
\*------------------------------------------------------------------------*/
  for( i=0; defaultFormatStr[i]; i++ ) {
    AudioFormat format;
    audioStrFormat( &format ,defaultFormatStr[i] );
    audioAddAudioFormat( &codec->defaultAudioFormats, &format );
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
    Delete list of default audio formats
\*------------------------------------------------------------------------*/
  audioFreeAudioFormatList( &codec->defaultAudioFormats );

/*------------------------------------------------------------------------*\
    That#s all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
      Check if codec supports audio type and format 
\*=========================================================================*/
static bool _codecCheckType(const char *type, const AudioFormat *format )
{
  enum mpg123_enc_enum  mpg123Format;
  const long           *rateList;
  const int            *encList;
  size_t                listLen, i;

  // type not supported?
  if( strcmp(type,"mp3") && strcmp(type,"audio/mpeg") )
    return false;    

  // Check number of channels (only mono and stereo)
  if( format->channels!=1 && format->channels!=2 )
    return false; 

  // Check sample rate
  mpg123_rates( &rateList, &listLen );
  for( i=0; i<listLen && rateList[i]!=format->sampleRate; i++ )
    ;
  if( i==listLen )
    return false;   

  // Check encoding
  mpg123Format = _getMpg123Format( format );
  mpg123_encodings( &encList, &listLen );
  for( i=0; i<listLen && encList[i]!=mpg123Format; i++ )
    ;
  if( i==listLen )
    return false;

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
  
/*------------------------------------------------------------------------*\
    Get libarary handle 
\*------------------------------------------------------------------------*/
  mh = mpg123_new( NULL, &rc );   
  if( !mh ) {
    logerr( "mpg123: could not init instance: %s", 
                      mpg123_plain_strerror(rc)  );
    return -1;	
  }
  
/*------------------------------------------------------------------------*\
    Start decoder 
\*------------------------------------------------------------------------*/
  rc = mpg123_open_feed( mh );
  if( rc!=MPG123_OK ) {
    logerr( "mpg123: could not open feed: %s", 
                      mpg123_plain_strerror(rc)  );
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
  int            rc;
  
/*------------------------------------------------------------------------*\
    No library handle?
\*------------------------------------------------------------------------*/
  if( !mh )
    return 0;
  instance->instanceData = NULL;
      
/*------------------------------------------------------------------------*\
    Close data source
\*------------------------------------------------------------------------*/
  rc = mpg123_close( mh );
  if( rc!=MPG123_OK ) {
    logerr( "mpg123: could not close handle: %s", 
                      mpg123_plain_strerror(rc)  );
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
      Feed input to codec 
        This can block until new data can be processed (input buffer full)
\*=========================================================================*/
static int _codecAcceptInput( CodecInstance *instance, void *data, size_t length, size_t *accepted )
{
  mpg123_handle *mh = (mpg123_handle*)instance->instanceData;    
  int            rc;
  
/*------------------------------------------------------------------------*\
   No data accepted yet
\*------------------------------------------------------------------------*/  
  *accepted = 0;
  
/*------------------------------------------------------------------------*\
   Loop until done or error 
\*------------------------------------------------------------------------*/  
  do {
    pthread_mutex_lock( &instance->mutex );
    rc = mpg123_feed( mh, data, length );
    pthread_mutex_unlock( &instance->mutex );
    DBGMSG( "mpg123: accepted data (%ld bytes): %s", 
                       (long)length, mpg123_plain_strerror(rc)  );

/*------------------------------------------------------------------------*\
    Interpret result
\*------------------------------------------------------------------------*/  
    switch( rc ) {
  
      // Everything fine
      case MPG123_OK:          
        *accepted = length;
        break;
      
      // Not enough space - block and wait ...
      case MPG123_NO_SPACE:
        sleep( 1 );
        rc = MPG123_OK;
        break;	
  
      // Report real error 
      default:
        logerr( "mpg123: could not accept data (%ld bytes): %s", 
                         (long)length, 
                         rc==MPG123_ERR?mpg123_strerror(mh):mpg123_plain_strerror(rc) );
        return -1;
    }
  } while (rc!=MPG123_OK);
  
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/  
  return 0;
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
  int            rc;
  int            err = 0;
  
/*------------------------------------------------------------------------*\
    Get data from decoder
\*------------------------------------------------------------------------*/  
  *realSize = 0;
  pthread_mutex_lock( &instance->mutex );
  rc = mpg123_read( mh, data, maxLength, realSize );  
  pthread_mutex_unlock( &instance->mutex );
  
  DBGMSG( "mpg123: delivered data (%ld/%ld bytes): %s", 
                     (long)*realSize, (long)maxLength, mpg123_plain_strerror(rc)  );
  
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
     if( !instance->endOfInput )
        break;

    // End of track: set status and call player callback
    case MPG123_DONE:
      instance->state = CodecEndOfTrack;
      pthread_cond_signal( &instance->condEndOfTrack );		
      break;
              	
    // Format detected or changed: inform player via call back
    case MPG123_NEW_FORMAT:
      if( instance->metaCallback ) {
      	AudioFormat format;
      	json_t      *meta = NULL;
      	// Collect data
      	// Fixme...
      	
      	// and deliver...
      	instance->metaCallback( instance, &format, meta );
      }	
      break;


    // Report real error 
    default:
      logerr( "mpg123: could not deliver data (avail %ld bytes): %s", 
                       (long)maxLength, 
                       rc==MPG123_ERR?mpg123_strerror(mh):mpg123_plain_strerror(rc)  );
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
             volume, muted?"(muted)":"(unmuted)", mpg123_plain_strerror(rc)  );

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
                                    END OF FILE
\*=========================================================================*/

