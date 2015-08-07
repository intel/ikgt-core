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

#include "mon_defs.h"
#include "vmcs_api.h"
#include "guest_cpu.h"
#include "isr.h"
#include "vmexit.h"
#include "hw_utils.h"
#include "ipc.h"
#include "guest_cpu_vmenter_event.h"
#include "em64t_defs.h"
#include "mon_events_data.h"
#include "vmdb.h"
#include "vmx_ctrl_msrs.h"
#include "vmx_nmi.h"
#include "file_codes.h"

#define MON_DEADLOOP() \
	MON_DEADLOOP_LOG(VMEXIT_INTERRUPT_EXCEPTION_NMI_C)
#define MON_ASSERT(__condition) \
	MON_ASSERT_LOG(VMEXIT_INTERRUPT_EXCEPTION_NMI_C, __condition)

static vmexit_handling_status_t vmexit_nmi_window(guest_cpu_handle_t gcpu);
static vmexit_handling_status_t
vmexit_software_interrupt_exception_nmi(guest_cpu_handle_t gcpu);

static
boolean_t page_fault(guest_cpu_handle_t gcpu, vmcs_object_t *vmcs)
{
	ia32_vmx_exit_qualification_t qualification;
	event_gcpu_page_fault_data_t data;

	qualification.uint64 =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);
	mon_memset(&data, 0, sizeof(data));
	data.pf_address = qualification.page_fault.address;
	data.pf_error_code = mon_vmcs_read(vmcs,
		VMCS_EXIT_INFO_EXCEPTION_ERROR_CODE);
	data.pf_processed = FALSE;

	event_raise(EVENT_GCPU_PAGE_FAULT, gcpu, &data);

	/* TODO: move resolution to common handler */
	if (data.pf_processed) {
		gcpu_vmexit_exception_resolve(gcpu);
	}

	/* Return FALSE in case when VTLB recognized as NATIVE and in case there is
	 * no event hander registered */
	return data.pf_processed;
}

void vmexit_nmi_exception_handlers_install(guest_id_t guest_id)
{
	vmexit_install_handler(guest_id,
		vmexit_software_interrupt_exception_nmi,
		IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI);

	vmexit_install_handler(guest_id,
		nmi_window_vmexit_handler, IA32_VMX_EXIT_NMI_WINDOW);
}

void check_and_set_nmi_blocking(vmcs_object_t *vmcs)
{
	ia32_vmx_vmcs_vmexit_info_idt_vectoring_t idt_vectoring_info;
	ia32_vmx_vmcs_vmexit_info_interrupt_info_t vmexit_exception_info;
	ia32_vmx_vmcs_guest_interruptibility_t guest_interruptibility;

	idt_vectoring_info.uint32 = (uint32_t)mon_vmcs_read(vmcs,
		VMCS_EXIT_INFO_IDT_VECTORING);
	vmexit_exception_info.uint32 = (uint32_t)mon_vmcs_read(vmcs,
		VMCS_EXIT_INFO_EXCEPTION_INFO);

	if (idt_vectoring_info.bits.valid || !vmexit_exception_info.bits.valid
	    || !vmexit_exception_info.bits.nmi_unblocking_due_to_iret) {
		return;
	}

	if ((vmexit_exception_info.bits.interrupt_type ==
	     VMEXIT_INTERRUPT_TYPE_EXCEPTION)
	    && (vmexit_exception_info.bits.vector ==
		IA32_EXCEPTION_VECTOR_DOUBLE_FAULT)) {
		return;
	}

	guest_interruptibility.uint32 = (uint32_t)mon_vmcs_read(vmcs,
		VMCS_GUEST_INTERRUPTIBILITY);
	guest_interruptibility.bits.block_nmi = 1;
	mon_vmcs_write(vmcs, VMCS_GUEST_INTERRUPTIBILITY,
		(uint64_t)guest_interruptibility.uint32);
}

vmexit_handling_status_t vmexit_software_interrupt_exception_nmi(guest_cpu_handle_t
								 gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ia32_vmx_vmcs_vmexit_info_interrupt_info_t vmexit_exception_info;
	boolean_t unsupported_exception = FALSE;
	boolean_t handled_exception = TRUE;

	vmexit_exception_info.uint32 = (uint32_t)mon_vmcs_read(vmcs,
		VMCS_EXIT_INFO_EXCEPTION_INFO);

	/* no exceptions allowed under emulator */
	MON_ASSERT((VMEXIT_INTERRUPT_TYPE_EXCEPTION !=
		    vmexit_exception_info.bits.interrupt_type)
		|| (TRUE == gcpu_is_native_execution(gcpu)));

	check_and_set_nmi_blocking(vmcs);

	switch (vmexit_exception_info.bits.interrupt_type) {
	case VMEXIT_INTERRUPT_TYPE_EXCEPTION:
		switch (vmexit_exception_info.bits.vector) {
		case IA32_EXCEPTION_VECTOR_MACHINE_CHECK:
			unsupported_exception = TRUE;
			break;

		case IA32_EXCEPTION_VECTOR_DEBUG_BREAKPOINT:
			handled_exception = vmdb_exception_handler(gcpu);
			break;

		case IA32_EXCEPTION_VECTOR_PAGE_FAULT:
			/* flat page tables support */
		{
			em64t_cr0_t cr0;
			cr0.uint64 =
				gcpu_get_guest_visible_control_reg(gcpu,
					IA32_CTRL_CR0);

			{
				handled_exception = page_fault(gcpu, vmcs);
			}
		}
		break;

		default:       /* unsupported exception */
			handled_exception = FALSE;
			break;
		}
		break;

	case VMEXIT_INTERRUPT_TYPE_NMI:
		/* call NMI handler */
		handled_exception = nmi_vmexit_handler(gcpu);
		break;

	default:
		unsupported_exception = TRUE;
		break;
	}

	if (TRUE == unsupported_exception) {
		MON_LOG(mask_anonymous, level_trace,
			"Unsupported interrupt/exception (%d) in ",
			vmexit_exception_info.bits.vector);
		PRINT_GCPU_IDENTITY(gcpu);
		MON_DEADLOOP();
	}

	return handled_exception ? VMEXIT_HANDLED : VMEXIT_NOT_HANDLED;
}

static
void vmexit_change_exit_reason_from_nmi_window_to_nmi(guest_cpu_handle_t gcpu)
{
	vmcs_hierarchy_t *hierarchy = gcpu_get_vmcs_hierarchy(gcpu);
	vmcs_object_t *merged_vmcs = vmcs_hierarchy_get_vmcs(hierarchy,
		VMCS_MERGED);
	ia32_vmx_exit_reason_t reason;
	ia32_vmx_vmcs_vmexit_info_interrupt_info_t vmexit_exception_info;

	/* Change reason */
	reason.uint32 = (uint32_t)mon_vmcs_read(merged_vmcs,
		VMCS_EXIT_INFO_REASON);
	reason.bits.basic_reason =
		IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI;
	MON_LOG(mask_anonymous,
		level_trace,
		"%s: Updaing VMExit reason to %d\n",
		__FUNCTION__,
		reason.bits.basic_reason);
	vmcs_write_nocheck(merged_vmcs, VMCS_EXIT_INFO_REASON, reason.uint32);
	MON_ASSERT((uint32_t)mon_vmcs_read(merged_vmcs,
			VMCS_EXIT_INFO_REASON) ==
		reason.uint32);

	/* Change exception info */
	vmexit_exception_info.uint32 = 0;
	vmexit_exception_info.bits.vector = IA32_EXCEPTION_VECTOR_NMI;
	vmexit_exception_info.bits.interrupt_type = VMEXIT_INTERRUPT_TYPE_NMI;
	vmexit_exception_info.bits.valid = 1;
	vmcs_write_nocheck(merged_vmcs, VMCS_EXIT_INFO_EXCEPTION_INFO,
		vmexit_exception_info.uint32);
}

vmexit_handling_status_t vmexit_nmi_window(guest_cpu_handle_t gcpu)
{
	boolean_t handled = ipc_nmi_window_vmexit_handler(gcpu);

	if (handled) {
		return VMEXIT_HANDLED;
	}

	MON_DEADLOOP();

	/* Check the case when level-1 mon requested NMI-Window exiting */
	if (gcpu_get_guest_level(gcpu) == GUEST_LEVEL_2) {
		vmcs_hierarchy_t *hierarchy = gcpu_get_vmcs_hierarchy(gcpu);
		vmcs_object_t *level1_vmcs =
			vmcs_hierarchy_get_vmcs(hierarchy, VMCS_LEVEL_1);
		processor_based_vm_execution_controls_t ctrls;

		ctrls.uint32 = (uint32_t)mon_vmcs_read(level1_vmcs,
			VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
		if (ctrls.bits.nmi_window) {
			/* Level1 requested NMI Window vmexit, don't change anything in
			 * vmexit information. */
			return VMEXIT_NOT_HANDLED;
		}
	}

	/* In all other cases, change event to NMI */
	vmexit_change_exit_reason_from_nmi_window_to_nmi(gcpu);

	return VMEXIT_NOT_HANDLED;
}
