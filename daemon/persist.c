/*$*********************************************************************\

Name            : -

Source File     : persist.c

Description     : audio control 

Comments        : Manager for persistent value storage

Called by       : - 

Calls           : jansson lib

Error Messages  : -
  
Date            : 24.02.2013

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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <jansson.h>

#include "utils.h"
#include "persist.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static char   *repositoryFileName;
static json_t *jRepository;

static int _dumpRepository( const char *name );
void       _freeRepository( void );  
static int _readRepository( const char *name );


/*=========================================================================*\
      Set Filename to store repository 
        An existing repository is not cleared...
\*=========================================================================*/
int persistSetFilename( const char *name )
{
  struct stat buf;
  
  DBGMSG( "persistSetFilename: \"%s\"", name ); 
  
/*------------------------------------------------------------------------*\
    Defensively dump to an existing file name
\*------------------------------------------------------------------------*/
  if( repositoryFileName && jRepository )
    _dumpRepository( repositoryFileName );

/*------------------------------------------------------------------------*\
    Try to read content from existing file
\*------------------------------------------------------------------------*/
  if( !stat(name,&buf) && _readRepository(name) )
    return -1;

/*------------------------------------------------------------------------*\
    Create empty repository if nevessary
\*------------------------------------------------------------------------*/
  if( !jRepository )
    jRepository = json_object();
  if( !jRepository ) {
    srvmsg( LOG_ERR, "Could not create repository object." );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Save new name
\*------------------------------------------------------------------------*/
  Sfree( repositoryFileName );
  repositoryFileName = strdup( name );
  return 0;    
}


/*=========================================================================*\
      Shutdown module, Free all memory
\*=========================================================================*/
void persistShutdown( void )
{
  srvmsg( LOG_INFO, "Shutting down persistency module..." ); 

/*------------------------------------------------------------------------*\
    Dump to file a last time
\*------------------------------------------------------------------------*/
  if( repositoryFileName && jRepository )
    _dumpRepository( repositoryFileName ); 
  
/*------------------------------------------------------------------------*\
    Free filename
\*------------------------------------------------------------------------*/
  Sfree( repositoryFileName ); 

/*------------------------------------------------------------------------*\
    Free JSON repository in memory
\*------------------------------------------------------------------------*/
  _freeRepository();

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}

/*=========================================================================*\
      Store JSON value in repository 
\*=========================================================================*/
int persistSetJSON( const char *key, json_t *jObj )
{

/*------------------------------------------------------------------------*\
    Add or replace value in repositiory
\*------------------------------------------------------------------------*/
  if( persistSetJSON_new(key,jObj) )
    return -1;

/*------------------------------------------------------------------------*\
    Compensate stolen reference, that's all
\*------------------------------------------------------------------------*/
  json_incref( jObj );  
  return 0;  
}


/*=========================================================================*\
      Store JSON value in repository, steal reference 
\*=========================================================================*/
int persistSetJSON_new( const char *key, json_t *value )
{
  DBGMSG( "persistSetJSON_new: (%s)", key ); 
   
/*------------------------------------------------------------------------*\
    Create repository if not available
\*------------------------------------------------------------------------*/
  if( !jRepository ) {
    srvmsg( LOG_WARNING, "Set value in uninitialized repository: \"%s\"", 
                         key );
    jRepository = json_object();
  }
  if( !jRepository ) {
    srvmsg( LOG_ERR, "Could not create repository object." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Store or replace value in repository, steal reference
\*------------------------------------------------------------------------*/
  if( json_object_set(jRepository,key,value) ) {
    srvmsg( LOG_ERR, "Cannot add vlaue for key \"%s\" to repository.", key );
    return -1;  
  }
  
/*------------------------------------------------------------------------*\
    Dump repository, that's it
\*------------------------------------------------------------------------*/
  return _dumpRepository( repositoryFileName );
}


/*=========================================================================*\
      Store string value in repository 
\*=========================================================================*/
int persistSetString( const char *key, const char *value )
{
  json_t *jObj;

  DBGMSG( "persistSetString: (%s)=\"%s\"", key, value ); 
    
/*------------------------------------------------------------------------*\
    Convert value to JSON
\*------------------------------------------------------------------------*/
  jObj = json_string( value );
  if( !jObj ) {
    srvmsg( LOG_ERR, "Cannot convert string to JSON: \"%s\"", value );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Add or replace value in repositiory, steal reference
\*------------------------------------------------------------------------*/
  return persistSetJSON_new( key, jObj );
}


/*=========================================================================*\
      Store integer value in repository 
\*=========================================================================*/
int persistSetInteger( const char *key, int value )
{
  json_t *jObj;

  DBGMSG( "persistSetInteger: (%s)=%d", key, value ); 
    
/*------------------------------------------------------------------------*\
    Convert value to JSON
\*------------------------------------------------------------------------*/
  jObj = json_integer( value );
  if( !jObj ) {
    srvmsg( LOG_ERR, "Cannot convert integer to JSON: %d", value );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Add or replace value in repositiory
\*------------------------------------------------------------------------*/
  return persistSetJSON_new( key, jObj );
}


/*=========================================================================*\
      Store double float value in repository 
\*=========================================================================*/
int persistSetReal( const char *key, double value )
{
  json_t *jObj;

  DBGMSG( "persistSetreal: (%s)=%g", key, value ); 
    
/*------------------------------------------------------------------------*\
    Convert value to JSON
\*------------------------------------------------------------------------*/
  jObj = json_real( value );
  if( !jObj ) {
    srvmsg( LOG_ERR, "Cannot convert double to JSON: %lg", value );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Add or replace value in repositiory
\*------------------------------------------------------------------------*/
  return persistSetJSON_new( key, jObj );
}


/*=========================================================================*\
      Store bool value in repository 
\*=========================================================================*/
int persistSetBool( const char *key, bool value )
{
  json_t *jObj;
    
  DBGMSG( "persistSetBool: (%s)=%s", key, value?"True":"False" ); 

/*------------------------------------------------------------------------*\
    Convert value to JSON
\*------------------------------------------------------------------------*/
  jObj = value ? json_true() : json_false();
  if( !jObj ) {
    srvmsg( LOG_ERR, "Cannot convert boolean to JSON: %d", value );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Add or replace value in repositiory
\*------------------------------------------------------------------------*/
  return persistSetJSON_new( key, jObj );
}


/*=========================================================================*\
      Remove a key:value entry from repository
        return -1 if key was not found
\*=========================================================================*/
int persistRemove( const char *key )
{

  DBGMSG( "persistRemove: (%s)", key ); 

/*------------------------------------------------------------------------*\
    Repository needs to be available
\*------------------------------------------------------------------------*/
  if( !jRepository ) {
    srvmsg( LOG_WARNING, "Try to remove key \"%s\" from uninitialized repository.", 
                          key );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Remove entry
\*------------------------------------------------------------------------*/
  return json_object_del( jRepository, key );
}


/*=========================================================================*\
      Get JSON object from repository 
         Returns a borrowed reference or 
                 NULL on error or if key is not found
\*=========================================================================*/
json_t *persistGetJSON( const char *key )
{    

/*------------------------------------------------------------------------*\
    Repository needs to be available
\*------------------------------------------------------------------------*/
  if( !jRepository ) {
    srvmsg( LOG_WARNING, "Get value for key \"%s\" in uninitialized repository.", 
                          key );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Lookup
\*------------------------------------------------------------------------*/
  json_t *jObj = json_object_get( jRepository, key );
  DBGMSG( "persistGetJSON: (%s)=%p", key, jObj ); 
  return jObj;
}


/*=========================================================================*\
      Get String object from repository 
         Returns NULL on error or if key is not found
         String reference is only good until the key or repository changes!
         So you might copy it for longer usage. 
\*=========================================================================*/
const char *persistGetString( const char *key )
{
  json_t *jObj;
    
/*------------------------------------------------------------------------*\
    Get Object
\*------------------------------------------------------------------------*/
  jObj = persistGetJSON( key );
  if( !jObj ) {
  	DBGMSG( "persistGetString: (%s) not set", key );
    return NULL;  
  }


/*------------------------------------------------------------------------*\
    Check type
\*------------------------------------------------------------------------*/
  if( !json_is_string(jObj) ) {
     srvmsg( LOG_WARNING, "Value for key \"%s\" is not a string (%d): ", 
                          key, json_typeof(jObj) );
     return NULL;
  }  

/*------------------------------------------------------------------------*\
    return string value as reference
\*------------------------------------------------------------------------*/
  const char *value = json_string_value( jObj );
  DBGMSG( "persistGetString: (%s)=\"%s\"", key, value?value:"" ); 
  return value;
}


/*=========================================================================*\
      Get integer number from repository 
         return 0 on error of if key is not found
\*=========================================================================*/
int persistGetInteger( const char *key )
{
  json_t *jObj;
    
/*------------------------------------------------------------------------*\
    Get Object
\*------------------------------------------------------------------------*/
  jObj = persistGetJSON( key );
  if( !jObj ) {
  	DBGMSG( "persistGetInteger: (%s) not set", key );
    return 0;  
  }

/*------------------------------------------------------------------------*\
    Check type
\*------------------------------------------------------------------------*/
  if( !json_is_integer(jObj) ) {
     srvmsg( LOG_WARNING, "Value for key \"%s\" is not an integer (%d): ", 
             key, json_typeof(jObj) );
     return 0;
  }  

/*------------------------------------------------------------------------*\
    return integer value 
\*------------------------------------------------------------------------*/
  int value = json_integer_value( jObj );  
  DBGMSG( "persistGetInteger: (%s)=%d", key, value ); 
  return value;
}


/*=========================================================================*\
      Get real number from repository 
         return 0.0 on error of if key is not found
\*=========================================================================*/
double persistGetReal( const char *key )
{
  json_t *jObj;
    
/*------------------------------------------------------------------------*\
    Get Object
\*------------------------------------------------------------------------*/
  jObj = persistGetJSON( key );
  if( !jObj ) {
  	DBGMSG( "persistGetReal: (%s) not set", key );
    return 0;  
  }

/*------------------------------------------------------------------------*\
    Check type
\*------------------------------------------------------------------------*/
  if( !json_is_real(jObj) ) {
     srvmsg( LOG_WARNING, "Value for key \"%s\" is not a real number (%d): ", 
                          key, json_typeof(jObj) );
     return 0;
  }  

/*------------------------------------------------------------------------*\
    return real value 
\*------------------------------------------------------------------------*/
  double value = json_real_value( jObj );
  DBGMSG( "persistGetReal: (%s)=%g", key, value ); 
  return value;
}


/*=========================================================================*\
      Get boolean value from repository 
         return false on error of if key is not found
\*=========================================================================*/
bool persistGetBool( const char *key )
{
  json_t *jObj;

  DBGMSG( "persistGetBool: (%s)", key ); 
    
/*------------------------------------------------------------------------*\
    Get Object
\*------------------------------------------------------------------------*/
  jObj = persistGetJSON( key );
  if( !jObj ) {
  	DBGMSG( "persistGetBool: (%s) not set", key );
    return false;  
  }
  
/*------------------------------------------------------------------------*\
    Check type
\*------------------------------------------------------------------------*/
  if( !json_is_true(jObj) && !json_is_false(jObj) ) {
     srvmsg( LOG_WARNING, "Value for key \"%s\" is not a boolean (%d): ", 
                          key, json_typeof(jObj) );
     return false;
  }  

/*------------------------------------------------------------------------*\
    return real value 
\*------------------------------------------------------------------------*/
  bool value = json_is_true( jObj );
  DBGMSG( "persistGetBool: (%s)=%s", key, value?"True":"False" ); 
  return value;
}


/*=========================================================================*\
      Write repository to file
\*=========================================================================*/
static int _dumpRepository( const char *name )
{
  int retcode = 0;
  
  DBGMSG( "Dumping persistency file: \"%s\"", name ); 

/*------------------------------------------------------------------------*\
    No name given? 
\*------------------------------------------------------------------------*/
  if( !name ) {
    srvmsg( LOG_ERR, "Cannot write persistent repository: no name set " );
    return -1;
  }

/*------------------------------------------------------------------------*\
    No repository in mamory? 
\*------------------------------------------------------------------------*/
  if( !jRepository ) {
    srvmsg( LOG_ERR, "Try to dump empty repository object." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Use toolbox function 
\*------------------------------------------------------------------------*/
  if( json_dump_file(jRepository,name,JSON_COMPACT) ) {
    srvmsg( LOG_ERR, "Error writing to persistent repository \"%s\": %s ", 
                     name, strerror(errno) );
    retcode = -1;  
  }

/*------------------------------------------------------------------------*\
    Try to change file mode in any case (might conatain secret info) 
\*------------------------------------------------------------------------*/
  if( chmod(name,S_IRUSR|S_IWUSR) ) {
    srvmsg( LOG_ERR, "Could not chmod persistent repository \"%s\": %s ", 
                     name, strerror(errno) );
    retcode = -1; 
  }
  
/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
  return retcode;
}

   
/*=========================================================================*\
      Free repository in memory
\*=========================================================================*/
void _freeRepository( void )
{
	
  if( jRepository ) {
    if( jRepository->refcount>1 )
      srvmsg( LOG_WARNING, "Refcount for jRepository is %d before deletion.", 
                           jRepository->refcount );    
    json_decref( jRepository ); 
  }
  jRepository = NULL;	

}   

   
/*=========================================================================*\
      read repository from file
\*=========================================================================*/
static int _readRepository( const char *name )
{
  json_t       *jObj;  
  json_error_t  error;
  
  DBGMSG( "Reading persistency file: \"%s\"", name ); 

/*------------------------------------------------------------------------*\
    No name given? 
\*------------------------------------------------------------------------*/
  if( !name ) {
    srvmsg( LOG_ERR, "Cannot read persistent repository: no name set " );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Try to read from file 
\*------------------------------------------------------------------------*/
  jObj = json_load_file( name, JSON_REJECT_DUPLICATES, &error );
  if( !jObj ) {
    srvmsg( LOG_ERR, "Cannot read persistent repository %s: currupt line %d: %s", 
                    name, error.line, error.text );
    return -1;                
  } 

/*------------------------------------------------------------------------*\
    Replace repository in memeory 
\*------------------------------------------------------------------------*/
  _freeRepository( );
  jRepository = jObj;
      
/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}





/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
