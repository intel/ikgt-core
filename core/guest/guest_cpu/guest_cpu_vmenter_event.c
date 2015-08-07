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
#include "vmcs_actual.h"
#include "vmx_ctrl_msrs.h"
#include "guest_cpu.h"
#include "guest_cpu_internal.h"
#include "scheduler.h"
#include "isr.h"
#include "guest_cpu_vmenter_event.h"
#include "mon_dbg.h"
#include "libc.h"
#include "ipc.h"
#include "file_codes.h"
#include "mon_callback.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(GUEST_CPU_VMENTER_EVENT_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(GUEST_CPU_VMENTER_EVENT_C,     \
	__condition)

/*----------------------- Local Types and Variables --------------------------*/
typedef enum {
	EXCEPTION_CLASS_BENIGN = 0,
	EXCEPTION_CLASS_CONTRIBUTORY = 1,
	EXCEPTION_CLASS_PAGE_FAULT = 2,
	EXCEPTION_CLASS_TRIPLE_FAULT = 3
} exception_class_t;

typedef enum {
	INJECT_2ND_EXCEPTION,
	INJECT_DOUBLE_FAULT,
	TEAR_DOWN_GUEST
} idt_resolution_action_t;

static idt_resolution_action_t idt_resolution_table[4][4] = {
	{ INJECT_2ND_EXCEPTION, INJECT_2ND_EXCEPTION, INJECT_2ND_EXCEPTION,
	  TEAR_DOWN_GUEST },
	{ INJECT_2ND_EXCEPTION, INJECT_DOUBLE_FAULT,  INJECT_2ND_EXCEPTION,
	  TEAR_DOWN_GUEST },
	{ INJECT_2ND_EXCEPTION, INJECT_DOUBLE_FAULT,  INJECT_DOUBLE_FAULT,
	  TEAR_DOWN_GUEST },
	{ TEAR_DOWN_GUEST,	TEAR_DOWN_GUEST,      TEAR_DOWN_GUEST,
	  TEAR_DOWN_GUEST }
};

/*-------------- Forward declarations for local functions --------------------*/

static
exception_class_t vector_to_exception_class(vector_id_t vector_id);
static
void gcpu_reinject_idt_exception(guest_cpu_handle_t gcpu,
				 ia32_vmx_vmcs_vmexit_info_idt_vectoring_t
				 idt_vectoring_info);
static
void gcpu_reinject_vmexit_exception(guest_cpu_handle_t gcpu,
				    ia32_vmx_vmcs_vmexit_info_interrupt_info_t
				    vmexit_exception_info);

INLINE void
copy_exception_to_vmenter_exception(ia32_vmx_vmcs_vmenter_interrupt_info_t *
				    vmenter_exception,
				    uint32_t source_exception)
{
	vmenter_exception->uint32 = source_exception;
	vmenter_exception->bits.reserved = 0;
}

/*------------------------------- Code Starts Here ---------------------------*/

/*---------------------------------------------------------------------------*
*  FUNCTION : vmentry_inject_event
*  PURPOSE  : Inject interrupt/exception into guest if allowed, otherwise
*           : set NMI/Interrupt window
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*           : vmenter_event_t  *p_event, function assumes valid input,
*  RETURNS  : TRUE if event was injected, FALSE
*  NOTES    : no checkings are done for event validity
*---------------------------------------------------------------------------*/
boolean_t gcpu_inject_event(guest_cpu_handle_t gcpu, vmenter_event_t *p_event)
{
	vmcs_object_t *vmcs;
	ia32_vmx_vmcs_vmexit_info_idt_vectoring_t idt_vectoring_info;
	boolean_t injection_allowed = TRUE;
	const virtual_cpu_id_t *vcpu_id;

	MON_ASSERT(gcpu);
	MON_ASSERT(0 == GET_EXCEPTION_RESOLUTION_REQUIRED_FLAG(gcpu));

	vmcs = mon_gcpu_get_vmcs(gcpu);
	idt_vectoring_info.uint32 =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_IDT_VECTORING);

	if (1 == idt_vectoring_info.bits.valid) {
		injection_allowed = FALSE;
	} else {
		ia32_vmx_vmcs_guest_interruptibility_t guest_interruptibility;
		guest_interruptibility.uint32 =
			(uint32_t)mon_vmcs_read(vmcs,
				VMCS_GUEST_INTERRUPTIBILITY);

		switch (p_event->interrupt_info.bits.interrupt_type) {
		case VMENTER_INTERRUPT_TYPE_EXTERNAL_INTERRUPT:
			if (1 ==
			    guest_interruptibility.bits.block_next_instruction
			    ||
			    1 ==
			    guest_interruptibility.bits.block_stack_segment) {
				injection_allowed = FALSE;
			}
			break;

		case VMENTER_INTERRUPT_TYPE_NMI:
			if (1 == guest_interruptibility.bits.block_nmi ||
			    1 ==
			    guest_interruptibility.bits.block_stack_segment) {
				injection_allowed = FALSE;
			}
			break;

		case VMENTER_INTERRUPT_TYPE_HARDWARE_EXCEPTION:
		case VMENTER_INTERRUPT_TYPE_SOFTWARE_INTERRUPT:
		case VMENTER_INTERRUPT_TYPE_PRIVILEGED_SOFTWARE_INTERRUPT:
		case VMENTER_INTERRUPT_TYPE_SOFTWARE_EXCEPTION:
			if (1 ==
			    guest_interruptibility.bits.block_stack_segment) {
				if (IA32_EXCEPTION_VECTOR_BREAKPOINT ==
				    p_event->interrupt_info.bits.vector
				    || IA32_EXCEPTION_VECTOR_DEBUG_BREAKPOINT ==
				    p_event->interrupt_info.bits.vector) {
					injection_allowed = FALSE;
				}
			}

			break;

		default:
			MON_LOG(mask_anonymous, level_trace,
				"Invalid VmEnterInterruptType(%d)\n",
				p_event->interrupt_info.bits.interrupt_type);
			MON_DEADLOOP();
			break;
		}
	}

	if (TRUE == injection_allowed) {
		/* to be on safe side */
		p_event->interrupt_info.bits.deliver_code = 0;

		switch (p_event->interrupt_info.bits.interrupt_type) {
		case VMENTER_INTERRUPT_TYPE_SOFTWARE_INTERRUPT:
		case VMENTER_INTERRUPT_TYPE_PRIVILEGED_SOFTWARE_INTERRUPT:
		case VMENTER_INTERRUPT_TYPE_SOFTWARE_EXCEPTION:
			/* Write the Instruction length field if this is any type of
			 * software interrupt */
			mon_vmcs_write(vmcs, VMCS_ENTER_INSTRUCTION_LENGTH,
				(uint64_t)p_event->instruction_length);
			break;

		case VMENTER_INTERRUPT_TYPE_HARDWARE_EXCEPTION:

			if (TRUE ==
			    isr_error_code_required((vector_id_t)p_event->
				    interrupt_info.bits.vector)) {
				mon_vmcs_write(vmcs,
					VMCS_ENTER_EXCEPTION_ERROR_CODE,
					(uint64_t)p_event->error_code);
				p_event->interrupt_info.bits.deliver_code = 1;
			}
			break;

		case VMENTER_INTERRUPT_TYPE_NMI:
			/* VNMI Support- create an event so VNMI handler can handle it. */
			vcpu_id = mon_guest_vcpu(gcpu);
			MON_ASSERT(vcpu_id);
			report_mon_event(MON_EVENT_NMI,
				(mon_identification_data_t)gcpu,
				(const guest_vcpu_t *)vcpu_id, NULL);
			break;

		default:
			break;
		}

		/* to be on a safe side */
		p_event->interrupt_info.bits.valid = 1;
		p_event->interrupt_info.bits.reserved = 0;

		mon_vmcs_write(vmcs, VMCS_ENTER_INTERRUPT_INFO,
			(uint64_t)(p_event->interrupt_info.uint32));
	} else {
		/* there are conditions which prevent injection of new event,
		 * therefore NMI/interrupt window is established */

		if (VMENTER_INTERRUPT_TYPE_NMI ==
		    p_event->interrupt_info.bits.interrupt_type) {
			/* NMI event cannot be injected, so set NMI-windowing */
			gcpu_set_pending_nmi(gcpu, TRUE);

			/* notify IPC component about inability to inject NMI */
			ipc_mni_injection_failed();
		} else {
			/* interrupt/exception cannot be injected, set
			 * interrupt-windowing */
			gcpu_temp_exceptions_setup(gcpu,
				GCPU_TEMP_EXIT_ON_INTR_UNBLOCK);
		}
	}

	return injection_allowed;
}

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_inject_gp0
*  PURPOSE  : Inject GP with error code 0
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : TRUE if event was injected, FALSE
*---------------------------------------------------------------------------*/
boolean_t mon_gcpu_inject_gp0(guest_cpu_handle_t gcpu)
{
	vmenter_event_t gp_exception;
	vmcs_object_t *vmcs;

	MON_ASSERT(gcpu);

	mon_memset(&gp_exception, 0, sizeof(gp_exception));

	vmcs = mon_gcpu_get_vmcs(gcpu);

	gp_exception.interrupt_info.bits.valid = 1;
	gp_exception.interrupt_info.bits.vector =
		IA32_EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT;
	gp_exception.interrupt_info.bits.interrupt_type =
		VMENTER_INTERRUPT_TYPE_HARDWARE_EXCEPTION;
	gp_exception.interrupt_info.bits.deliver_code = 1;
	gp_exception.instruction_length =
		(uint32_t)mon_vmcs_read(vmcs,
			VMCS_EXIT_INFO_INSTRUCTION_LENGTH);
	gp_exception.error_code = 0;

	return gcpu_inject_event(gcpu, &gp_exception);
}

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_inject_fault
*  PURPOSE  : Inject a fault to guest CPU
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*              int vec               - fault vector
*              uint32_t code           - error code pushed on guest stack
*  RETURNS  : TRUE if event was injected, FALSE
*---------------------------------------------------------------------------*/
boolean_t gcpu_inject_fault(guest_cpu_handle_t gcpu, int vec, uint32_t code)
{
	vmcs_object_t *vmcs;
	vmenter_event_t e;

	MON_ASSERT(gcpu);
	vmcs = mon_gcpu_get_vmcs(gcpu);

	mon_memset(&e, 0, sizeof(e));

	e.interrupt_info.bits.valid = 1;
	e.interrupt_info.bits.vector = vec;

	e.interrupt_info.bits.interrupt_type =
		VMENTER_INTERRUPT_TYPE_HARDWARE_EXCEPTION;

	e.instruction_length =
		(uint32_t)mon_vmcs_read(vmcs,
			VMCS_EXIT_INFO_INSTRUCTION_LENGTH);

	if (vec != IA32_EXCEPTION_VECTOR_DEBUG_BREAKPOINT) {
		e.interrupt_info.bits.deliver_code = 1;
		e.error_code = code;
	}

	if (vec == IA32_EXCEPTION_VECTOR_VIRTUAL_EXCEPTION) {
		/* no error code delivered */
		e.interrupt_info.bits.deliver_code = 0;
		e.interrupt_info.bits.reserved = 0;
	}

	return gcpu_inject_event(gcpu, &e);
}

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_inject_nmi
*  PURPOSE  : Inject NMI into guest if allowed, otherwise set NMI window
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : TRUE if event was injected, FALSE
*---------------------------------------------------------------------------*/
boolean_t gcpu_inject_nmi(guest_cpu_handle_t gcpu)
{
	vmenter_event_t nmi_event;

	MON_ASSERT(gcpu);

	mon_memset(&nmi_event, 0, sizeof(nmi_event));

	nmi_event.interrupt_info.bits.valid = 1;
	nmi_event.interrupt_info.bits.vector = IA32_EXCEPTION_VECTOR_NMI;
	nmi_event.interrupt_info.bits.interrupt_type =
		VMENTER_INTERRUPT_TYPE_NMI;
	/* no error code delivered */
	nmi_event.interrupt_info.bits.deliver_code = 0;
	return gcpu_inject_event(gcpu, &nmi_event);
}

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_inject_double_fault
*  PURPOSE  : Inject Double Fault exception into guest if allowed,
*           :  otherwise set Interruption window
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : TRUE if event was injected, FALSE
*---------------------------------------------------------------------------*/
boolean_t gcpu_inject_double_fault(guest_cpu_handle_t gcpu)
{
	vmenter_event_t double_fault_event;

	MON_ASSERT(gcpu);

	mon_memset(&double_fault_event, 0, sizeof(double_fault_event));

	double_fault_event.interrupt_info.bits.valid = 1;
	double_fault_event.interrupt_info.bits.vector =
		IA32_EXCEPTION_VECTOR_DOUBLE_FAULT;
	double_fault_event.interrupt_info.bits.interrupt_type =
		VMENTER_INTERRUPT_TYPE_HARDWARE_EXCEPTION;
	double_fault_event.interrupt_info.bits.deliver_code = 1;
	double_fault_event.error_code = 0;

	return gcpu_inject_event(gcpu, &double_fault_event);
}

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_set_pending_nmi
*  PURPOSE  : Cause NMI VMEXIT be invoked immediately when NMI blocking
*             finished
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*           : boolean_t value
*  RETURNS  : void
*---------------------------------------------------------------------------*/
void gcpu_set_pending_nmi(guest_cpu_handle_t gcpu, boolean_t value)
{
	vmcs_object_t *vmcs;

	MON_ASSERT(gcpu);

	vmcs = mon_gcpu_get_vmcs(gcpu);
	vmcs_write_nmi_window_bit(vmcs, value);
}

/*---------------------------------------------------------------------------*
*  FUNCTION : vector_to_exception_class
*  PURPOSE  : Translate vector ID to exception "co-existence" class
*  ARGUMENTS: vector_id_t vector_id
*  RETURNS  : exception_class_t
*---------------------------------------------------------------------------*/
exception_class_t vector_to_exception_class(vector_id_t vector_id)
{
	exception_class_t ex_class;

	switch (vector_id) {
	case IA32_EXCEPTION_VECTOR_PAGE_FAULT:
		ex_class = EXCEPTION_CLASS_PAGE_FAULT;
		break;

	case IA32_EXCEPTION_VECTOR_DIVIDE_ERROR:
	case IA32_EXCEPTION_VECTOR_INVALID_TASK_SEGMENT_SELECTOR:
	case IA32_EXCEPTION_VECTOR_SEGMENT_NOT_PRESENT:
	case IA32_EXCEPTION_VECTOR_STACK_SEGMENT_FAULT:
	case IA32_EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT:
		ex_class = EXCEPTION_CLASS_CONTRIBUTORY;
		break;

	case IA32_EXCEPTION_VECTOR_DOUBLE_FAULT:

		MON_LOG(mask_anonymous, level_trace,
			"FATAL ERROR: Tripple Fault Occured\n");
		/* have to tear down the guest */
		ex_class = EXCEPTION_CLASS_TRIPLE_FAULT;
		MON_DEADLOOP();
		break;

	default:
		ex_class = EXCEPTION_CLASS_BENIGN;
		break;
	}
	return ex_class;
}

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_reinject_vmexit_exception
*  PURPOSE  : Reinject VMEXIT exception and optionally errcode, instruction
*             length
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU . Argument is assumed valid.
*             Caller function validates.
*           : ia32_vmx_vmcs_vmexit_info_interrupt_info_t vmexit_exception_info
*  RETURNS  : void
*---------------------------------------------------------------------------*/
void gcpu_reinject_vmexit_exception(guest_cpu_handle_t gcpu,
				    ia32_vmx_vmcs_vmexit_info_interrupt_info_t
				    vmexit_exception_info)
{
	vmenter_event_t event;
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	copy_exception_to_vmenter_exception(&event.interrupt_info,
		vmexit_exception_info.uint32);

	/* some exceptions require error code */
	if (vmexit_exception_info.bits.error_code_valid) {
		event.error_code = mon_vmcs_read(vmcs,
			VMCS_EXIT_INFO_EXCEPTION_ERROR_CODE);
	}

	if (VMEXIT_INTERRUPT_TYPE_SOFTWARE_EXCEPTION ==
	    vmexit_exception_info.bits.interrupt_type) {
		event.instruction_length =
			(uint32_t)mon_vmcs_read(vmcs,
				VMCS_EXIT_INFO_INSTRUCTION_LENGTH);
	}

	gcpu_inject_event(gcpu, &event);
}

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_reinject_idt_exception
*  PURPOSE  : Reinject IDT Vectoring exception and optionally errcode,
*             instruction length
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*           : ia32_vmx_vmcs_vmexit_info_idt_vectoring_t idt_vectoring_info
*  RETURNS  : void
*---------------------------------------------------------------------------*/
void gcpu_reinject_idt_exception(guest_cpu_handle_t gcpu,
				 ia32_vmx_vmcs_vmexit_info_idt_vectoring_t
				 idt_vectoring_info)
{
	vmenter_event_t event;
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	/* re-inject the event, by copying IDT vectoring info into VMENTER */
	copy_exception_to_vmenter_exception(&event.interrupt_info,
		idt_vectoring_info.uint32);

	/* some exceptions require error code */
	if (idt_vectoring_info.bits.error_code_valid) {
		event.error_code =
			mon_vmcs_read(vmcs,
				VMCS_EXIT_INFO_IDT_VECTORING_ERROR_CODE);
	}

	/* SW exceptions and interrupts require instruction length to be injected */
	switch (idt_vectoring_info.bits.interrupt_type) {
	case IDT_VECTORING_INTERRUPT_TYPE_SOFTWARE_INTERRUPT:
	case IDT_VECTORING_INTERRUPT_TYPE_SOFTWARE_EXCEPTION:
	case IDT_VECTORING_INTERRUPT_TYPE_PRIVILEGED_SOFTWARE_INTERRUPT:
		event.instruction_length =
			(uint32_t)mon_vmcs_read(vmcs,
				VMCS_EXIT_INFO_INSTRUCTION_LENGTH);
		break;
	default:
		break;
	}

	/* clear IDT valid, so we can re-inject the event */
	mon_vmcs_write(vmcs, VMCS_EXIT_INFO_IDT_VECTORING, 0);

	/* finally inject the event */
	gcpu_inject_event(gcpu, &event);
}

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_vmexit_exception_resolve
*  PURPOSE  : Called if exception, caused VMEXIT was resolved by MON code
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : void
*---------------------------------------------------------------------------*/
void gcpu_vmexit_exception_resolve(guest_cpu_handle_t gcpu)
{
	ia32_vmx_vmcs_vmexit_info_idt_vectoring_t idt_vectoring_info;
	vmcs_object_t *vmcs;

	MON_ASSERT(gcpu);

	vmcs = mon_gcpu_get_vmcs(gcpu);

	CLR_EXCEPTION_RESOLUTION_REQUIRED_FLAG(gcpu);

	idt_vectoring_info.uint32 =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_IDT_VECTORING);
	if (1 == idt_vectoring_info.bits.valid) {
		gcpu_reinject_idt_exception(gcpu, idt_vectoring_info);
	} else {
		ia32_vmx_vmcs_vmexit_info_interrupt_info_t vmexit_exception_info;

		vmexit_exception_info.uint32 =
			(uint32_t)mon_vmcs_read(vmcs,
				VMCS_EXIT_INFO_EXCEPTION_INFO);
		if (vmexit_exception_info.bits.valid == 1
		    && vmexit_exception_info.bits.nmi_unblocking_due_to_iret ==
		    1
		    && vmexit_exception_info.bits.vector !=
		    IA32_EXCEPTION_VECTOR_DOUBLE_FAULT) {
			ia32_vmx_vmcs_guest_interruptibility_t
				guest_interruptibility;
			guest_interruptibility.uint32 = 0;
			guest_interruptibility.bits.block_nmi = 1;
			vmcs_update(vmcs,
				VMCS_GUEST_INTERRUPTIBILITY,
				(uint64_t)guest_interruptibility.uint32,
				(uint64_t)guest_interruptibility.uint32);
		}
	}
}

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_vmexit_exception_reflect
*  PURPOSE  : Reflect exception to guest.
*           : Called if exception, caused VMEXIT was caused by Guest SW
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : void
*---------------------------------------------------------------------------*/
void gcpu_vmexit_exception_reflect(guest_cpu_handle_t gcpu)
{
	/* 1st exception */
	ia32_vmx_vmcs_vmexit_info_idt_vectoring_t idt_vectoring_info;
	/* 2nd exception */
	ia32_vmx_vmcs_vmexit_info_interrupt_info_t vmexit_exception_info;
	exception_class_t exception1_class;
	exception_class_t exception2_class;
	idt_resolution_action_t action;
	vmcs_object_t *vmcs;
	boolean_t inject_exception = FALSE;

	MON_ASSERT(gcpu);

	vmcs = mon_gcpu_get_vmcs(gcpu);

	CLR_EXCEPTION_RESOLUTION_REQUIRED_FLAG(gcpu);

	idt_vectoring_info.uint32 =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_IDT_VECTORING);
	vmexit_exception_info.uint32 =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_EXCEPTION_INFO);

	if (1 == idt_vectoring_info.bits.valid) {
		exception1_class =
			vector_to_exception_class((vector_id_t)idt_vectoring_info.
				bits.vector);
		exception2_class =
			vector_to_exception_class((vector_id_t)vmexit_exception_info.
				bits.vector);

		action =
			idt_resolution_table[exception1_class][exception2_class];

		/* clear IDT valid, for we can re-inject the event */
		mon_vmcs_write(vmcs, VMCS_EXIT_INFO_IDT_VECTORING, 0);

		switch (action) {
		case INJECT_2ND_EXCEPTION:
			/* inject 2nd exception, by copying VMEXIT exception info into
			 * VMENTER */
			inject_exception = TRUE;
			break;
		case INJECT_DOUBLE_FAULT:
			gcpu_inject_double_fault(gcpu);
			break;
		case TEAR_DOWN_GUEST:
			/* TBD */
			MON_LOG(mask_anonymous, level_trace,
				"Triple Fault occured. Tear down Guest CPU ");
			PRINT_GCPU_IDENTITY(gcpu);
			MON_LOG(mask_anonymous, level_trace, "\n");
			break;
		}
	} else {
		inject_exception = TRUE;
	}

	if (inject_exception) {
		if (vmexit_exception_info.bits.vector ==
		    IA32_EXCEPTION_VECTOR_PAGE_FAULT) {
			/* CR2 information resides in qualification */
			ia32_vmx_exit_qualification_t qualification;
			qualification.uint64 =
				mon_vmcs_read(vmcs,
					VMCS_EXIT_INFO_QUALIFICATION);
			gcpu_set_control_reg(gcpu, IA32_CTRL_CR2,
				qualification.page_fault.address);
		}

		/* re-inject the event, by copying VMEXIT exception info into VMENTER */
		gcpu_reinject_vmexit_exception(gcpu, vmexit_exception_info);
	}
}

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_inject_invalid_opcode_exception
*  PURPOSE  : Inject invalid opcode exception
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : TRUE if event was injected, FALSE if event was not injected.
*---------------------------------------------------------------------------*/
boolean_t mon_gcpu_inject_invalid_opcode_exception(guest_cpu_handle_t gcpu)
{
	vmenter_event_t ud_exception;
	em64t_rflags_t rflags;
	boolean_t inject_allowed;

	MON_ASSERT(gcpu);

	mon_memset(&ud_exception, 0, sizeof(ud_exception));

	ud_exception.interrupt_info.bits.valid = 1;
	ud_exception.interrupt_info.bits.vector =
		IA32_EXCEPTION_VECTOR_UNDEFINED_OPCODE;
	ud_exception.interrupt_info.bits.interrupt_type =
		VMENTER_INTERRUPT_TYPE_HARDWARE_EXCEPTION;
	ud_exception.interrupt_info.bits.deliver_code = 0;
	ud_exception.instruction_length = 0;
	ud_exception.error_code = 0;

	inject_allowed = gcpu_inject_event(gcpu, &ud_exception);
	if (inject_allowed) {
		rflags.uint64 = gcpu_get_native_gp_reg(gcpu, IA32_REG_RFLAGS);
		rflags.bits.rf = 1;
		gcpu_set_native_gp_reg(gcpu, IA32_REG_RFLAGS, rflags.uint64);
	}
	return inject_allowed;
}
