/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef _MON_LOCK_H_
#define _MON_LOCK_H_

#include "mon_defs.h"
#include "hw_interlocked.h"
#include "mon_dbg.h"

typedef struct {
	volatile uint32_t	uint32_lock;
	volatile cpu_id_t	owner_cpu_id;
	char			padding[2];
} mon_lock_t;

#define LOCK_INIT_STATE     { (uint32_t)0, (cpu_id_t)-1, 0 }

/*
 * Read/Write lock
 *
 * multiple readers can read the data in parallel but an exclusive lock is
 * needed while writing the data. When a writer is writing the data, readers
 * will be blocked until the writer has finished writing
 */
typedef struct {
	mon_lock_t		lock;
	uint32_t		padding;
	volatile int32_t	readers;
} mon_read_write_lock_t;

/*
 * Various locking routines
 */
void lock_acquire(mon_lock_t *lock);

void interruptible_lock_acquire(mon_lock_t *lock);

void lock_release(mon_lock_t *lock);

void lock_initialize(mon_lock_t *lock);

#ifdef DEBUG
void lock_print(mon_lock_t *lock);
#endif

void lock_initialize_read_write_lock(mon_read_write_lock_t *lock);

void lock_acquire_readlock(mon_read_write_lock_t *lock);

void interruptible_lock_acquire_readlock(mon_read_write_lock_t *lock);

void lock_release_readlock(mon_read_write_lock_t *lock);

void lock_acquire_writelock(mon_read_write_lock_t *lock);

void interruptible_lock_acquire_writelock(mon_read_write_lock_t *lock);

void lock_release_writelock(mon_read_write_lock_t *lock);

#endif    /* _MON_LOCK_H_ */
