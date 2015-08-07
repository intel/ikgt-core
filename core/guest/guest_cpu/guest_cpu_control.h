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

#ifndef _GUEST_CPU_CONTROL_INTERNAL_H_
#define _GUEST_CPU_CONTROL_INTERNAL_H_

#include "mon_defs.h"
#include "vmx_ctrl_msrs.h"
#include "lock.h"

/* -------------------------- types ----------------------------------------- */
typedef struct {
	uint8_t		counters[64];
	uint64_t	bit_field;              /* 1bit for each non-zero counter */
	uint64_t	minimal_1_settings;     /* one time calculated at boot; enforce 1 for each bit set */
	uint64_t	minimal_0_settings;     /* one time calculated at boot; enforce 0 or each bit cleared */
} gcpu_vmexit_control_field_counters_t;

typedef struct {
	gcpu_vmexit_control_field_counters_t	cr0;
	gcpu_vmexit_control_field_counters_t	cr4;
	gcpu_vmexit_control_field_counters_t	pin_ctrls;
	gcpu_vmexit_control_field_counters_t	processor_ctrls;
	gcpu_vmexit_control_field_counters_t	processor_ctrls2;
	gcpu_vmexit_control_field_counters_t	exceptions_ctrls;
	gcpu_vmexit_control_field_counters_t	vm_entry_ctrls;
	gcpu_vmexit_control_field_counters_t	vm_exit_ctrls;
	mon_lock_t				lock;
} gcpu_vmexit_controls_t;

void guest_cpu_control_setup(guest_cpu_handle_t gcpu);

typedef enum {
	GCPU_TEMP_EXCEPTIONS_EXIT_ON_ALL,
	GCPU_TEMP_EXCEPTIONS_RESTORE_ALL,

	GCPU_TEMP_EXIT_ON_PF_AND_CR3,
	GCPU_TEMP_RESTORE_PF_AND_CR3,

	GCPU_TEMP_CR0_NO_EXIT_ON_WP,
	GCPU_TEMP_CR0_RESTORE_WP,

	GCPU_TEMP_EXIT_ON_INTR_UNBLOCK,
	GCPU_TEMP_NO_EXIT_ON_INTR_UNBLOCK,
} gcpu_temp_exceptions_setup_t;

void gcpu_temp_exceptions_setup(guest_cpu_handle_t gcpu,
				gcpu_temp_exceptions_setup_t action);

boolean_t gcpu_cr3_virtualized(guest_cpu_handle_t gcpu);

void gcpu_enforce_settings_on_hardware(guest_cpu_handle_t gcpu,
				       gcpu_temp_exceptions_setup_t action);

#endif                          /* _GUEST_CPU_CONTROL_INTERNAL_H_ */
