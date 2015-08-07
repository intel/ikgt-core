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

#ifndef _MON_EVENTS_DATA_H_
#define _MON_EVENTS_DATA_H_

#include "mon_defs.h"
#include "event_mgr.h"

/*************************************************************************
*
* Define specific per-event data for event manager
*
*************************************************************************/

/* EVENT_EMULATOR_BEFORE_MEM_WRITE
 * EVENT_EMULATOR_AFTER_MEM_WRITE */
typedef struct {
	gpa_t		gpa;
	uint32_t	size;
	uint8_t		padding[4];
} event_emulator_mem_write_data_t;

/* EVENT_GCPU_BEFORE_GUEST_CR0_WRITE
 * EVENT_GCPU_BEFORE_GUEST_CR3_WRITE
 * EVENT_GCPU_BEFORE_GUEST_CR4_WRITE
 * event is rased before any changes */
typedef struct {
	uint64_t new_guest_visible_value;
} event_gcpu_guest_cr_write_data_t;

/* EVENT_GCPU_INVALIDATE_PAGE */
typedef struct {
	uint64_t invlpg_addr;
} event_gcpu_invalidate_page_data_t;

/* EVENT_GCPU_BEFORE_EFER_MSR_WRITE
 * EVENT_GCPU_BEFORE_PAT_MSR_WRITE
 * EVENT_GCPU_BEFORE_MTRR_MSR_WRITE
 * event is rased before any changes */
typedef struct {
	uint64_t	new_guest_visible_value;
	msr_id_t	msr_index;
	uint8_t		padding[4];
} event_gcpu_guest_msr_write_data_t;

typedef struct {
	uint64_t	guest_pat;
	uint64_t	actual_pat;
} event_gcpu_pat_msr_update_data_t;

/* EVENT_GCPU_PAGE_FAULT
 * NOTE: this callback must set processed to TRUE! */
typedef struct {
	uint64_t	pf_address;
	uint64_t	pf_error_code;
	boolean_t	pf_processed;
	uint8_t		pad[4];
} event_gcpu_page_fault_data_t;

/* EVENT_GCPU_BEFORE_ACTIVITY_STATE_CHANGE
 * event is rased before the change */
typedef struct {
	ia32_vmx_vmcs_guest_sleep_state_t	prev_state;
	ia32_vmx_vmcs_guest_sleep_state_t	new_state;
} event_gcpu_activity_state_change_data_t;

typedef struct {
	ia32_vmx_exit_qualification_t	qualification;
	uint64_t			guest_linear_address;
	boolean_t			processed;
	uint8_t				pad[4];
} event_gcpu_mtf_data_t;

typedef struct {
	ia32_vmx_exit_qualification_t	qualification;
	uint64_t			guest_linear_address;
	uint64_t			guest_physical_address;
	boolean_t			processed;
	uint8_t				pad[4];
} event_gcpu_ept_violation_data_t;

typedef struct {
	uint64_t	guest_physical_address;
	boolean_t	processed;
	uint8_t		pad[4];
} event_gcpu_ept_misconfiguration_data_t;

typedef enum {
	MON_MEM_OP_RECREATE = 1,
	MON_MEM_OP_SWITCH,
	MON_MEM_OP_UPDATE,
	MON_MEM_OP_REMOVE,
} mon_mem_op_t;

typedef struct {
	guest_id_t	guest_id;
	uint16_t	padding;
	mon_mem_op_t	operation;
} event_gpm_modification_data_t;

typedef struct {
	gpa_t		gpa;
	boolean_t	vtlb_succeed;
	guest_id_t	guest_id;
	uint8_t		pad[2];
} event_guest_memory_write_t;

typedef struct {
	guest_id_t guest_id;
} event_guest_create_data_t;

typedef struct {
	guest_id_t guest_id;
} event_guest_destroy_data_t;

#endif                          /* _MON_EVENTS_DATA_H_ */
