/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VMM_LOCK_H_
#define _VMM_LOCK_H_

#include "vmm_base.h"

typedef struct {
	volatile uint32_t lock;
	uint32_t pad;
	const char * name;
} vmm_lock_t;

void lock_init(vmm_lock_t *lock, const char *name);
void lock_acquire_read(vmm_lock_t *lock);
void lock_acquire_write(vmm_lock_t *lock);
void lock_release(vmm_lock_t *lock);

#endif
