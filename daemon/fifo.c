/*$*********************************************************************\

Name            : -

Source File     : fifo.c

Description     : fifo ringbuffer for audio data 

Comments        : -

Called by       : audio module 

Calls           : 

Error Messages  : -
  
Date            : 28.02.2013

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
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>

#include "utils.h"
#include "fifo.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none

/*=========================================================================*\
	Private symbols
\*=========================================================================*/
// none


/*=========================================================================*\
      Create a ring buffer 
        name is optional and might be NULL
        returns NULL on error
\*=========================================================================*/
Fifo *fifoCreate( const char *name, size_t size )
{
  Fifo *fifo;

/*------------------------------------------------------------------------*\
    Allocate and init header 
\*------------------------------------------------------------------------*/
  fifo = calloc( 1, sizeof(Fifo) );
  if( !fifo ) {
  	logerr( "fifoCreate: out of memory!" );
    return NULL;
  }
  if( name ) {
    fifo->name = strdup( name );
    if( !fifo->name ) {
      Sfree( fifo );
      logerr( "fifoCreate: out of memory!" );
      return NULL;
    }
  }
  
/*------------------------------------------------------------------------*\
    Allocate buffer and init pointers 
\*------------------------------------------------------------------------*/
  fifo->size          = size;
  fifo->lowWatermark  = size;
  fifo->highWatermark = 0;
  fifo->buffer = malloc( size );
  if( !fifo->buffer ) {
    Sfree( fifo->name );
    Sfree( fifo );
    logerr( "fifoCreate: out of memory!" );
    return NULL;
  }
  fifo->readp  = fifo->buffer;
  fifo->writep = fifo->buffer;
  fifo->isFull = false;

/*------------------------------------------------------------------------*\
    init mutex and conditions 
\*------------------------------------------------------------------------*/
  pthread_mutex_init( &fifo->mutex, NULL );
  pthread_cond_init( &fifo->condIsWritable, NULL );
  pthread_cond_init( &fifo->condIsReadable, NULL );

/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/
  DBGMSG( "Fifo \"%s\" initialized (%ld bytes): %p", 
                     name?name:"<unknown>", (long)size, fifo );
  return fifo;
}


/*=========================================================================*\
      Free a ring buffer ressource 
\*=========================================================================*/
void fifoDelete( Fifo *fifo )
{
  DBGMSG( "Fifo %p (%s, %ld bytes) freed", fifo,
                     fifo->name?fifo->name:"<unknown>", (long)fifo->size );

/*------------------------------------------------------------------------*\
    Delete mutex and conditions 
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &fifo->mutex );
  pthread_cond_destroy( &fifo->condIsWritable );
  pthread_cond_destroy( &fifo->condIsReadable );

/*------------------------------------------------------------------------*\
    Free buffer, name (if any) and header 
\*------------------------------------------------------------------------*/
  Sfree( fifo->buffer );
  Sfree( fifo->name );
  Sfree( fifo );
}


/*=========================================================================*\
      Reset fifo 
\*=========================================================================*/
void fifoReset( Fifo *fifo )
{
  DBGMSG( "Fifo %p (%s, %ld bytes) reset", fifo,
                     fifo->name?fifo->name:"<unknown>", (long)fifo->size );

/*------------------------------------------------------------------------*\
    Lock fifo
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &fifo->mutex );

/*------------------------------------------------------------------------*\
    reset pointers and state
\*------------------------------------------------------------------------*/
  fifo->readp  = fifo->buffer;
  fifo->writep = fifo->buffer;
  fifo->isFull = false;

/*------------------------------------------------------------------------*\
    Unlock fifo
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &fifo->mutex );
}


/*=========================================================================*\
      Lock fifo to avoid concurrent modifications
\*=========================================================================*/
void fifoLock( Fifo *fifo )
{
  pthread_mutex_lock( &fifo->mutex );	
  DBGMSG( "Fifo %p (%s): locked.", 
                      fifo, fifo->name?fifo->name:"<unknown>" ); 
}


/*=========================================================================*\
      Lock fifo and wait for writable (usedSize<lowWatermark) condition
        timeout is in ms, 0 or a negative values are treated as infinity
        returns 0 and locks fifo, if condition is met
                std. errode (ETIMEDOUT in case of timeout) and no locking otherwise 
\*=========================================================================*/
int fifoLockWaitWritable( Fifo *fifo, int timeout )
{
  struct timeval  now;
  struct timespec abstime;
  int             err = 0;

  DBGMSG( "Fifo %p (%s): waiting for writable: used=%ld <? low mark=%ld, timeout %dms", 
          fifo, fifo->name?fifo->name:"<unknown>",
          (long)fifoGetSize(fifo,FifoTotalUsed), (long)fifo->lowWatermark, timeout ); 

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   pthread_mutex_lock( &fifo->mutex );
  
/*------------------------------------------------------------------------*\
    Get absolut timestamp for timeout
\*------------------------------------------------------------------------*/
  if( timeout>0 ) {
    gettimeofday( &now, NULL );
    abstime.tv_sec  = now.tv_sec + timeout/1000;
    abstime.tv_nsec = now.tv_usec*1000UL +(timeout%1000)*1000UL*1000UL;
    if( abstime.tv_nsec>1000UL*1000UL*1000UL ) {
      abstime.tv_nsec -= 1000UL*1000UL*1000UL;
      abstime.tv_sec++;
    }
  }

/*------------------------------------------------------------------------*\
    Loop while condtion is not met (cope with "spurious  wakeups")
\*------------------------------------------------------------------------*/
  while( !FifoIsWritable(fifo) ) {

    // wait for condition
    err = timeout>0 ? pthread_cond_timedwait( &fifo->condIsWritable, &fifo->mutex, &abstime )
                    : pthread_cond_wait( &fifo->condIsWritable, &fifo->mutex );
    
    // Break on errors
    if( err )
      break; 
  }
  
/*------------------------------------------------------------------------*\
    In case of error: unlock mutex
\*------------------------------------------------------------------------*/
  if( err ) {
    pthread_mutex_unlock( &fifo->mutex );
    if( FifoIsReadable(fifo) )
      pthread_cond_signal( &fifo->condIsReadable );		
  }
  
  DBGMSG( "Fifo %p (%s): waiting for writable: %s", 
          fifo, fifo->name?fifo->name:"<unknown>", err?strerror(err):"Locked" ); 

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return err;
}


/*=========================================================================*\
      Lock fifo and wait for readable (usedSize>highWatermark) condition
        timeout is in ms, 0 or a negative values are treated as infinity
        returns 0 and locks fifo, if condition is met
                std. errode (ETIMEDOUT in case of timeout) and no locking otherwise 
\*=========================================================================*/
int fifoLockWaitReadable( Fifo *fifo, int timeout )
{
  struct timeval  now;
  struct timespec abstime;
  int             err = 0;

  DBGMSG( "Fifo %p (%s): waiting for readable: used=%ld >? high mark=%ld, timeout %dms", 
          fifo, fifo->name?fifo->name:"<unknown>",
          (long)fifoGetSize(fifo,FifoTotalUsed), (long)fifo->highWatermark, timeout ); 

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   pthread_mutex_lock( &fifo->mutex );
  
/*------------------------------------------------------------------------*\
    Get absolut timestamp for timeout
\*------------------------------------------------------------------------*/
  if( timeout>0 ) {
    gettimeofday( &now, NULL );
    abstime.tv_sec  = now.tv_sec + timeout/1000;
    abstime.tv_nsec = now.tv_usec*1000UL +(timeout%1000)*1000UL*1000UL;
    if( abstime.tv_nsec>1000UL*1000UL*1000UL ) {
      abstime.tv_nsec -= 1000UL*1000UL*1000UL;
      abstime.tv_sec++;
    }
  }

/*------------------------------------------------------------------------*\
    Loop while condtion is not met (cope with "spurious  wakeups")
\*------------------------------------------------------------------------*/
  while( !FifoIsReadable(fifo) ) {

    // wait for condition
    err = timeout>0 ? pthread_cond_timedwait( &fifo->condIsReadable, &fifo->mutex, &abstime )
                    : pthread_cond_wait( &fifo->condIsReadable, &fifo->mutex );
    
    // Break on errors
    if( err )
      break; 
  }

/*------------------------------------------------------------------------*\
    In case of error: unlock mutex
\*------------------------------------------------------------------------*/
  if( err ) {
    pthread_mutex_unlock( &fifo->mutex );
    if( FifoIsWritable(fifo) )
      pthread_cond_signal( &fifo->condIsWritable );		
  }

  DBGMSG( "Fifo %p (%s): waiting for readable: %s", 
          fifo, fifo->name?fifo->name:"<unknown>", err?strerror(err):"Locked" ); 

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return err;
}


/*=========================================================================*\
      Unlock after data was consumed
\*=========================================================================*/
void fifoUnlockAfterRead( Fifo *fifo, size_t size )
{
  DBGMSG( "Fifo %p (%s): unlocked after read of %ld byte: %s %s (used=%ld)", 
                      fifo, fifo->name?fifo->name:"<unknown>", (long) size,
                      FifoIsWritable(fifo) ? "isWritable" : "", 
                      FifoIsReadable(fifo) ? "isReadable" : "",
                      (long)fifoGetSize(fifo,FifoTotalUsed)  ); 

/*------------------------------------------------------------------------*\
    Adjust pointers
\*------------------------------------------------------------------------*/
  fifoDataConsumed( fifo, size );
  
/*------------------------------------------------------------------------*\
    Release mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &fifo->mutex );

/*------------------------------------------------------------------------*\
    Check for conditions: write first
\*------------------------------------------------------------------------*/
  if( FifoIsWritable(fifo) )
    pthread_cond_signal( &fifo->condIsWritable );		
  if( FifoIsReadable(fifo) )
    pthread_cond_signal( &fifo->condIsReadable );		
}


/*=========================================================================*\
      Unlock after data was written
\*=========================================================================*/
void fifoUnlockAfterWrite( Fifo *fifo, size_t size )
{
  DBGMSG( "Fifo %p (%s): unlocked after write of %ld bytes: %s %s (used=%ld)", 
                      fifo, fifo->name?fifo->name:"<unknown>", (long) size,
                      FifoIsWritable(fifo) ? "isWritable" : "", 
                      FifoIsReadable(fifo) ? "isReadable" : "",
                      (long)fifoGetSize(fifo,FifoTotalUsed) ); 

/*------------------------------------------------------------------------*\
    Adjust pointers
\*------------------------------------------------------------------------*/
  fifoDataWritten( fifo, size );
  
/*------------------------------------------------------------------------*\
    Release mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &fifo->mutex );

/*------------------------------------------------------------------------*\
    Check for conditions: read first
\*------------------------------------------------------------------------*/
  if( FifoIsReadable(fifo) )
    pthread_cond_signal( &fifo->condIsReadable );		
  if( FifoIsWritable(fifo) )
    pthread_cond_signal( &fifo->condIsWritable );		
}


/*=========================================================================*\
      Get ring buffer sizes 
        Note: the maximum fill size is size -1 
              (else wp==rp would indicate the empty condition)
\*=========================================================================*/
size_t fifoGetSize( Fifo *fifo, FifoSizeMode mode )
{
  char *endp = fifo->buffer+fifo->size;
  
/*------------------------------------------------------------------------*\
    Return size as requested by mode 
\*------------------------------------------------------------------------*/
  switch( mode ) {
  
    // Ring buffer size
    case FifoTotal:
      return fifo->size;

    // Used size
    case FifoTotalUsed:
      if( fifo->isFull )
        return fifo->size;
      if( fifo->readp<=fifo->writep )
        return fifo->writep-fifo->readp;
      return (endp-fifo->readp) + (fifo->writep-fifo->buffer);

    // Free size
    case FifoTotalFree:
      if( fifo->isFull )
        return 0;
      if( fifo->readp<=fifo->writep )
        return (fifo->readp-fifo->buffer) + (endp-fifo->writep);
      return fifo->readp - fifo->writep;
      
    // Size of next readable chunk (takes wrapping into account) 
    case FifoNextReadable:
      if( fifo->isFull || fifo->readp>fifo->writep )
        return endp - fifo->readp;
      return fifo->writep - fifo->readp;
      

    // Size of next writable chunk (takes wrapping into account) 
    case FifoNextWritable:
      if( fifo->isFull || fifo->readp>fifo->writep )
        return fifo->readp - fifo->writep;
      return endp - fifo->writep;
      
  }
  
/*------------------------------------------------------------------------*\
    Unknown mode 
\*------------------------------------------------------------------------*/
  logerr( "fifoGetSize(%p): unknown mode %d", fifo, mode );
  return -1;
}


/*=========================================================================*\
      New data was written, adjust wrtite pointer
        fifo should be locked
        return 0 on success, -1 on error (boundary check)
\*=========================================================================*/
int fifoDataWritten( Fifo *fifo, size_t size )
{

/*------------------------------------------------------------------------*\
    Check for boundary violation
\*------------------------------------------------------------------------*/
  char *eptr = (fifo->writep>=fifo->readp) ? fifo->buffer+fifo->size : fifo->readp;
  if( fifo->writep+size>eptr ) {
    logerr( "fifo %p (%s): data written beyond boundary (by %ld bytes)", 
                     fifo, fifo->name?fifo->name:"<unknown>",
                     (long) (fifo->writep-eptr+size) );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Increment read pointer modulo buffer size 
\*------------------------------------------------------------------------*/  
  fifo->writep += size;
  if( fifo->writep>=fifo->buffer+fifo->size )
    fifo->writep = fifo->buffer;

/*------------------------------------------------------------------------*\
    Is buffer full? 
\*------------------------------------------------------------------------*/
  if( size )
    fifo->isFull = (fifo->writep==fifo->readp);

/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/      
  return 0;
}    
    
    
/*=========================================================================*\
      Data was consumed, adjust read pointer
        fifo should be locked
        return 0 on success, -1 on error (boundary check)
\*=========================================================================*/
int fifoDataConsumed( Fifo *fifo, size_t size )
{

/*------------------------------------------------------------------------*\
    Check for boundary violation
\*------------------------------------------------------------------------*/
  char *eptr = (fifo->readp>=fifo->writep) ? fifo->buffer+fifo->size : fifo->writep;
  if( fifo->readp+size>eptr ) {
    logerr( "fifo %p (%s): data consumed beyond boundary (%ld bytes)", 
                     fifo, fifo->name?fifo->name:"<unknown>", 
                     (long) (fifo->readp+size-eptr) );
    return -1;
  }
  
/*------------------------------------------------------------------------*\
    Increment read pointer modulo buffer size 
\*------------------------------------------------------------------------*/  
  fifo->readp += size;
  if( fifo->readp>=fifo->buffer+fifo->size )
    fifo->readp = fifo->buffer;

/*------------------------------------------------------------------------*\
    Is buffer full? 
\*------------------------------------------------------------------------*/  
  if( size )
    fifo->isFull = false;

/*------------------------------------------------------------------------*\
    That's all 
\*------------------------------------------------------------------------*/  
  return 0;
}    


/*=========================================================================*\
      Insert new data (atomic opertion)
\*=========================================================================*/
size_t fifoInsert( Fifo *fifo, char *source, size_t len )
{
  // Fixme
  return 0;
}


/*=========================================================================*\
      Consume data (atomic operation)
\*=========================================================================*/
size_t fifoConsume( Fifo *fifo, char *target, size_t len )
{
  // Fixme
  return 0;
}




/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/

