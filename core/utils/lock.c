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

#include "libc.h"
#include "hw_includes.h"
#include "lock.h"
#include "ipc.h"
#include "mon_dbg.h"
#include "file_codes.h"

#define MON_DEADLOOP()                 MON_DEADLOOP_LOG(LOCK_C)
#define MON_ASSERT(__condition)        MON_ASSERT_LOG(LOCK_C, __condition)
#define MON_ASSERT_NOLOCK(__condition) MON_ASSERT_NOLOCK_LOG(LOCK_C, \
	__condition)

/* lock_try_acquire - returns TRUE if lock was aquired and FALSE if lock is
 * taken by another cpu */
boolean_t lock_try_acquire(mon_lock_t *lock);

/*
 * Various locking and interlocked routines
 */
void lock_acquire(mon_lock_t *lock)
{
	cpu_id_t this_cpu_id = hw_cpu_id();

	if (!lock) {
		/* error */
		return;
	}

	while (FALSE == lock_try_acquire(lock)) {
		MON_ASSERT_NOLOCK(lock->owner_cpu_id != this_cpu_id);
		hw_pause();
	}

	lock->owner_cpu_id = this_cpu_id;
}

void interruptible_lock_acquire(mon_lock_t *lock)
{
	cpu_id_t this_cpu_id = hw_cpu_id();
	boolean_t ipc_processed = FALSE;

	if (!lock) {
		/* error */
		return;
	}

	while (FALSE == lock_try_acquire(lock)) {
		MON_ASSERT_NOLOCK(lock->owner_cpu_id != this_cpu_id);
		ipc_processed = ipc_process_one_ipc();

		if (FALSE == ipc_processed) {
			hw_pause();
		}
	}

	lock->owner_cpu_id = this_cpu_id;
}

void lock_release(mon_lock_t *lock)
{
	if (!lock) {
		/* error */
		return;
	}

	lock->owner_cpu_id = (cpu_id_t)-1;
	hw_interlocked_assign((volatile int32_t *)(&(lock->uint32_lock)), 0);
}

/* lock_try_acquire - returns TRUE if lock was aquired and FALSE if lock is
 * taken by another cpu */
boolean_t lock_try_acquire(mon_lock_t *lock)
{
	uint32_t expected_value = 0, current_value;
	uint32_t new_value = 1;

	if (!lock) {
		/* error */
		return FALSE;
	}

	current_value =
		hw_interlocked_compare_exchange((int32_t volatile
						 *)(&(lock->uint32_lock)),
			expected_value, new_value);

	return current_value == expected_value;
}

void lock_initialize(mon_lock_t *lock)
{
	lock_release(lock);
}

void lock_initialize_read_write_lock(mon_read_write_lock_t *lock)
{
	lock_initialize(&lock->lock);
	lock->readers = 0;
}

void lock_acquire_readlock(mon_read_write_lock_t *lock)
{
	lock_acquire(&lock->lock);
	hw_interlocked_increment((int32_t *)(&lock->readers));
	MON_ASSERT(lock->readers);
	lock_release(&lock->lock);
}

void interruptible_lock_acquire_readlock(mon_read_write_lock_t *lock)
{
	interruptible_lock_acquire(&lock->lock);
	hw_interlocked_increment((int32_t *)(&lock->readers));
	MON_ASSERT(lock->readers);
	lock_release(&lock->lock);
}

void lock_release_readlock(mon_read_write_lock_t *lock)
{
	MON_ASSERT(lock->readers);
	hw_interlocked_decrement((int32_t *)(&lock->readers));
}

void lock_acquire_writelock(mon_read_write_lock_t *lock)
{
	lock_acquire(&lock->lock);
	while (lock->readers)
		hw_pause();
	/*
	 *  wait until readers == 0
	 */
	MON_ASSERT(lock->readers == 0);
}

void interruptible_lock_acquire_writelock(mon_read_write_lock_t *lock)
{
	boolean_t ipc_processed = FALSE;

	interruptible_lock_acquire(&lock->lock);
	while (lock->readers) {
		ipc_processed = ipc_process_one_ipc();

		if (FALSE == ipc_processed) {
			hw_pause();
		}
		/*
		 *  wait until readers == 0
		 */
	}
	MON_ASSERT(lock->readers == 0);
}

void lock_release_writelock(mon_read_write_lock_t *lock)
{
	MON_ASSERT(lock->readers == 0);
	lock_release(&lock->lock);
}

MON_DEBUG_CODE(
	void lock_print(mon_lock_t *lock)
	{
		MON_LOG(mask_anonymous, level_trace,
			"lock %p: value=%d, owner=%d\r\n", lock,
			lock->uint32_lock, lock->owner_cpu_id);
	}
	)
