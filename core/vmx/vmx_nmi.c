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

#include "file_codes.h"
/* need these defines for guest.h -> array_iterators.h */
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMX_NMI_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMX_NMI_C, __condition)

#include "mon_defs.h"
#include "mon_dbg.h"
#include "common_libc.h"
#include "memory_allocator.h"
#include "hw_utils.h"
#include "isr.h"
#include "mon_objects.h"
#include "guest.h"
#include "guest_cpu.h"
#include "guest_cpu_vmenter_event.h"
#include "ipc.h"
#include "vmexit.h"
#include "vmx_ctrl_msrs.h"
#include "vmx_nmi.h"

/*
 * +---------+---------+---------+---------+---------------+-----------+
 * |  input  |  input  |  input  |output   |     output    |  output   |
 * +---------+---------+---------+---------+---------------+-----------+
 * |         |         | X-lated |X-lated  |               |           |
 * | Reason  | Platform| Reason  |reason   | Local Action  |Deliver to |
 * |         |   NMI   |         |requested|               | upper MON |
 * |         |         |         |by Lvl-1 |               |           |
 * +---------+---------+---------+---------+---------------+-----------+
 * |   NMI   |   No    |No reason|   N/A   |    Dismiss    |     No    |
 * +---------+---------+---------+---------+---------------+-----------+
 * |   NMI   |   Yes   |   NMI   |   No    |Inject to guest|     No    |
 * +---------+---------+---------+---------+---------------+-----------+
 * |   NMI   |   Yes   |   NMI   |   Yes   |Emulate x-lated|     Yes   |
 * |         |         |         |         |vmexit to lvl-1|           |
 * +---------+---------+---------+---------+---------------+-----------+
 * | NMI-Win |   No    | NMI-Win |   No    |    Dismiss    |     No    |
 * +---------+---------+---------+---------+---------------+-----------+
 * | NMI-Win |   No    | NMI-Win |   Yes   |Emulate x-lated|     Yes   |
 * |         |         |         |         |vmexit to lvl-1|           |
 * +---------+---------+---------+---------+---------------+-----------+
 * | NMI-Win |   Yes   |   NMI   |   No    |Inject to guest|     Yes   |
 * +---------+---------+---------+---------+---------------+-----------+
 * | NMI-Win |   Yes   |   NMI   |   Yes   |Emulate x-lated|     Yes   |
 * |         |         |         |         |vmexit to lvl-1|           |
 * +---------+---------+---------+---------+---------------+-----------+
 */


#define XLAT_NMI_VMEXIT_REASON(__nmi_exists) ((__nmi_exists) ?                 \
	IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI :          \
	IA32_VMX_EXIT_BASIC_REASON_COUNT)

#define XLAT_NMI_WINDOW_VMEXIT_REASON(__nmi_exists) ((__nmi_exists) ?          \
	IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI :          \
	IA32_VMX_EXIT_NMI_WINDOW)

#define NMI_EXISTS_ON_GCPU(__gcpu)                                             \
	(nmi_is_pending_this() && guest_is_nmi_owner(mon_gcpu_guest_handle(    \
		__gcpu)))

#define COPY_VMCS1_HOST_TO_MERGE_GUEST()    /* */

/*--------------------------------Local Variables-----------------------------*/
static boolean_t *nmi_array;

/*-------------------------Local functions declarations-----------------------*/
vmexit_handling_status_t nmi_process_translated_reason(guest_cpu_handle_t gcpu,
						       ia32_vmx_exit_basic_reason_t
						       xlat_reason);
static vmexit_handling_status_t nmi_propagate_nmi(guest_cpu_handle_t gcpu);
static vmexit_handling_status_t nmi_propagate_nmi_window(guest_cpu_handle_t gcpu);
static void nmi_emulate_nmi_vmexit(guest_cpu_handle_t gcpu);
static cpu_id_t nmi_num_of_cores;

/*---------------------------------------------------------------------------*
*                           Initialization
*---------------------------------------------------------------------------*/
boolean_t nmi_manager_initialize(cpu_id_t num_of_cores)
{
	boolean_t success = FALSE;

	do {
		nmi_array = mon_malloc(num_of_cores * sizeof(boolean_t));
		nmi_num_of_cores = num_of_cores;
		if (NULL == nmi_array) {
			break;
		}
		mon_memset(nmi_array, 0, num_of_cores * sizeof(boolean_t));

		success = ipc_initialize(num_of_cores);
	} while (0);

	/* no need to release memory in case of failure, because it is Fatal */

	return success;
}

/*---------------------------------------------------------------------------*
*                            Accessors
*---------------------------------------------------------------------------*/
static void nmi_raise(cpu_id_t cpu_id)
{
	MON_LOG(mask_anonymous, level_trace, "[nmi] Platform nmi on CPU%d\n",
		cpu_id);
	nmi_array[cpu_id] = TRUE;
}

static void nmi_clear(cpu_id_t cpu_id)
{
	nmi_array[cpu_id] = FALSE;
}

static boolean_t nmi_is_pending(cpu_id_t cpu_id)
{
	return nmi_array[cpu_id];
}

void nmi_raise_this(void)
{
	cpu_id_t cpu_id = hw_cpu_id();

	if (cpu_id >= nmi_num_of_cores) {
		MON_LOG(mask_anonymous, level_error,
			"Error: invalid cpu_id.\n");
		return;
	}
	nmi_raise(cpu_id);
}

void nmi_clear_this(void)
{
	cpu_id_t cpu_id = hw_cpu_id();

	if (cpu_id >= nmi_num_of_cores) {
		MON_LOG(mask_anonymous, level_error,
			"Error: invalid cpu_id.\n");
		return;
	}
	nmi_clear(cpu_id);
}

boolean_t nmi_is_pending_this(void)
{
	cpu_id_t cpu_id = hw_cpu_id();

	if (cpu_id >= nmi_num_of_cores) {
		MON_LOG(mask_anonymous, level_error,
			"Error: invalid cpu_id.\n");
		return FALSE;
	}
	return nmi_is_pending(cpu_id);
}

/*---------------------------------------------------------------------------*
*  FUNCTION : nmi_resume_handler()
*  PURPOSE  : If current CPU is platform NMI owner and unhandled platform NMI
*           : exists on current CPU, sets NMI-Window to get VMEXIT asap.
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : void
*---------------------------------------------------------------------------*/
void nmi_resume_handler(guest_cpu_handle_t gcpu)
{
	if (NMI_EXISTS_ON_GCPU(gcpu)) {
		gcpu_set_pending_nmi(gcpu, TRUE);
	}
}

/*---------------------------------------------------------------------------*
*  FUNCTION : nmi_vmexit_handler()
*  PURPOSE  : Process NMI VMEXIT
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : Status which says if VMEXIT was finally handled or
*           : it should be processed by upper layer
*  CALLED   : called as bottom-up local handler
*---------------------------------------------------------------------------*/
vmexit_handling_status_t nmi_vmexit_handler(guest_cpu_handle_t gcpu)
{
	ipc_nmi_vmexit_handler(gcpu);
	return nmi_process_translated_reason(gcpu,
		XLAT_NMI_VMEXIT_REASON
			(NMI_EXISTS_ON_GCPU(gcpu)));
}

/*---------------------------------------------------------------------------*
*  FUNCTION : nmi_window_vmexit_handler()
*  PURPOSE  : Process NMI Window VMEXIT
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : Status which says if VMEXIT was finally handled or
*           : it should be processed by upper layer
*  CALLED   : called as bottom-up local handler
*---------------------------------------------------------------------------*/
vmexit_handling_status_t nmi_window_vmexit_handler(guest_cpu_handle_t gcpu)
{
	ipc_nmi_window_vmexit_handler(gcpu);
	gcpu_set_pending_nmi(gcpu, FALSE);

	return nmi_process_translated_reason(gcpu,
		XLAT_NMI_WINDOW_VMEXIT_REASON
			(NMI_EXISTS_ON_GCPU(gcpu)));
}

vmexit_handling_status_t nmi_process_translated_reason(guest_cpu_handle_t gcpu,
						       ia32_vmx_exit_basic_reason_t
						       xlat_reason)
{
	vmexit_handling_status_t status;

	switch (xlat_reason) {
	case IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI:
		status = nmi_propagate_nmi(gcpu);
		break;
	case IA32_VMX_EXIT_NMI_WINDOW:
		status = nmi_propagate_nmi_window(gcpu);
		break;
	default:
		status = VMEXIT_HANDLED; /* dismiss */
		break;
	}
	return status;
}

/*---------------------------------------------------------------------------*
*  FUNCTION : nmi_propagate_nmi()
*  PURPOSE  : If layered and upper MON requested NMI VMEXIT, emulate it,
*           : else inject it directly to VM
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : Status which says if VMEXIT was finally handled or
*           : it should be processed by upper layer
*---------------------------------------------------------------------------*/
vmexit_handling_status_t nmi_propagate_nmi(guest_cpu_handle_t gcpu)
{
	vmexit_handling_status_t status;

	do {
		if (gcpu_is_vmcs_layered(gcpu)) {
			/* if upper layer requested NMI VMEXIT, emulate NMI VMEXIT into it */
			vmcs_object_t *vmcs1 = gcpu_get_vmcs_layered(gcpu,
				VMCS_LEVEL_1);
			pin_based_vm_execution_controls_t pin_based_vmexit_ctrls;

			pin_based_vmexit_ctrls.uint32 = (uint32_t)mon_vmcs_read(
				vmcs1,
				VMCS_CONTROL_VECTOR_PIN_EVENTS);

			if (pin_based_vmexit_ctrls.bits.nmi) {
				nmi_emulate_nmi_vmexit(gcpu);
				nmi_clear_this();
				status = VMEXIT_NOT_HANDLED;
				break;
			}
		}

		/* here is non-layered case, or level.1 did not request NMI VMEXIT */
		if (gcpu_inject_nmi(gcpu)) {
			nmi_clear_this();
		}

		/* do not deliver to upper level even if NMI was not really injected */
		status = VMEXIT_HANDLED;
	} while (0);

	return status;
}

/*---------------------------------------------------------------------------*
*  FUNCTION : nmi_propagate_nmi_window()
*  PURPOSE  : If layered and upper MON requested NMI-Window VMEXIT, emulate it,
*           : else dismiss it.
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : Status which says if VMEXIT was finally handled or
*           : it should be processed by upper layer
*---------------------------------------------------------------------------*/
vmexit_handling_status_t nmi_propagate_nmi_window(guest_cpu_handle_t gcpu)
{
	vmexit_handling_status_t status;

	do {
		if (gcpu_is_vmcs_layered(gcpu)) {
			/* if upper layer requested NMI VMEXIT, emulate NMI VMEXIT into it */
			vmcs_object_t *vmcs1 = gcpu_get_vmcs_layered(gcpu,
				VMCS_LEVEL_1);
			processor_based_vm_execution_controls_t ctrls;

			ctrls.uint32 = (uint32_t)mon_vmcs_read(vmcs1,
				VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
			if (ctrls.bits.nmi_window) {
				status = VMEXIT_NOT_HANDLED;
				break;
			}
		}

		/* here is non-layered case, or level.1 did not request NMI Window
		 * VMEXIT */
		/* do not deliver NMI Window to upper level */
		status = VMEXIT_HANDLED;
	} while (0);

	return status;
}

/*---------------------------------------------------------------------------*
*  FUNCTION : nmi_emulate_nmi_vmexit()
*  PURPOSE  : Emulates NMI VMEXIT into upper MON
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : void
*---------------------------------------------------------------------------*/
void nmi_emulate_nmi_vmexit(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, VMCS_MERGED);
	uint32_t reason;
	ia32_vmx_vmcs_vmexit_info_interrupt_info_t exception_info;

	MON_CALLTRACE_ENTER();

	reason = (uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_REASON);

	/* change VMEXIT INFO, which is read-only. It is done in cache only */
	/* and should not be writeen to hardware VMCS */
	vmcs_write_nocheck(vmcs, VMCS_EXIT_INFO_REASON, (uint64_t)
		IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI);

	exception_info.uint32 = 0;
	exception_info.bits.vector = IA32_EXCEPTION_VECTOR_NMI;
	exception_info.bits.interrupt_type = IA32_EXCEPTION_VECTOR_NMI;
	exception_info.bits.valid = 1;
	vmcs_write_nocheck(vmcs, VMCS_EXIT_INFO_EXCEPTION_INFO,
		(uint64_t)exception_info.uint32);

	COPY_VMCS1_HOST_TO_MERGE_GUEST();

	MON_CALLTRACE_LEAVE();
}
