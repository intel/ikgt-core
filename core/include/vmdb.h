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

#ifndef _VMDB_H_
#define _VMDB_H_

typedef enum {
	VMDB_BREAK_ON_EXE = 0,
	VMDB_BREAK_ON_WO = 1,   /* break on memory write only */
	VMDB_BREAK_ON_IO = 2,   /* break on IO read/write */
	VMDB_BREAK_ON_RW = 3,   /* break on memory read/write */
	VMDB_BREAK_TYPE_FIRST = 0,
	VMDB_BREAK_TYPE_LAST = 3
} vmdb_breakpoint_type_t;

typedef enum {
	VMDB_BREAK_LENGTH_1 = 0, /* used for exe breaks */
	VMDB_BREAK_LENGTH_2 = 1,
	VMDB_BREAK_LENGTH_8 = 2,
	VMDB_BREAK_LENGTH_4 = 3,
	VMDB_BREAK_LENGTH_FIRST = 0,
	VMDB_BREAK_LENGTH_LAST = 3
} vmdb_break_length_type_t;

#define VMDB_INCLUDE

#ifdef VMDB_INCLUDE

void vmdb_initialize(void);
mon_status_t vmdb_guest_initialize(guest_id_t);
mon_status_t vmdb_thread_attach(guest_cpu_handle_t gcpu);
mon_status_t vmdb_thread_detach(guest_cpu_handle_t gcpu);

boolean_t vmdb_exception_handler(guest_cpu_handle_t gcpu);
mon_status_t vmdb_breakpoint_info(guest_cpu_handle_t gcpu,
				  uint32_t bp_id,
				  address_t *linear_address,
				  vmdb_breakpoint_type_t *bp_type,
				  vmdb_break_length_type_t *bp_len,
				  uint16_t *skip_counter);
mon_status_t vmdb_breakpoint_add(guest_cpu_handle_t gcpu,
				 address_t linear_address,
				 vmdb_breakpoint_type_t bp_type,
				 vmdb_break_length_type_t bp_len,
				 uint16_t skip_counter);
mon_status_t vmdb_breakpoint_delete(guest_cpu_handle_t gcpu,
				    address_t linear_address);
mon_status_t vmdb_breakpoint_delete_all(guest_cpu_handle_t gcpu);
mon_status_t vmdb_single_step_enable(guest_cpu_handle_t gcpu, boolean_t enable);
mon_status_t vmdb_single_step_info(guest_cpu_handle_t gcpu, boolean_t *enable);
void vmdb_settings_apply_to_hw(guest_cpu_handle_t gcpu);

#else
#define vmdb_initialize()
#define vmdb_guest_initialize(guest_id)             MON_OK
#define vmdb_thread_attach(gcpu)                    MON_ERROR
#define vmdb_thread_detach(gcpu)                    MON_ERROR
#define vmdb_exception_handler(gcpu) (FALSE /* exception NOT handled */)
#define vmdb_breakpoint_add(gcpu, linear_address, bp_type, bp_len, skip_counter) \
	MON_OK
#define vmdb_breakpoint_delete(gcpu, linear_address) MON_OK
#define vmdb_breakpoint_delete_all(gcpu)            MON_OK
#define vmdb_settings_apply_to_hw(gcpu)
#define vmdb_single_step_enable(gcpu, enable)       MON_OK

#endif          /* VMDB_INCLUDE */

#endif          /* _VMDB_H_ */
