/*$*********************************************************************\

Name            : -

Source File     : fifo.h

Description     : Main include file for fifo.c 

Comments        : -

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


#ifndef __FIFO_H
#define __FIFO_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <errno.h>


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/

// A fifo instance
struct _fifo;
typedef struct _fifo Fifo;

// Modes for inquiring speces
typedef enum {
  FifoTotal,
  FifoTotalUsed,
  FifoTotalFree,
  FifoNextReadable,
  FifoNextWritable
} FifoSizeMode;


/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
// None

/*========================================================================*\
   Prototypes
\*========================================================================*/
Fifo       *fifoCreate( const char *name, size_t size );
void        fifoDelete( Fifo *fifo );
const char *fifoGetReadPtr( Fifo *fifo );
char       *fifoGetWritePtr( Fifo *fifo );
void        fifoReset( Fifo *fifo );
void        fifoLock( Fifo *fifo );
int         fifoLockWaitReadable( Fifo *fifo, int timeout );
int         fifoLockWaitWritable( Fifo *fifo, int timeout );
int         fifoLockWaitDrained( Fifo *fifo, int timeout );
void        fifoUnlock( Fifo *fifo );
void        fifoUnlockAfterRead( Fifo *fifo, size_t size );
void        fifoUnlockAfterWrite( Fifo *fifo, size_t size );
size_t      fifoFillAndUnlock( Fifo *fifo, const char *src, size_t bytes );
size_t      fifoGetSize( Fifo *fifo, FifoSizeMode mode );
int         fifoDataWritten( Fifo *fifo, size_t size );
int         fifoDataConsumed( Fifo *fifo, size_t size );

#endif  /* __FIFO_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

