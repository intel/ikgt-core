/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "lock.h"
#include "dbg.h"
#include "lib/util.h"
#include "lib/print.h"

#define WRITELOCK_MASK  0x80000000
#define DEADLOCK_CYCLE 1200000

#if LOG_LEVEL >= LEVEL_PANIC
static boolean_t printed = FALSE;
#define print_once(fmt, ...) { \
	if (!printed) { \
		printf("PANIC IN VMM: " fmt, ##__VA_ARGS__); \
		printed = TRUE; \
	} \
}
#else
#define print_once(fmt, ...) {}
#endif

void lock_init(vmm_lock_t *lock, const char *name)
{
	D(VMM_ASSERT(lock));

	lock->lock = 0;
	lock->name = name;
}

void lock_acquire_read(vmm_lock_t *lock)
{
	uint32_t orig_value;
	uint32_t new_value;
	uint32_t count = 0;

	D(VMM_ASSERT(lock));

	for (orig_value = lock->lock;; orig_value = lock->lock) {
		new_value = orig_value + 1;
		if (WRITELOCK_MASK == orig_value) {
			asm_pause();
			count++;
			if (count >= DEADLOCK_CYCLE)
				print_once("cpu maybe dead lock(%s)\n", lock->name);
		}else if (orig_value == asm_lock_cmpxchg32(&(lock->lock),
				new_value,
				orig_value)) {
			break;
		}
	}
}

void lock_acquire_write(vmm_lock_t *lock)
{
	uint32_t orig_value;
	uint32_t count = 0;

	D(VMM_ASSERT(lock));

	for (;;) {
		orig_value = asm_lock_cmpxchg32(&(lock->lock),
				WRITELOCK_MASK,
				0);
		if (0 == orig_value) {
			break;
		}
		asm_pause();
		count++;
		if (count >= DEADLOCK_CYCLE)
			print_once("cpu maybe dead lock(%s)\n", lock->name);
	}
}

void lock_release(vmm_lock_t *lock)
{
	uint32_t orig_value;
	uint32_t new_value;

	D(VMM_ASSERT(lock));

	if (WRITELOCK_MASK == lock->lock) {
		lock->lock = 0;
		return;
	}

	for (orig_value = lock->lock;; orig_value = lock->lock) {
		new_value = orig_value - 1;
		if (0 == orig_value) {
			D(printf("there is nothing to hold the lock\n"));
			break;
		}
		if (orig_value == asm_lock_cmpxchg32(&(lock->lock),
					new_value,
					orig_value)) {
			break;
		}
	}
}









