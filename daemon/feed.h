/*$*********************************************************************\

Name            : -

Source File     : feed.h

Description     : Main include file for feed.c 

Comments        : -

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


#ifndef __FEED_H
#define __FEED_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <stdbool.h>
#include <pthread.h>
#include <curl/curl.h>
#include "audio.h"
#include "codec.h"
#include "fifo.h"


/*=========================================================================*\
       Some definitions 
\*=========================================================================*/
#define FeederAgentString "ickstream-agent/1.0"


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/

// A feeder instance
struct _audioFeed;
typedef struct _audioFeed AudioFeed;

// State of a feed (evaluate in callback!)
typedef enum {
  FeedInitialized,
  FeedConnecting,       // Includes reading header
  FeedConnected,        // Fd will now deliver data
  FeedTerminating,
  FeedTerminatedOk,     // Includes EOF or audioFeedDelete()
  FeedTerminatedError   // Includes broken connection
} AudioFeedState;

// Flags for a feed
typedef enum {
  FeedIcy            = 0x0001
} AudioFeedFlag;

/*------------------------------------------------------------------------*\
    Macros
\*------------------------------------------------------------------------*/
#define AUDIOFEEDISDONE(feed) (audioFeedGetState(feed)>FeedTerminating)

/*------------------------------------------------------------------------*\
    Signatures for function pointers
\*------------------------------------------------------------------------*/
typedef int (*AudioFeedCallback)( AudioFeed *feed, void *usrData );


/*=========================================================================*\
       Prototypes 
\*=========================================================================*/
AudioFeed      *audioFeedCreate( const char *uri, int flags, AudioFeedCallback callback, void *usrData );
int             audioFeedDelete( AudioFeed *feed, bool wait );
void            audioFeedLock( AudioFeed *feed );
void            audioFeedUnlock( AudioFeed *feed );
int             audioFeedLockWaitForConnection( AudioFeed *feed, int timeout );
const char     *audioFeedGetURI( AudioFeed *feed );
int             audioFeedGetFlags( AudioFeed *feed );
AudioFeedState  audioFeedGetState( AudioFeed *feed );
int             audioFeedGetFd( AudioFeed *feed );
const char     *audioFeedGetType( AudioFeed *feed );
long            audioFeedGetIcyInterval( AudioFeed *feed );
const char     *audioFeedGetResponseHeader( AudioFeed *feed );
char           *audioFeedGetResponseHeaderField( AudioFeed *feed, const char *fieldName );


#endif  /* __FEED_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

