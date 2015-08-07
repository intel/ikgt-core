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

#ifndef _MON_CALLBACK_H
#define _MON_CALLBACK_H

#include "mon_defs.h"
#include "mon_objects.h"
#include "vmx_vmcs.h"
#include "mon_arch_defs.h"

#ifndef INVALID_GUEST_ID
#define INVALID_GUEST_ID    ((guest_id_t)-1)
#endif

typedef void *mon_identification_data_t;

typedef struct {
	uint16_t		num_of_cpus;
	uint16_t		padding[3];
	guest_data_t		guest_data[MON_MAX_GUESTS_SUPPORTED];
	memory_config_t		*mem_config;
} report_initialization_data_t;

typedef struct {
	uint64_t	qualification;
	uint64_t	guest_linear_address;
	uint64_t	guest_physical_address;
} report_ept_violation_data_t;

typedef struct {
	uint64_t qualification;
} report_cr_dr_load_access_data_t;

typedef struct {
	uint64_t	qualification;
	uint32_t	instruction_info;
	uint32_t	padding;
} report_dtr_access_data_t;

typedef struct {
	uint32_t msr_id;
} report_msr_write_access_data_t;

typedef struct {
	uint32_t msr_id;
} report_msr_read_access_data_t;

typedef struct {
	uint64_t	eptp_list_index;
	boolean_t	update_hw;
	uint32_t	padding;
} report_set_active_eptp_data_t;

typedef struct {
	uint64_t	current_cpu_rip;
	uint32_t	vmexit_reason;
	uint32_t	padding;
} report_initial_vmexit_check_data_t;

typedef struct {
	uint32_t	vector;
	uint32_t	padding;
} report_mon_log_event_data_t;

typedef struct {
	uint64_t nonce;
} report_mon_teardown_data_t;

typedef struct {
	uint64_t reg;
} report_fast_view_switch_data_t;

typedef struct {
	uint64_t params;
} report_cpuid_data_t;

/* MON REPORTED EVENTS This enumeration specify the supported events reported
 * by MON to the supporting modules. */
typedef enum {
	/* Initialization before the APs have started */
	MON_EVENT_INITIALIZATION_BEFORE_APS_STARTED,

	/* Initialization after the APs have launched the guest */
	MON_EVENT_INITIALIZATION_AFTER_APS_STARTED,

	/* EPT Violation */
	MON_EVENT_EPT_VIOLATION,

	/* MTF VMExit */
	MON_EVENT_MTF_VMEXIT,

	/* CR Access VMExit */
	MON_EVENT_CR_ACCESS,

	/* DR Load Access VMExit */
	MON_EVENT_DR_LOAD_ACCESS,

	/* LDTR Load Access VMExit */
	MON_EVENT_LDTR_LOAD_ACCESS,

	/* GDTR Load Access VMExit */
	MON_EVENT_GDTR_IDTR_ACCESS,

	/* MSR Read Access VMExit */
	MON_EVENT_MSR_READ_ACCESS,

	/* MSR Write Access VMExit */
	MON_EVENT_MSR_WRITE_ACCESS,

	/* Set Active View (for Fast View Switch) */
	MON_EVENT_SET_ACTIVE_EPTP,

	/* Check for MTF at the start of VMExit */
	MON_EVENT_INITIAL_VMEXIT_CHECK,

	/* Check for single stepping */
	MON_EVENT_SINGLE_STEPPING_CHECK,

	/* MON Teardown VMExit */
	MON_EVENT_MON_TEARDOWN,

	/* Fast View Switch Event */
	MON_EVENT_INVALID_FAST_VIEW_SWITCH,

	/* VMX Timer VMExit */
	MON_EVENT_VMX_PREEMPTION_TIMER,

	/* Halt Instruction VMExit */
	MON_EVENT_HALT_INSTRUCTION,

	/* IO Instruction VMExit */
	MON_EVENT_IO_INSTRUCTION,

	/* NMI event handling */
	MON_EVENT_NMI,

	/* Event log */
	MON_EVENT_LOG,

	/* Update active view */
	MON_EVENT_UPDATE_ACTIVE_VIEW,

	/* MON_ASSERT handling */
	MON_EVENT_MON_ASSERT,

	/* CPUID VMExit */
	MON_EVENT_CPUID,

	MON_EVENT_MAX_COUNT
} report_mon_event_t;

extern boolean_t report_mon_event(report_mon_event_t event,
				  mon_identification_data_t gcpu,
				  const guest_vcpu_t *vcpu_id,
				  void *event_specific_data);

#endif /*_MON_CALLBACK_H */
