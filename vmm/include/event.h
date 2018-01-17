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
#ifndef _EVNET_MGR_H
#define _EVNET_MGR_H

#include "vmm_objects.h"
#include "vmm_base.h"
#include "ept.h"

typedef void (*event_callback_t) (guest_cpu_handle_t gcpu, void *pv);

typedef enum {
	EVENT_SIPI_VMEXIT,
	EVENT_PROCESS_NMI_BEFORE_RESUME,
	EVENT_VMX_TIMER,
	EVENT_GCPU_INIT,
	EVENT_GUEST_MODULE_INIT, //gcpu is NULL for this event
	EVENT_GCPU_MODULE_INIT,
	EVENT_EPT_VIOLATION,
	EVENT_DEADLOOP,
	EVENT_MSR_ACCESS,
	EVENT_GCPU_SWAPIN,
	EVENT_GCPU_SWAPOUT,
	EVENT_RESUME_FROM_S3,
	EVENT_GPM_SET,
	EVENT_VMENTRY_FAIL,
	EVENT_MODULE_PROFILE,
	EVENT_SET_CR2,
	EVENT_INJECT_INTR,
	EVENT_RSB_OVERWRITE, /* This event must be raised before any RET instruction on VM Exit.
				And it is used for Spectre module only. Do NOT register this event
				if you are not familiar with Spectre module. */
	EVENTS_COUNT
} vmm_event_t;

typedef struct {
	uint8_t vector;
	uint8_t pad[3];
	boolean_t handled;
} event_sipi_vmexit_t;

typedef struct {
	boolean_t handled;
} event_ept_violation_t;

typedef struct {
	const char *file_name;
	uint32_t line_num;
	uint32_t pad;
} event_deadloop_t;

typedef struct {
	boolean_t is_write;
	boolean_t handled;
} event_msr_vmexit_t;

typedef struct {
	guest_handle_t guest;
	uint64_t gpa;
	uint64_t hpa;
	uint64_t size;
	ept_attr_t attr;
	uint32_t padding;
} event_gpm_set_t;

typedef struct {
	uint64_t vmexit_tsc;
	guest_cpu_handle_t next_gcpu;
} event_profile_t;

typedef struct {
	uint64_t cr2;
	boolean_t handled;
	uint32_t pad;
} event_set_cr2_t;

void event_register(vmm_event_t e, event_callback_t call);

void event_raise(guest_cpu_handle_t gcpu, vmm_event_t e, void *p);

#endif /* _EVNET_MGR_H */
