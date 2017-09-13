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

#include "vmm_base.h"
#include "vmcs.h"
#include "gcpu.h"
#include "dbg.h"
#include "gcpu_inject_event.h"
#include "host_cpu.h"
#include "guest.h"
#include "vmx_cap.h"
#include "vmexit.h"

static inline void set_block_by_nmi(vmcs_obj_t vmcs)
{
	uint32_t interruptibility = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_INTERRUPTIBILITY);

	// only bit3 (block_by_nmi) might be 1, others must be 0
	VMM_ASSERT_EX(((interruptibility & 0xFFFFFFF7) == 0),
		"unexpected bit in guest interruptibility(=%x)\n", interruptibility);

	/* PIN_NMI_EXIT and PIN_VIRTUAL_NMI have
	 * already checked by the caller function */

	/* uint32_t pin;
	pin = (uint32_t)vmcs_read(vmcs, VMCS_PIN_CTRL);
	if ((pin & PIN_NMI_EXIT) && ((pin & PIN_VIRTUAL_NMI) == 0)) // don't set block_by_nmi in this case.
	{
		// in this case, block_by_nmi will be not set or cleared automatically.
		// instead, when nmi == virtual_nmi, block_by_nmi will be set and cleared automatically
		return;
	} */

	vmcs_write(vmcs, VMCS_GUEST_INTERRUPTIBILITY, 0x8);
}

static inline void clear_block_by_nmi(vmcs_obj_t vmcs)
{
	uint32_t interruptibility = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_INTERRUPTIBILITY);

	// only bit3 (block_by_nmi) might be 1, others must be 0
	VMM_ASSERT_EX(((interruptibility & 0xFFFFFFF7) == 0),
		"unexpected bit in guest interruptibility(=%llx)\n", interruptibility);
	vmcs_write(vmcs, VMCS_GUEST_INTERRUPTIBILITY, 0);
}


// should be called in vmexit_common_handler() for all vmexits
void gcpu_reflect_idt_vectoring_info(guest_cpu_handle_t gcpu)
{
	// no need to assert gcpu, caller (vmexit_common_handler) makes sure it is valid
	vmx_exit_idt_info_t idt_vectoring_info;
	vmx_exit_reason_t exit_reason;
	vmx_exit_qualification_t qual;

	D(VMM_ASSERT(gcpu));

	idt_vectoring_info.uint32 = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_IDT_VECTOR_INFO);
	if (idt_vectoring_info.bits.valid == 0)
		return; // do nothing

	exit_reason.uint32 = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_REASON);
	if (exit_reason.bits.basic_reason == REASON_09_TASK_SWITCH)
	{
		qual.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
		if ((uint32_t)qual.task_switch.source == TASK_SWITCH_TYPE_IDT)
		{
			return; //do nothong
		}
	}

	print_trace("%s(): idt vectoring=0x%x\n", __FUNCTION__, idt_vectoring_info.uint32);

	switch (idt_vectoring_info.bits.interrupt_type)
	{
		case VECTOR_TYPE_EXT_INT:
			gcpu_set_pending_intr(gcpu, (uint8_t)idt_vectoring_info.bits.vector);
			break;
		case VECTOR_TYPE_NMI:
			host_cpu_inc_pending_nmi();
			print_trace("hcpu%d, %s(): nmi=%d\n", host_cpu_id(),
				__FUNCTION__, host_cpu_get_pending_nmi());
			clear_block_by_nmi(gcpu->vmcs);
			break;
		default: // exception & sw interrupt
			// don't inject them. they will occur again since RIP doesn't change
			// I tried to reflect them by injecting them, but actually Android failed to boot.
			break;
	}
}

// should be called in vmexit_common_handler() for some vmexits
void gcpu_check_nmi_iret(guest_cpu_handle_t gcpu)
{
	// no need to assert gcpu, caller (vmexit_common_handler) makes sure it is valid
	vmcs_obj_t vmcs;
	vmx_exit_reason_t reason;
	uint32_t pin;
	vmx_exit_idt_info_t idt_vectoring_info;
	vmx_exit_interrupt_info_t interrupt_info;
	uint32_t uint32;

	D(VMM_ASSERT(gcpu));

	vmcs = gcpu->vmcs;
	pin = (uint32_t)vmcs_read(vmcs, VMCS_PIN_CTRL);
	if ((pin & PIN_NMI_EXIT) && ((pin & PIN_VIRTUAL_NMI) == 0))
		return;

	idt_vectoring_info.uint32 = (uint32_t)vmcs_read(vmcs, VMCS_IDT_VECTOR_INFO);
	if (idt_vectoring_info.bits.valid)
		return;

	reason.uint32 = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_REASON);
	switch (reason.bits.basic_reason)
	{
		case REASON_00_NMI_EXCEPTION:
		case REASON_01_EXT_INT:
			interrupt_info.uint32 = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_INT_INFO);
			if (!interrupt_info.bits.valid)
				return;
			if ((interrupt_info.bits.interrupt_type == VECTOR_TYPE_HW_EXCEPTION) &&
				(interrupt_info.bits.vector == EXCEPTION_DF))
				return;
			uint32 = interrupt_info.uint32;
			break;
		case REASON_48_EPT_VIOLATION:
		case REASON_49_EPT_MISCONFG:
			uint32 = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_QUAL);
			break;
		default:
			return;
			break;
	}
	// here uint32 is the VMCS_EXIT_INT_INFO or VMCS_EXIT_QUAL
	// for different vmexit reasons
	print_trace("%s(): reason=%d, uint32=0x%x\n", __FUNCTION__,
		reason.bits.basic_reason, uint32);
	if (uint32 & 0x00001000) // bit12: nmi unblocking due to iret
		set_block_by_nmi(vmcs);
}

static inline boolean_t vector_is_hw_exception(uint8_t vector)
{
	switch (vector)
	{
		case EXCEPTION_BP:
		case EXCEPTION_OF:
			return FALSE;
			break;
		default:
			return TRUE;
			break;
	}
}

static boolean_t isr_error_code_required(uint8_t vector_id)
{
	switch (vector_id) {
	case EXCEPTION_DF:
	case EXCEPTION_TS:
	case EXCEPTION_NP:
	case EXCEPTION_SS:
	case EXCEPTION_GP:
	case EXCEPTION_PF:
	case EXCEPTION_AC:
		return TRUE;
		break;
	default:
		return FALSE;
		break;
	}
}

void gcpu_inject_exception(guest_cpu_handle_t gcpu, uint8_t vector, uint32_t code)
{
	vmcs_obj_t vmcs;
	uint32_t activity_state;
	vmx_entry_info_t interrupt_info;

	D(VMM_ASSERT(gcpu));
	// #VE is the last exception in IA32
	VMM_ASSERT_EX((vector <= EXCEPTION_VE),
		"invalid exception vector(%u)\n", vector);
	// cannot inject NMI as exception
	VMM_ASSERT_EX((vector != EXCEPTION_NMI),
		"exception vector can not be NMI(2)\n");

	vmcs = gcpu->vmcs;
	activity_state = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_ACTIVITY_STATE);
	// #DB and #MC can be injected in HLT. but for evmm, it is only allowed to inject exception in "active" state
	VMM_ASSERT_EX((activity_state == ACTIVITY_STATE_ACTIVE),
		"invalid guest state(%u)\n", activity_state);

	// since exceptions will be not reflected in gcpu_reflect_idt_vectoring_info(), existing
	// VMCS_ENTRY_INTR_INFO is not checked. overwrite it even if it is valid.
	// that is, only the later excpetion will be injected, the previous excpetion will be lost

	interrupt_info.uint32 = 0;
	interrupt_info.bits.valid = 1;
	interrupt_info.bits.vector = vector;

	if (vector_is_hw_exception(vector))
	{
		interrupt_info.bits.interrupt_type = VECTOR_TYPE_HW_EXCEPTION;
		if (isr_error_code_required(vector))
		{
			// bit[31:15] must be 0
			VMM_ASSERT_EX(((code & 0xFFFF8000) == 0),
				"error code bit[31:15] isn't all zero\n");
			interrupt_info.bits.deliver_code = 1;
			vmcs_write(vmcs, VMCS_ENTRY_ERR_CODE, code);
			// instruction length is not used for hw exception
		}
	}
	else
	{
		interrupt_info.bits.interrupt_type = VECTOR_TYPE_SW_EXCEPTION;
		vmcs_write(vmcs, VMCS_ENTRY_INSTR_LEN,
			vmcs_read(vmcs, VMCS_EXIT_INSTR_LEN));
	}
	vmcs_write(vmcs, VMCS_ENTRY_INTR_INFO, interrupt_info.uint32);
}

void vmexit_nmi_window(guest_cpu_handle_t gcpu)
{
	D(VMM_ASSERT(gcpu));
	// disable nmi window
	vmcs_write(gcpu->vmcs, VMCS_PROC_CTRL1,
		vmcs_read(gcpu->vmcs, VMCS_PROC_CTRL1) &
		~(PROC_NMI_WINDOW_EXIT));

	/* pending nmi will be checked in gcpu_inject_pending_nmi() and nmi window
	** will be enabled/disabled automatically there */
}

void vmexit_intr_window(guest_cpu_handle_t gcpu)
{
	D(VMM_ASSERT(gcpu));
	// disable interrupt window
	vmcs_write(gcpu->vmcs, VMCS_PROC_CTRL1,
		vmcs_read(gcpu->vmcs, VMCS_PROC_CTRL1) &
		~(PROC_INT_WINDOW_EXIT));

	/* pending interrupt will be checked in gcpu_inject_pending_intr() and intr window
	** will be enabled/disabled automatically there */
}
