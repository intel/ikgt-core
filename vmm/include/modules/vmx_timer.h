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

#ifndef _VMX_TIMER_H_
#define _VMX_TIMER_H_

#ifndef MODULE_VMX_TIMER
#error "MODULE_VMX_TIMER is required "
#endif

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

uint64_t inline vmx_timer_tick_to_tsc(uint64_t tick)
{
	msr_misc_data_t misc_data;
	misc_data.uint64 = get_misc_data_cap();
	return tick << (misc_data.bits.vmx_timer_scale);
}

uint64_t inline vmx_timer_tick_to_ms(uint64_t tick)
{
	return vmx_timer_tick_to_tsc(tick) /(get_tsc_per_ms());
}

uint64_t inline vmx_timer_tsc_to_tick(uint64_t tsc)
{
	msr_misc_data_t misc_data;
	misc_data.uint64 = get_misc_data_cap();
	return tsc >> (misc_data.bits.vmx_timer_scale);
}

uint64_t inline vmx_timer_ms_to_tick(uint64_t ms)
{
	return vmx_timer_tsc_to_tick(ms * (get_tsc_per_ms()));
}

#endif                          /* _MODULE_VMX_TIMER_H_ */
