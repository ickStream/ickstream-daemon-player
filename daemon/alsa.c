/*$*********************************************************************\

Name            : -

Source File     : alsa.c

Description     : interface to alsa API 

Comments        : -

Called by       : audio module 

Calls           : 

Error Messages  : -
  
Date            : 26.02.2013

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
#include <strings.h>
#include <alsa/asoundlib.h>

#include "ickpd.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
// none

/*=========================================================================*\
      Get all ALSA pcm devices 
        descrListPtr might be NULL;
\*=========================================================================*/
int alsaGetDeviceList( char ***deviceListPtr, char ***descrListPtr )
{
  void **hints; 
  void **hintPtr;
  int    retval;
  
/*------------------------------------------------------------------------*\
    Reset results 
\*------------------------------------------------------------------------*/
  *deviceListPtr = NULL;
  if( descrListPtr )
    *descrListPtr = NULL;
    
/*------------------------------------------------------------------------*\
    Try to get hints 
\*------------------------------------------------------------------------*/
  if( snd_device_name_hint(-1,"pcm",&hints)<0 ) {
    srvmsg( LOG_ERR, "ALSA: Error searching for pcm devices." ); 
    return -1; 
  }	

/*------------------------------------------------------------------------*\
    Count hints and allocate list storage
\*------------------------------------------------------------------------*/
  for( hintPtr=hints,retval=0; *hintPtr; hintPtr++ )
  	retval++;
  *deviceListPtr = calloc( retval+1, sizeof(*deviceListPtr) );
  if( descrListPtr )
    *descrListPtr = calloc( retval+1, sizeof(*descrListPtr) );
    
/*------------------------------------------------------------------------*\
    Loop over all hints 
\*------------------------------------------------------------------------*/
  for( hintPtr=hints,retval=0; *hintPtr; hintPtr++ ) {
    char *str;
    
    // Get and check name
    str = snd_device_name_get_hint( *hintPtr, "NAME" );
    if( !str ) {
       srvmsg( LOG_ERR, "ALSA: Found device without name." ); 
       continue;
    }
  	(*deviceListPtr)[retval] = str;
  	
  	// Get and check description text
  	if( descrListPtr ) {
  	  str = snd_device_name_get_hint( *hintPtr, "DESC" );
  	  if( !str )
  	    str = strdup( "" );  
      (*descrListPtr)[retval] = str;
  	}
  	
  	retval++;    
  }
  
/*------------------------------------------------------------------------*\
    return number of devices found with name
\*------------------------------------------------------------------------*/
  return retval;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
