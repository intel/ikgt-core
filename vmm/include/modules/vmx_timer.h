/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VMX_TIMER_H_
#define _VMX_TIMER_H_

#ifndef MODULE_VMX_TIMER
#error "MODULE_VMX_TIMER is required "
#endif

#include "lib/util.h"
#include "vmx_cap.h"

typedef enum {
	TIMER_MODE_PERIOD,
	TIMER_MODE_ONESHOT,
	TIMER_MODE_STOPPED,
	TIMER_MODE_NOT_EXIST,
	TIMER_MODE_ONESHOT_DEACTIVE
} vmx_timer_mode_t;

void vmx_timer_init(void);
void vmx_timer_set_mode(guest_cpu_handle_t gcpu, uint32_t mode, uint64_t periodic);
uint32_t vmx_timer_get_mode(guest_cpu_handle_t gcpu);
void vmx_timer_copy(guest_cpu_handle_t gcpu_from, guest_cpu_handle_t gcpu_to);

inline uint64_t vmx_timer_tick_to_tsc(uint64_t tick)
{
	msr_misc_data_t misc_data;
	misc_data.uint64 = get_misc_data_cap();
	return tick << (misc_data.bits.vmx_timer_scale);
}

inline uint64_t vmx_timer_tick_to_ms(uint64_t tick)
{
	return vmx_timer_tick_to_tsc(tick) / tsc_per_ms;
}

inline uint64_t vmx_timer_tsc_to_tick(uint64_t tsc)
{
	msr_misc_data_t misc_data;
	misc_data.uint64 = get_misc_data_cap();
	return tsc >> (misc_data.bits.vmx_timer_scale);
}

inline uint64_t vmx_timer_ms_to_tick(uint64_t ms)
{
	return vmx_timer_tsc_to_tick(ms * tsc_per_ms);
}

#endif                          /* _MODULE_VMX_TIMER_H_ */
