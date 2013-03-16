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

// #undef DEBUG

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <jansson.h>
#include <mpg123.h>

#include "utils.h"
#include "codec.h"
#include "codecMpg123.h"



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
static int    _codecInit( void );
static int    _codecShutdown( bool force );
static bool   _codecCheckType(const char *type, const struct _audioFormat *format );
static int    _codecNewInstance( CodecInstance *instance ); 
static int    _codecDeleteInstance( CodecInstance *instance ); 
static int    _codecAcceptInput( CodecInstance *instance, void *data, size_t length, size_t *accepted );  
static int    _codecDeliverOutput( CodecInstance *instance, void *data, size_t maxLength, size_t *realSize );
static int    _codecSetVolume( CodecInstance *instance, double volume );
static int    _codecGetSeekTime( CodecInstance *instance, double *pos );  


/*=========================================================================*\
      return descriptor for this codec 
\*=========================================================================*/
Codec *mpg123Descriptor( void )
{
  static Codec codec;
  
  // Set name	
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
  
  return &codec;	
}

/*=========================================================================*\
      Global init for codec lib 
        return false on error
\*=========================================================================*/
static int _codecInit( void )
{
  int rc;
  
/*------------------------------------------------------------------------*\
    Try to init lib 
\*------------------------------------------------------------------------*/
  rc = mpg123_init();
  if( rc!=MPG123_OK ) {
    srvmsg( LOG_ERR, "mpg123: could not init lib: %s", 
                      mpg123_plain_strerror(rc)  );
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
static int _codecShutdown( bool force )
{
  mpg123_exit();
  return 0;
}


/*=========================================================================*\
      Check if codec supports audio type and format 
\*=========================================================================*/
static bool _codecCheckType(const char *type, const struct _audioFormat *format )
{

  if( !strcmp(type,"mp3") )
    return true;    

  if( !strcmp(type,"audio/mpeg") )
    return true;    

  // type not supported
  return false;    
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
    srvmsg( LOG_ERR, "mpg123: could not init instance: %s", 
                      mpg123_plain_strerror(rc)  );
    return -1;	
  }
  
/*------------------------------------------------------------------------*\
    Start decoder 
\*------------------------------------------------------------------------*/
  rc = mpg123_open_feed( mh );
  if( rc!=MPG123_OK ) {
    srvmsg( LOG_ERR, "mpg123: could not open feed: %s", 
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
    srvmsg( LOG_ERR, "mpg123: could not close handle: %s", 
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
        srvmsg( LOG_ERR, "mpg123: could not accept data (%ld bytes): %s", 
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
      	instance->metaCallback( (struct _codecinstance*)instance, &format, meta );
      }	
      break;


    // Report real error 
    default:
      srvmsg( LOG_ERR, "mpg123: could not deliver data (avail %ld bytes): %s", 
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
static int _codecSetVolume( CodecInstance *instance, double volume )
{
  mpg123_handle *mh = (mpg123_handle*)instance->instanceData;
  int            rc;

/*------------------------------------------------------------------------*\
    Call library
\*------------------------------------------------------------------------*/ 
  pthread_mutex_lock( &instance->mutex ); 
  rc = mpg123_volume( mh, volume );
  pthread_mutex_unlock( &instance->mutex );
  if( rc )
    srvmsg( LOG_ERR, "mpg123: could not set volume to %.2lf%%: %s", 
                     volume*100, mpg123_plain_strerror(rc)  );
                       
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
                                    END OF FILE
\*=========================================================================*/