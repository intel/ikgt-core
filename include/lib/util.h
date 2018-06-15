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

#ifndef _UTIL_H_
#define _UTIL_H_

#include "vmm_base.h"

extern uint64_t tsc_per_ms;

void memcpy(void *dest, const void *src, uint64_t count);
void memset(void *dest, uint8_t val, uint64_t count);
uint32_t lock_inc32(volatile uint32_t *addr);

boolean_t check_vmx(void);

void wait_us(uint64_t us);

uint64_t determine_nominal_tsc_freq(void);

#ifdef STACK_PROTECTOR
uint64_t get_stack_cookie_value(void);
#endif

#endif
