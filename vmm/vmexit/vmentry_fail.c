/*******************************************************************************
* Copyright (c) 2017 Intel Corporation
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
#include "vmm_base.h"
#include "scheduler.h"
#include "vmcs.h"
#include "dbg.h"
#include "host_cpu.h"
#include "guest.h"
#include "gcpu.h"
#include "event.h"

/*
 * VMCS Instruction error
 */
typedef enum {
	VMCS_INSTR_NO_INSTRUCTION_ERROR = 0,						/* VMxxxxx */
	VMCS_INSTR_VMCALL_IN_ROOT_ERROR,						/* VMCALL */
	VMCS_INSTR_VMCLEAR_INVALID_PHYSICAL_ADDRESS_ERROR,				/* VMCLEAR */
	VMCS_INSTR_VMCLEAR_WITH_CURRENT_CONTROLLING_PTR_ERROR,				/* VMCLEAR */
	VMCS_INSTR_VMLAUNCH_WITH_NON_CLEAR_VMCS_ERROR,					/* VMLAUNCH */
	VMCS_INSTR_VMRESUME_WITH_NON_LAUNCHED_VMCS_ERROR,				/* VMRESUME */
	VMCS_INSTR_VMRESUME_WITH_NON_CHILD_VMCS_ERROR,					/* VMRESUME */
	VMCS_INSTR_VMENTER_BAD_CONTROL_FIELD_ERROR, 					/* VMENTER */
	VMCS_INSTR_VMENTER_BAD_MONITOR_STATE_ERROR, 					/* VMENTER */
	VMCS_INSTR_VMPTRLD_INVALID_PHYSICAL_ADDRESS_ERROR,				/* VMPTRLD */
	VMCS_INSTR_VMPTRLD_WITH_CURRENT_CONTROLLING_PTR_ERROR,				/* VMPTRLD */
	VMCS_INSTR_VMPTRLD_WITH_BAD_REVISION_ID_ERROR,					/* VMPTRLD */
	VMCS_INSTR_VMREAD_OR_VMWRITE_OF_UNSUPPORTED_COMPONENT_ERROR,			/* VMREAD */
	VMCS_INSTR_VMWRITE_OF_READ_ONLY_COMPONENT_ERROR,				/* VMWRITE */
	VMCS_INSTR_VMWRITE_INVALID_FIELD_VALUE_ERROR,					/* VMWRITE */
	VMCS_INSTR_VMXON_IN_VMX_ROOT_OPERATION_ERROR,					/* VMXON */
	VMCS_INSTR_VMENTRY_WITH_BAD_OSV_CONTROLLING_VMCS_ERROR, 			/* VMENTER */
	VMCS_INSTR_VMENTRY_WITH_NON_LAUNCHED_OSV_CONTROLLING_VMCS_ERROR,		/* VMENTER */
	VMCS_INSTR_VMENTRY_WITH_NON_ROOT_OSV_CONTROLLING_VMCS_ERROR,			/* VMENTER */
	VMCS_INSTR_VMCALL_WITH_NON_CLEAR_VMCS_ERROR,					/* VMCALL */
	VMCS_INSTR_VMCALL_WITH_BAD_VMEXIT_FIELDS_ERROR, 				/* VMCALL */
	VMCS_INSTR_VMCALL_WITH_INVALID_MSEG_MSR_ERROR,					/* VMCALL */
	VMCS_INSTR_VMCALL_WITH_INVALID_MSEG_REVISION_ERROR, 				/* VMCALL */
	VMCS_INSTR_VMXOFF_WITH_CONFIGURED_SMM_MONITOR_ERROR,				/* VMXOFF */
	VMCS_INSTR_VMCALL_WITH_BAD_SMM_MONITOR_FEATURES_ERROR,				/* VMCALL */
	/* Return from SMM */
	VMCS_INSTR_RETURN_FROM_SMM_WITH_BAD_VM_EXECUTION_CONTROLS_ERROR,
	VMCS_INSTR_VMENTRY_WITH_EVENTS_BLOCKED_BY_MOV_SS_ERROR, 			/* VMENTER */
	VMCS_INSTR_BAD_ERROR_CODE,							/* Bad error code */
	VMCS_INSTR_INVALIDATION_WITH_INVALID_OPERAND					/* INVEPT, INVVPID */
} vmcs_instruction_error_t;

static
const char *g_instr_error_message[] = {
	"VMCS_INSTR_NO_INSTRUCTION_ERROR",						/* VMxxxxx */
	"VMCS_INSTR_VMCALL_IN_ROOT_ERROR",						/* VMCALL */
	"VMCS_INSTR_VMCLEAR_INVALID_PHYSICAL_ADDRESS_ERROR",				/* VMCLEAR */
	"VMCS_INSTR_VMCLEAR_WITH_CURRENT_CONTROLLING_PTR_ERROR",			/* VMCLEAR */
	"VMCS_INSTR_VMLAUNCH_WITH_NON_CLEAR_VMCS_ERROR",				/* VMLAUNCH */
	"VMCS_INSTR_VMRESUME_WITH_NON_LAUNCHED_VMCS_ERROR", 				/* VMRESUME */
	"VMCS_INSTR_VMRESUME_WITH_NON_CHILD_VMCS_ERROR",				/* VMRESUME */
	"VMCS_INSTR_VMENTER_BAD_CONTROL_FIELD_ERROR",					/* VMENTER */
	"VMCS_INSTR_VMENTER_BAD_MONITOR_STATE_ERROR",					/* VMENTER */
	"VMCS_INSTR_VMPTRLD_INVALID_PHYSICAL_ADDRESS_ERROR",				/* VMPTRLD */
	"VMCS_INSTR_VMPTRLD_WITH_CURRENT_CONTROLLING_PTR_ERROR",			/* VMPTRLD */
	"VMCS_INSTR_VMPTRLD_WITH_BAD_REVISION_ID_ERROR",				/* VMPTRLD */
	"VMCS_INSTR_VMREAD_OR_VMWRITE_OF_UNSUPPORTED_COMPONENT_ERROR",			/* VMREAD */
	"VMCS_INSTR_VMWRITE_OF_READ_ONLY_COMPONENT_ERROR",				/* VMWRITE */
	"VMCS_INSTR_VMWRITE_INVALID_FIELD_VALUE_ERROR", 				/* VMWRITE */
	"VMCS_INSTR_VMXON_IN_VMX_ROOT_OPERATION_ERROR", 				/* VMXON */
	"VMCS_INSTR_VMENTRY_WITH_BAD_OSV_CONTROLLING_VMCS_ERROR",			/* VMENTER */
	/* VMENTER */
	"VMCS_INSTR_VMENTRY_WITH_NON_LAUNCHED_OSV_CONTROLLING_VMCS_ERROR",
	"VMCS_INSTR_VMENTRY_WITH_NON_ROOT_OSV_CONTROLLING_VMCS_ERROR",			/* VMENTER */
	"VMCS_INSTR_VMCALL_WITH_NON_CLEAR_VMCS_ERROR",					/* VMCALL */
	"VMCS_INSTR_VMCALL_WITH_BAD_VMEXIT_FIELDS_ERROR",				/* VMCALL */
	"VMCS_INSTR_VMCALL_WITH_INVALID_MSEG_MSR_ERROR",				/* VMCALL */
	"VMCS_INSTR_VMCALL_WITH_INVALID_MSEG_REVISION_ERROR",				/* VMCALL */
	"VMCS_INSTR_VMXOFF_WITH_CONFIGURED_SMM_MONITOR_ERROR",				/* VMXOFF */
	"VMCS_INSTR_VMCALL_WITH_BAD_SMM_MONITOR_FEATURES_ERROR",			/* VMCALL */
	/* Return from SMM */
	"VMCS_INSTR_RETURN_FROM_SMM_WITH_BAD_VM_EXECUTION_CONTROLS_ERROR",
	"VMCS_INSTR_VMENTRY_WITH_EVENTS_BLOCKED_BY_MOV_SS_ERROR",			/* VMENTER */
	"VMCS_INSTR_BAD_ERROR_CODE",							/* Bad error code */
	"VMCS_INSTR_INVALIDATION_WITH_INVALID_OPERAND"					/* INVEPT, INVVPID */
};

static vmcs_instruction_error_t vmcs_last_instruction_error_code(
	vmcs_obj_t obj,
	const char
	**error_message)
{
	uint32_t err = (uint32_t)vmcs_read(obj,
		VMCS_INSTR_ERROR);

	if (error_message) {
		*error_message = (err <= VMCS_INSTR_BAD_ERROR_CODE) ?
				 g_instr_error_message[err] :
				 "UNKNOWN VMCS_INSTR_ERROR";
	}

	return (vmcs_instruction_error_t)err;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmentry_failure_function
*  PURPOSE  : Called upon VMENTER failure
*  ARGUMENTS: uint64_t flag - value of processor flags register
*  RETURNS  : void
*  NOTES    : is not VMEXIT
*--------------------------------------------------------------------------*/
void vmentry_failure_function(uint64_t flags)
{
	guest_cpu_handle_t gcpu = get_current_gcpu();
	const char *err = NULL;
	vmcs_instruction_error_t code;

	VMM_ASSERT_EX(gcpu, "gcpu is NULL in vmentry failure function\n");

	gcpu->is_vmentry_fail = 1;
	vmcs_clear_cache(gcpu->vmcs);

	code = vmcs_last_instruction_error_code(gcpu->vmcs, &err);

	print_panic("CPU%d: VMENTRY Failed\n", host_cpu_id());
	print_panic("FLAGS=0x%llX (zf=%d cf=%d) ErrorCode=0x%X Desc=%s\n",
		flags,!!(flags & RFLAGS_ZF), !!(flags & RFLAGS_CF), code, err);
	print_panic("The previous VMEXIT Reason = %llu.\n", vmcs_read(gcpu->vmcs, VMCS_EXIT_REASON));

	event_raise(gcpu, EVENT_VMENTRY_FAIL, NULL);

	VMM_DEADLOOP();
}

