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

/*
 *
 * Trace mechanism
 *
 */

#ifndef VMX_TRACE_H
#define VMX_TRACE_H

#include "mon_defs.h"
#include "guest_cpu.h"

typedef enum {
	MON_TRACE_DISABLED,
	MON_TRACE_ENABLED_RECYCLED,
	MON_TRACE_ENABLED_NON_RECYCLED
} mon_trace_state_t;

boolean_t mon_trace_init(uint32_t max_num_guests, uint32_t max_num_guest_cpus);

boolean_t mon_trace(guest_cpu_handle_t guest_cpu, const char *format, ...);

boolean_t
mon_trace_buffer(guest_cpu_handle_t guest_cpu,
		 uint8_t buffer_index,
		 const char *format,
		 ...);

boolean_t mon_trace_print_all(uint32_t guest_num, char *guest_names[]);

void mon_trace_state_set(mon_trace_state_t state);

#endif                          /* VMX_TRACE_H */
