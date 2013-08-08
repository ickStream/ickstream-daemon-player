/*$*********************************************************************\

Name            : -

Source File     : ickScrobble.c

Description     : ickstream scrobble service

Comments        : -

Called by       : player

Calls           : 

Error Messages  : -
  
Date            : 12.04.2013

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

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <jansson.h>

#include "ickutils.h"
#include "player.h"
#include "ickService.h"
#include "ickCloud.h"
#include "ickScrobble.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
// none


/*=========================================================================*\
    Announce a track played to all scrobble services
\*=========================================================================*/
int ickScrobbleTrack( PlaylistItem *item, double seekPos )
{
  const char      *token;
  json_t          *jParams;
  double           duration;
  ServiceListItem *service;

  DBGMSG( "ickScrobbleTrack (%p,%s): seekPos=%.2lfs.",
          item, playlistItemGetText(item), seekPos );

/*------------------------------------------------------------------------*\
    Need token...
\*------------------------------------------------------------------------*/
  token = ickCloudGetAccessToken();
  if( !token ) {
    logwarn( "ickScrobbleTrack (%s): No device token set.",
             playlistItemGetText(item) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Any scrobble service available?
\*------------------------------------------------------------------------*/
  service = ickServiceFind( NULL, NULL, "scrobble", 0 );
  if( !service ) {
    return 0;
  }

/*------------------------------------------------------------------------*\
    Build scrobble message
\*------------------------------------------------------------------------*/
  jParams = json_object();

  // Add timestamp
  json_object_set( jParams, "occurrenceTimestamp", json_real(srvtime()) );

  // If possible calculate and add percentage to method parameters
  duration = playlistItemGetDuration( item );
  if( seekPos>=0 && duration>0 )
    json_object_set( jParams, "playedPercentage",
                     json_integer((int)(seekPos*100/duration+.5)) );

  // Add playlist item info itself to method parameters
  json_object_set( jParams, "track", playlistItemGetJSON(item) );

/*------------------------------------------------------------------------*\
    Loop over all scrobble services
\*------------------------------------------------------------------------*/
  for( ; service; service=ickServiceFind(service,NULL,"scrobble",0) ) {
    const char *uri;
    int         rc;

    DBGMSG( "ickScrobbleTrack (%p,%s): Using service \"%s\" (%s).",
             item,  playlistItemGetText(item),
             ickServiceGetName(service), ickServiceGetId(service) );

    // Get service URI
    uri = ickServiceGetURI( service );
    if( !uri ) {
      logerr( "ickScrobbleTrack (%s): No endpoint defined for service \"%s\" (%s).",
               playlistItemGetText(item), ickServiceGetName(service), ickServiceGetId(service) );
      continue;
    }

    // Fire off request, ignore result
    rc = ickCloudRequestAsync( uri, token, "playedTrack", jParams, NULL, NULL );
    if( rc ) {
      logerr( "ickScrobbleTrack (%s): Could not send to service \"%s\" (%s) - %d.",
          playlistItemGetText(item), ickServiceGetName(service), ickServiceGetId(service), rc );
      continue;
    }

  }

/*------------------------------------------------------------------------*\
    Clean up, that's it
\*------------------------------------------------------------------------*/
  json_decref( jParams );
  return 0;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
