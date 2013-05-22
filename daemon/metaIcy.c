/*$*********************************************************************\

Name            : -

Source File     : metaIcy.c

Description     : handle ICY meta data

Comments        : -

Called by       : codec and player

Calls           : -

Error Messages  : -
  
Date            : 04.04.2013

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

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <jansson.h>

#include "utils.h"


/*=========================================================================*\
    Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
    Macro and type definitions
\*=========================================================================*/
// none


/*=========================================================================*\
    Private symbols
\*=========================================================================*/
// none


/*=========================================================================*\
    Private prototypes
\*=========================================================================*/
static json_t *_getInteger( const char *str, const char *pbrk );
static json_t *_getReal( const char *str, const char *pbrk );
static int _segmentKeyValList( const char *str, json_t *jObj, const char *pbrk );


/*=========================================================================*\
    Parse HTTP header, will write all icy-*, ice-* plus some relevant HTTP
    elements to a JSON object. Type detection is done to identify numbers.
    Returns NULL on error or an object with key-value pairs for identified
    fields
\*=========================================================================*/
json_t *icyExtractHeaders( const char *httpHeader )
{
  json_t     *jObj;
  const char *keyPtr;

  DBGMSG( "icyExtractHeaders: \"%s\".", httpHeader );

/*------------------------------------------------------------------------*\
    Create result container
\*------------------------------------------------------------------------*/
  jObj = json_object();
  if( !jObj ) {
    logerr( "icyExtractHeaders: Out of memory." );
    return NULL;
  }
  json_object_set( jObj, "timestamp", json_real(srvtime()) );

/*------------------------------------------------------------------------*\
    Loop over all lines
\*------------------------------------------------------------------------*/
  for( keyPtr=httpHeader; keyPtr&&*keyPtr; keyPtr++ ) {
    const char *linePtr = strpbrk( keyPtr, "\n\r" );
    char *key           = NULL;
    const char *valPtr;
    json_t     *jVal = NULL;

    // Skip white spaces and line feeds
    if( *keyPtr=='\r' || *keyPtr=='\n' || *keyPtr==' ' || *keyPtr=='\t' )
      continue;

    // Get key name separator within this line, ignore lines without separator
    valPtr = strchr( keyPtr, ':' );
    if( !valPtr || valPtr>linePtr ) {
      keyPtr = linePtr;
      continue;
    }

    // Only consider header elements of interest
    if( strcmpprefix(keyPtr,"icy-") &&
        strcmpprefix(keyPtr,"ice-") &&
        strcmpprefix(keyPtr,"Content-Type:") &&
        strcmpprefix(keyPtr,"Server:") ) {
      keyPtr = linePtr;
      continue;
    }
    key = strndup( keyPtr, valPtr-keyPtr );

    // Skip separator and leading white spaces in value
    do
      valPtr++;
    while( *valPtr && (*valPtr==' ' || *valPtr=='\t') );

    // Special treatment for ice tag
    if( !strcmp(key,"ice-audio-info") ) {
      if( _segmentKeyValList(valPtr,jObj,linePtr)<0 ) {
        DBGMSG( "icyExtractHeaders: Cannot code entries for key \"%s\": \"%.*s\"",
                 key, linePtr-valPtr, valPtr );
      }
      goto next;
    }

    // No value
    if( !*valPtr || valPtr==linePtr )
      jVal = json_null();

    // Type detection of value: try integer
    if( !jVal )
      jVal =_getInteger( valPtr, linePtr );

    // Try float
    if( !jVal )
      jVal =_getReal( valPtr, linePtr );

    // Use string as it is, convert to UTF8
    if( !jVal )
      jVal = json_mkstring( valPtr, linePtr-valPtr );

    // Could not code this?
    if( !jVal ) {
      DBGMSG( "icyExtractHeaders: Cannot code entry for key \"%s\": \"%.*s\"",
               key, linePtr-valPtr, valPtr );
    }

    // Set value in target
    else {
      if( jVal && json_object_set(jObj,key,jVal) ) {
        logerr( "icyExtractHeaders: Cannot insert/set JSON for key \"%s\" in target.", key );
        Sfree( key );
        json_decref( jObj );
        return NULL;
      }
      DBGMSG( "icyExtractHeaders: Set entry for key \"%s\": \"%.*s\"", key, linePtr-valPtr, valPtr );
    }

    // Clean up and set pointer to next line separator
next:
    Sfree( key );
    keyPtr = linePtr;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return jObj;
}


/*=========================================================================*\
    Parse ICY inband data, will write all key/val pairs to result.
      Values are of string type.
    Returns NULL on error or an object with key-value pairs for identified
    fields
\*=========================================================================*/
json_t *icyParseInband( const char *icyInband )
{
  json_t *jObj;
  DBGMSG( "icyParseInband: \"%s\".", icyInband );

/*------------------------------------------------------------------------*\
    Create result container
\*------------------------------------------------------------------------*/
  jObj = json_object();
  if( !jObj ) {
    logerr( "icyExtractHeaders: Out of memory." );
    return NULL;
  }
  json_object_set( jObj, "timestamp", json_real(srvtime()) );

/*------------------------------------------------------------------------*\
    Use segmentation function
\*------------------------------------------------------------------------*/
  if( _segmentKeyValList(icyInband,jObj,NULL)<0 ) {
    logerr( "icyParseInband: Cannot parse message \"%s\".", icyInband );
    json_decref( jObj );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return jObj;
}


/*=========================================================================*\
    Check if str codes a double value (returns a JSON real in that case)
      NULL is also returned if after the number any character other than
      white spaces is found up to (excluding) pbrk.
\*=========================================================================*/
static json_t *_getReal( const char *str, const char *pbrk )
{
  char   *eptr;
  double  val;

  // Try to convert the string
  val = strtod( str, &eptr );

  // Conversion failed?
  if( eptr==str )
    return NULL;

  // Ignore trailing spaces
  while( *eptr && isspace(*eptr) && (!pbrk || eptr<pbrk) )
    eptr++;

  // Terminating character not allowed?
  if( *eptr && !isspace(*eptr) && pbrk && eptr!=pbrk )
    return NULL;

  // Convert to JSON and return
  return json_real( val );
}


/*=========================================================================*\
    Check if str codes a long value (returns a JSON integer in that case)
      NULL is also returned if after the number any character other than
      white spaces is found up to (excluding) pbrk.
\*=========================================================================*/
static json_t *_getInteger( const char *str, const char *pbrk )
{
  char   *eptr;
  long    val;

  // Try to convert the string
  val = strtol( str, &eptr, 10 );

  // Conversion failed?
  if( eptr==str )
    return NULL;

  // Ignore trailing spaces
  while( *eptr && isspace(*eptr) && (!pbrk || eptr<pbrk) )
    eptr++;

  // Terminating character not allowed?
  if( *eptr && !isspace(*eptr) && pbrk && eptr!=pbrk )
    return NULL;

  // Convert to JSON and return
  return json_integer( val );
}


/*=========================================================================*\
    Interpret a string of the form "key1=va11;...;keyn=valn[;]" up to
      (excluding) pbrk and insert JSON elements to jObj. Type detection is
      done to identify numbers. Existing elements are overwritten.
    Returns -1 on error or the number of elements processed
\*=========================================================================*/
static int _segmentKeyValList( const char *str, json_t *jObj, const char *pbrk )
{
  int     retval = 0;

  // Loop over string (range)
  while( str && *str && (!pbrk || str<pbrk) ) {
    const char *valPtr;
    char       *key;
    json_t     *jVal = NULL;

    // Get key name
    valPtr = strchr( str, '=' );
    if( !valPtr )
      break;
    key = strndup( str, valPtr-str );

    // Skip separator (equal sign) and leading white spaces in value
    do
      valPtr++;
    while( *valPtr && (*valPtr==' ' || *valPtr=='\t') );

    // No value
    if( !*valPtr || *valPtr== ';' ) {
      str = *valPtr ? valPtr+1 : valPtr;
      jVal = json_null();
    }

    // Value is of string type
    else if( *valPtr=='\'' ) {
      // there seems to be no escape mechanism for single quotes...
      str = strchr( ++valPtr, '\'' );
      if( !str || (pbrk && str>pbrk) ) {
        logwarn( "_segmentKeyValList: unterminated string fragment \"%s\".", valPtr );
        if( pbrk )
          str = pbrk;
      }
      if( !str )
        str = strchr( valPtr, 0 );
      jVal = json_mkstring( valPtr, str-valPtr );
    }

    // O.k. - this should be a number...
    else {
      str = strchr( valPtr, ';' );
      if( pbrk && (!str || str>pbrk) )
        str = pbrk;
      if( !str )
        str = strchr( valPtr, 0 );
      jVal = _getInteger( valPtr, str );
      if( !jVal )
        jVal = _getReal( valPtr, str );
      if( !jVal )
        logwarn( "_segmentKeyValList: Expected number, but got \"%.*s\" (key: \"%s\"",
                 str-valPtr, valPtr, key );
    }

    // Store JSON element
    if( jVal && json_object_set(jObj,key,jVal) ) {
      logerr( "_segmentKeyValList: Cannot insert/set JSON for key \"%s\" in target.", key );
      Sfree( key );
      return -1;
    }
    if( jVal ) {
      retval++;
      DBGMSG( "_segmentKeyValList: Set entry for key \"%s\": \"%.*s\"",
               key, str-valPtr, valPtr );
    }
    else {
      DBGMSG( "_segmentKeyValList: Cannot code entry for key \"%s\": \"%.*s\"",
              key, str-valPtr, valPtr );
    }

    // Clean up and skip separator
    Sfree( key );
    while( str && *str==';' )
      str++;
  }

  // Return number of elements
  return retval;
}



/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
