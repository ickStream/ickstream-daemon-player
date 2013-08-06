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

#undef ICK_DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>

#include "ickutils.h"
#include "fifo.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
	Private symbols
\*=========================================================================*/
struct _fifo {
  char            *name;

  // Actual buffer
  size_t           size;
  char            *buffer;
  volatile char   *readp;          // pointer to next read
  volatile char   *writep;         // pointer to next write
  volatile bool    isFull;         // if readp==writep buffer is either empty or full
  volatile bool    isDraining;

  // Access arbitration
  size_t           lowWatermark;   // freeSize<lowWatermark  -> isWritable
  size_t           highWatermark;  // freeSize>highWatermark -> isReadable
  pthread_mutex_t  mutex;
  pthread_cond_t   condIsWritable;
  pthread_cond_t   condIsDrained;
  pthread_cond_t   condIsReadable;
};

#define FifoIsEmpty(fifo) ((fifo)->readp==(fifo)->writep)
#define FifoIsWritable(fifo) (fifoGetSize((fifo),FifoTotalUsed)<(fifo)->lowWatermark)
#define FifoIsReadable(fifo) (fifoGetSize((fifo),FifoTotalUsed)>(fifo)->highWatermark)


/*=========================================================================*\
      Create a ring buffer 
        name is optional and might be NULL
        returns NULL on error
\*=========================================================================*/
Fifo *fifoCreate( const char *name, size_t size )
{
  Fifo                *fifo;

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
    Init mutex and conditions
\*------------------------------------------------------------------------*/
  ickMutexInit( &fifo->mutex );
  pthread_cond_init( &fifo->condIsWritable, NULL );
  pthread_cond_init( &fifo->condIsDrained, NULL );
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
  pthread_cond_destroy( &fifo->condIsDrained );
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
  DBGMSG( "Fifo (%p,%s): lock.", fifo, fifo->name?fifo->name:"<unknown>" );
  pthread_mutex_lock( &fifo->mutex );
}


/*=========================================================================*\
      Lock fifo and wait for writable (usedSize<lowWatermark) condition
        timeout is in ms, 0 or a negative values are treated as infinity
        bytes is minimum size required (might be 0)
        returns 0 and locks fifo, if condition is met
                std. errode (ETIMEDOUT in case of timeout) and no locking otherwise 
\*=========================================================================*/
int fifoLockWaitWritable( Fifo *fifo, int timeout, size_t bytes )
{
  struct timeval  now;
  struct timespec abstime;
  int             err = 0;

/*------------------------------------------------------------------------*\
    Check limit
\*------------------------------------------------------------------------*/
  if( bytes>fifo->size ) {
    logerr( "fifoLockWaitWritable: requested size (%ld) is larger then fifo (%ld).",
        bytes, fifo->size );
  }

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   pthread_mutex_lock( &fifo->mutex );

   DBGMSG( "Fifo (%p,%s): waiting for writable: used=%ld <? low mark=%ld, timeout %dms, requested=%ld <? free=%ld",
           fifo, fifo->name?fifo->name:"<unknown>",
           (long)fifoGetSize(fifo,FifoTotalUsed), (long)fifo->lowWatermark, timeout,
           (long)bytes, (long)fifoGetSize(fifo,FifoTotalFree) );

/*------------------------------------------------------------------------*\
    Get absolute timestamp for timeout
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
    Loop while condition is not met (cope with "spurious wakeups")
\*------------------------------------------------------------------------*/
  while( !FifoIsWritable(fifo) || (bytes&&fifoGetSize(fifo,FifoTotalFree)<bytes) ) {

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
  
/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  DBGMSG( "Fifo (%p,%s): waiting for writable: %s",
          fifo, fifo->name?fifo->name:"<unknown>", err?strerror(err):"Locked" );
  return err;
}


/*=========================================================================*\
      Lock fifo and wait for empty condition
        timeout is in ms, 0 or a negative values are treated as infinity
        returns 0 and locks fifo, if condition is met
                std. errode (ETIMEDOUT in case of timeout) and no locking otherwise
\*=========================================================================*/
int fifoLockWaitDrained( Fifo *fifo, int timeout )
{
  struct timeval  now;
  struct timespec abstime;
  int             err = 0;

  DBGMSG( "Fifo (%p,%s): waiting for drained: used=%ld, timeout %dms",
          fifo, fifo->name?fifo->name:"<unknown>",
          (long)fifoGetSize(fifo,FifoTotalUsed), timeout );

/*------------------------------------------------------------------------*\
    We're draining
\*------------------------------------------------------------------------*/
  fifo->isDraining = true;

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
   pthread_mutex_lock( &fifo->mutex );

/*------------------------------------------------------------------------*\
    Get absolute timestamp for timeout
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
    Loop while condition is not met (cope with "spurious  wakeups")
\*------------------------------------------------------------------------*/
  while( !FifoIsEmpty(fifo) ) {

    // wait for condition
    err = timeout>0 ? pthread_cond_timedwait( &fifo->condIsDrained, &fifo->mutex, &abstime )
                    : pthread_cond_wait( &fifo->condIsDrained, &fifo->mutex );

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

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  fifo->isDraining = false;
  DBGMSG( "Fifo (%p,%s): waiting for draining: %s",
          fifo, fifo->name?fifo->name:"<unknown>", err?strerror(err):"Locked" );
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

  DBGMSG( "Fifo (%p,%s): waiting for readable: used=%ld >? high mark=%ld, timeout %dms",
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
    if( fifo->isDraining ) {
      if( FifoIsEmpty(fifo) )
        pthread_cond_signal( &fifo->condIsDrained );
    }
    else if( FifoIsWritable(fifo) )
      pthread_cond_signal( &fifo->condIsWritable );
  }

  DBGMSG( "Fifo (%p,%s): waiting for readable: %s",
          fifo, fifo->name?fifo->name:"<unknown>", err?strerror(err):"Locked" ); 

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return err;
}


/*=========================================================================*\
      Unlock fifo to avoid concurrent modifications
\*=========================================================================*/
void fifoUnlock( Fifo *fifo )
{
  DBGMSG( "Fifo (%p,%s): unlock.", fifo, fifo->name?fifo->name:"<unknown>" );
  pthread_mutex_unlock( &fifo->mutex );
}


/*=========================================================================*\
      Unlock after data was consumed
\*=========================================================================*/
void fifoUnlockAfterRead( Fifo *fifo, size_t size )
{
  DBGMSG( "Fifo (%p,%s): unlocked after read of %ld byte: %s %s (used=%ld)",
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
    Check for conditions: write/draining first
\*------------------------------------------------------------------------*/
  if( fifo->isDraining ) {
    if( FifoIsEmpty(fifo) )
      pthread_cond_signal( &fifo->condIsDrained );
  }
  else if( FifoIsWritable(fifo) )
    pthread_cond_signal( &fifo->condIsWritable );
  if( FifoIsReadable(fifo) )
    pthread_cond_signal( &fifo->condIsReadable );
}


/*=========================================================================*\
      Unlock after data was written
\*=========================================================================*/
void fifoUnlockAfterWrite( Fifo *fifo, size_t size )
{
  DBGMSG( "Fifo (%p,%s): unlocked after write of %ld bytes: %s %s (used=%ld)",
                      fifo, fifo->name?fifo->name:"<unknown>", (long) size,
                      FifoIsWritable(fifo) ? "isWritable" : "", 
                      FifoIsReadable(fifo) ? "isReadable" : "",
                      (long)fifoGetSize(fifo,FifoTotalUsed) ); 

/*------------------------------------------------------------------------*\
    This is an error!
\*------------------------------------------------------------------------*/
  if( fifo->isDraining )
    logerr( "Fifo (%s): wrote %ld bytes in draining mode.",
        fifo, fifo->name?fifo->name:"<unknown>", (long) size );

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
  if( fifo->isDraining ) {
    if( FifoIsEmpty(fifo) )
      pthread_cond_signal( &fifo->condIsDrained );
  }
  else if( FifoIsWritable(fifo) )
    pthread_cond_signal( &fifo->condIsWritable );
}


/*=========================================================================*\
    Copy data to fifo and unlock - caller has to lock fifo first
      returns the data actually copied
\*=========================================================================*/
size_t fifoFillAndUnlock( Fifo *fifo, const char *src, size_t bytes )
{
  size_t space;
  size_t written = 0;
  DBGMSG( "Fifo (%p,%s): fill with %ld bytes",
                      fifo, fifo->name?fifo->name:"<unknown>", (long)bytes );

/*------------------------------------------------------------------------*\
    This is an error!
\*------------------------------------------------------------------------*/
  if( fifo->isDraining )
    logerr( "Fifo (%s): writing %ld bytes in draining mode.",
        fifo, fifo->name?fifo->name:"<unknown>", (long) bytes );

/*------------------------------------------------------------------------*\
    Get size of first trunk (up to end) and
    check if there is actually something to do
\*------------------------------------------------------------------------*/
  space = fifoGetSize( fifo, FifoNextWritable );
  if( space && bytes ) {

/*------------------------------------------------------------------------*\
    All data fits in as one piece
\*------------------------------------------------------------------------*/
    if( bytes<=space ) {

      // Copy data and increment write pointer
      memcpy( (char*)fifo->writep, src, bytes );
      written       = bytes;
      fifo->writep += written;

      // wrapping?
      if( fifo->writep>=fifo->buffer+fifo->size )
        fifo->writep = fifo->buffer;
    }

/*------------------------------------------------------------------------*\
    Need to split data in two parts
\*------------------------------------------------------------------------*/
    else {

      // Copy first part till end of buffer, wrap write pointer
      memcpy( (char*)fifo->writep, src, space );
      written = space;
      bytes  -= space;
      src    += space;
      fifo->writep = fifo->buffer;

      // Copy second part
      space = MIN( bytes, fifoGetSize(fifo,FifoNextWritable) );
      memcpy( (char*)fifo->writep, src, space );
      written      += space;
      fifo->writep += space;
    }

/*------------------------------------------------------------------------*\
    Is buffer full?
\*------------------------------------------------------------------------*/
    fifo->isFull = (fifo->writep==fifo->readp);
  }

/*------------------------------------------------------------------------*\
    Release mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &fifo->mutex );

/*------------------------------------------------------------------------*\
    Check for conditions: read first
\*------------------------------------------------------------------------*/
  if( FifoIsReadable(fifo) )
    pthread_cond_signal( &fifo->condIsReadable );
  if( fifo->isDraining ) {
    if( FifoIsEmpty(fifo) )
      pthread_cond_signal( &fifo->condIsDrained );
  }
  else if( FifoIsWritable(fifo) )
    pthread_cond_signal( &fifo->condIsWritable );

/*------------------------------------------------------------------------*\
    Return number of bytes written
\*------------------------------------------------------------------------*/
  return written;
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
  logerr( "Fifo (%s): unknown getSize mode %d",
           fifo->name?fifo->name:"<unknown>", mode );
  return -1;
}


/*=========================================================================*\
    Get pointer to read position
\*=========================================================================*/
const char *fifoGetReadPtr( Fifo *fifo )
{
  return (const char *) fifo->readp;
}

/*=========================================================================*\
    Get pointer to write position
\*=========================================================================*/
char *fifoGetWritePtr( Fifo *fifo )
{
  return (char *) fifo->writep;
}

/*=========================================================================*\
      New data was written, adjust write pointer
        fifo should be locked
        return 0 on success, -1 on error (boundary check)
\*=========================================================================*/
int fifoDataWritten( Fifo *fifo, size_t size )
{
  volatile char *eptr;
  DBGMSG( "Fifo (%p,%s): %ld bytes written.",
          fifo, fifo->name?fifo->name:"<unknown>", (long)size );

/*------------------------------------------------------------------------*\
    Check for boundary violation
\*------------------------------------------------------------------------*/
  eptr = (fifo->writep>=fifo->readp) ? fifo->buffer+fifo->size : fifo->readp;
  if( fifo->writep+size>eptr ) {
    logerr( "Fifo (%s): data written beyond boundary (by %ld bytes)",
                     fifo->name?fifo->name:"<unknown>",
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
  volatile char *eptr;
  DBGMSG( "Fifo (%p,%s): %ld bytes consumed.",
          fifo, fifo->name?fifo->name:"<unknown>", (long)size );

/*------------------------------------------------------------------------*\
    Check for boundary violation
\*------------------------------------------------------------------------*/
  eptr = (fifo->readp>=fifo->writep) ? fifo->buffer+fifo->size : fifo->writep;
  if( fifo->readp+size>eptr ) {
    logerr( "Fifo (%s): data consumed beyond boundary (%ld bytes)",
                     fifo->name?fifo->name:"<unknown>",
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
                                    END OF FILE
\*=========================================================================*/

