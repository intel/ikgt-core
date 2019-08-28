/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#include "vmm_base.h"
#include "evmm_desc.h"

extern uint64_t tsc_per_ms;

void memcpy(void *dest, const void *src, uint64_t count);
void memset(void *dest, uint8_t val, uint64_t count);
int memcmp(const void *dest, const void *src, uint64_t count);
uint32_t lock_inc32(volatile uint32_t *addr);

boolean_t check_vmx(void);

void wait_us(uint64_t us);

uint64_t determine_nominal_tsc_freq(void);

#ifdef STACK_PROTECTOR
uint64_t get_stack_cookie_value(void);
#endif

void save_current_cpu_state(gcpu_state_t *s);

/* Calculate barrier size for rowhammer mitigation.
 * return: 0xFFFFFFFFFFFFFFFF on fail, others on success */
uint64_t calulate_barrier_size(uint64_t total_mem_size, uint64_t min_mem_size);

#endif
