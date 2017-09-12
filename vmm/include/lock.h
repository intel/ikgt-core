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

#ifndef _VMM_LOCK_H_
#define _VMM_LOCK_H_

#include "vmm_base.h"

typedef struct {
	volatile uint32_t	uint32_lock;
#ifdef DEBUG
	uint32_t count[MAX_CPU_NUM];
#endif
} vmm_lock_t;

void lock_init(vmm_lock_t *lock);
void lock_acquire_read(vmm_lock_t *lock);
void lock_acquire_write(vmm_lock_t *lock);
void lock_release(vmm_lock_t *lock);

#endif
