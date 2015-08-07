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

#ifndef _VMCS_HW_OBJECT_H_
#define _VMCS_HW_OBJECT_H_

#include "mon_objects.h"
#include "vmcs_api.h"

/*
 * VMCS Instruction error
 */
typedef enum {
	VMCS_INSTR_NO_INSTRUCTION_ERROR = 0,                                    /* VMxxxxx */
	VMCS_INSTR_VMCALL_IN_ROOT_ERROR,                                        /* VMCALL */
	VMCS_INSTR_VMCLEAR_INVALID_PHYSICAL_ADDRESS_ERROR,                      /* VMCLEAR */
	VMCS_INSTR_VMCLEAR_WITH_CURRENT_CONTROLLING_PTR_ERROR,                  /* VMCLEAR */
	VMCS_INSTR_VMLAUNCH_WITH_NON_CLEAR_VMCS_ERROR,                          /* VMLAUNCH */
	VMCS_INSTR_VMRESUME_WITH_NON_LAUNCHED_VMCS_ERROR,                       /* VMRESUME */
	VMCS_INSTR_VMRESUME_WITH_NON_CHILD_VMCS_ERROR,                          /* VMRESUME */
	VMCS_INSTR_VMENTER_BAD_CONTROL_FIELD_ERROR,                             /* VMENTER */
	VMCS_INSTR_VMENTER_BAD_MONITOR_STATE_ERROR,                             /* VMENTER */
	VMCS_INSTR_VMPTRLD_INVALID_PHYSICAL_ADDRESS_ERROR,                      /* VMPTRLD */
	VMCS_INSTR_VMPTRLD_WITH_CURRENT_CONTROLLING_PTR_ERROR,                  /* VMPTRLD */
	VMCS_INSTR_VMPTRLD_WITH_BAD_REVISION_ID_ERROR,                          /* VMPTRLD */
	VMCS_INSTR_VMREAD_OR_VMWRITE_OF_UNSUPPORTED_COMPONENT_ERROR,            /* VMREAD */
	VMCS_INSTR_VMWRITE_OF_READ_ONLY_COMPONENT_ERROR,                        /* VMWRITE */
	VMCS_INSTR_VMWRITE_INVALID_FIELD_VALUE_ERROR,                           /* VMWRITE */
	VMCS_INSTR_VMXON_IN_VMX_ROOT_OPERATION_ERROR,                           /* VMXON */
	VMCS_INSTR_VMENTRY_WITH_BAD_OSV_CONTROLLING_VMCS_ERROR,                 /* VMENTER */
	VMCS_INSTR_VMENTRY_WITH_NON_LAUNCHED_OSV_CONTROLLING_VMCS_ERROR,        /*VMENTER*/
	VMCS_INSTR_VMENTRY_WITH_NON_ROOT_OSV_CONTROLLING_VMCS_ERROR,            /* VMENTER */
	VMCS_INSTR_VMCALL_WITH_NON_CLEAR_VMCS_ERROR,                            /* VMCALL */
	VMCS_INSTR_VMCALL_WITH_BAD_VMEXIT_FIELDS_ERROR,                         /* VMCALL */
	VMCS_INSTR_VMCALL_WITH_INVALID_MSEG_MSR_ERROR,                          /* VMCALL */
	VMCS_INSTR_VMCALL_WITH_INVALID_MSEG_REVISION_ERROR,                     /* VMCALL */
	VMCS_INSTR_VMXOFF_WITH_CONFIGURED_SMM_MONITOR_ERROR,                    /* VMXOFF */
	VMCS_INSTR_VMCALL_WITH_BAD_SMM_MONITOR_FEATURES_ERROR,                  /* VMCALL */
	/* Return from SMM */
	VMCS_INSTR_RETURN_FROM_SMM_WITH_BAD_VM_EXECUTION_CONTROLS_ERROR,
	VMCS_INSTR_VMENTRY_WITH_EVENTS_BLOCKED_BY_MOV_SS_ERROR, /* VMENTER */
	VMCS_INSTR_BAD_ERROR_CODE,                              /* Bad error code */
	VMCS_INSTR_INVALIDATION_WITH_INVALID_OPERAND            /* INVEPT, INVVPID */
} vmcs_instruction_error_t;

vmcs_object_t *vmcs_act_create(guest_cpu_handle_t gcpu);

/*
 *  Functions which are not a part ofg general VMCS API,
 *  but are specific to VMCS applied to real hardware
 */
void vmcs_clear_cache(vmcs_object_t *);
void vmcs_activate(vmcs_object_t *);
void vmcs_deactivate(vmcs_object_t *);
boolean_t vmcs_launch_required(const vmcs_object_t *);
void vmcs_set_launch_required(vmcs_object_t *);
void vmcs_set_launched(vmcs_object_t *);
void vmcs_nmi_handler(vmcs_object_t *vmcs);
void vmcs_write_nmi_window_bit(vmcs_object_t *vmcs, boolean_t value);
boolean_t vmcs_read_nmi_window_bit(vmcs_object_t *vmcs);

void nmi_window_update_before_vmresume(vmcs_object_t *vmcs);
vmcs_instruction_error_t vmcs_last_instruction_error_code(
	const vmcs_object_t *obj,
	const char
	**error_message);

#endif   /* _VMCS_HW_OBJECT_H_ */
