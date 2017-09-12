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

#include "vmm_asm.h"
#include "lock.h"
#include "dbg.h"
#include "host_cpu.h"
#include "lib/print.h"

#define WRITELOCK_MASK  0x80000000

void lock_init(vmm_lock_t *lock)
{
	D(VMM_ASSERT(lock));

	lock->uint32_lock = 0;

}

void lock_acquire_read(vmm_lock_t *lock)
{
	uint32_t orig_value;
	uint32_t new_value;

	D(VMM_ASSERT(lock));
	D(VMM_ASSERT(not_in_isr()));

	for (orig_value = lock->uint32_lock;; orig_value = lock->uint32_lock) {
		new_value = orig_value + 1;
		if (WRITELOCK_MASK == orig_value) {
			D(VMM_ASSERT(lock->count[host_cpu_id()] == 0));
			asm_pause();
		}else if (orig_value == asm_lock_cmpxchg32((&lock->uint32_lock),
			    new_value,
			    orig_value)) {
			break;
		}
	}
	D(asm_inc32(&lock->count[host_cpu_id()]));
}

void lock_acquire_write(vmm_lock_t *lock)
{
	uint32_t orig_value;

	D(VMM_ASSERT(lock));
	D(VMM_ASSERT(not_in_isr()));
	for (;;) {
		orig_value = asm_lock_cmpxchg32((&lock->uint32_lock),
				WRITELOCK_MASK,
				0);
		if (0 == orig_value) {
			break;
		}
		D(VMM_ASSERT(lock->count[host_cpu_id()] == 0));
		asm_pause();
	}

	D(asm_inc32(&lock->count[host_cpu_id()]));
}

void lock_release(vmm_lock_t *lock)
{
	uint32_t orig_value;
	uint32_t new_value;

	D(VMM_ASSERT(lock));

	if (WRITELOCK_MASK == lock->uint32_lock) {
		lock->uint32_lock = 0;
		D(asm_dec32(&lock->count[host_cpu_id()]));
		return;
	}

	for (orig_value = lock->uint32_lock;; orig_value = lock->uint32_lock) {
		new_value = orig_value - 1;
		if (0 == orig_value) {
			D(printf("there is nothing to hold the lock\n"));
			break;
		}
		if (orig_value == asm_lock_cmpxchg32((&lock->uint32_lock),
					new_value,
					orig_value)) {
			break;
		}
	}
	D(asm_dec32(&lock->count[host_cpu_id()]));
}









