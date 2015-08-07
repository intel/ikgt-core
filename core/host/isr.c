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
#include "libc.h"
#include "hw_utils.h"
#include "hw_setjmp.h"
#include "trial_exec.h"
#include "guest_cpu.h"
#include "idt.h"
#include "isr.h"
#include "mon_dbg.h"
#include "vmcs_api.h"
#include "scheduler.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(ISR_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(ISR_C, __condition)

extern isr_parameters_on_stack_t *g_exception_stack;

/*------------ local macro definitions ------------ */

#define INTERRUPT_COUNT_VECTORS 256
#define EXCEPTION_COUNT_VECTORS 32

#define ERROR_CODE_EXT_BIT 0x1
#define ERROR_CODE_IN_IDT  0x2
#define ERROR_CODE_TI      0x4

/* Interrupt flag in RFLAGS register */
#define RFLAGS_IF           9

typedef enum {
	INTERRUPT_CLASS,
	ABORT_CLASS,
	FAULT_CLASS,
	TRAP_CLASS,
	RESERVED_CLASS
} exception_class_type_t;

/*---------------- local variables ------------------ */

static func_mon_isr_handler_t isr_table[INTERRUPT_COUNT_VECTORS];

static const char *exception_message[] = {
	"divide error",
	"debug breakpoint",
	"nmi",
	"breakpoint",
	"overflow",
	"bound range exceeded",
	"undefined opcode",
	"no math coprocessor",
	"double fault",
	"reserved 0x09",
	"invalid task segment selector",
	"segment not present",
	"stack segment fault",
	"general protection fault",
	"page fault",
	"reserved 0x0f",
	"math fault",
	"alignment check",
	"machine check",
	"simd floating point numeric error",
	"reserved simd floating point numeric error"
};

const uint8_t exception_class[] = {
	FAULT_CLASS,            /* Divide Error */
	TRAP_CLASS,             /* Debug Breakpoint */
	INTERRUPT_CLASS,        /* NMI */
	TRAP_CLASS,             /* Breakpoint */
	TRAP_CLASS,             /* Overflow */
	FAULT_CLASS,            /* Bound Range Exceeded */
	FAULT_CLASS,            /* Undefined Opcode */
	FAULT_CLASS,            /* No Math Coprocessor */
	ABORT_CLASS,            /* Double Fault */
	RESERVED_CLASS,         /* reserved 0x09 */
	FAULT_CLASS,            /* Invalid Task Segment selector */
	FAULT_CLASS,            /* Segment Not present */
	FAULT_CLASS,            /* Stack Segment Fault */
	FAULT_CLASS,            /* General Protection Fault */
	FAULT_CLASS,            /* Page Fault */
	RESERVED_CLASS,         /* reserved 0x0f */
	FAULT_CLASS,            /* Math Fault */
	FAULT_CLASS,            /* Alignment Check */
	ABORT_CLASS,            /* Machine Check */
	FAULT_CLASS,            /* SIMD Floating Point Numeric Error */
	RESERVED_CLASS,         /* reserved SIMD Floating Point Numeric Error */
};

/*---------------------- Code ----------------------- */

/*-------------------------------------------------------*
*  FUNCTION     : isr_c_handler()
*  PURPOSE      : Generic ISR handler which calls registered
*               : vector specific handlers.
*               : Clear FLAGS.IF
*  ARGUMENTS    : IN isr_parameters_on_stack_t *p_stack - points
*               : to the stack, where FLAGS register stored
*               : as a part of return from interrupt cycle
*  RETURNS      : void
*-------------------------------------------------------*/
void isr_c_handler(IN isr_parameters_on_stack_t *p_stack)
{
	vector_id_t vector_id = (vector_id_t)p_stack->a.vector_id;
	boolean_t interrut_during_emulation;

	interrut_during_emulation = gcpu_process_interrupt(vector_id);
	if (FALSE == interrut_during_emulation) {
		boolean_t handled = FALSE;

		/* if it is a fault exception,skip faulty instruction
		 * in case there is instruction length supplied */
		if (vector_id < NELEMENTS(exception_class)
		    && FAULT_CLASS == exception_class[vector_id]) {
			trial_data_t *p_trial_data = trial_execution_get_last();

			if (NULL != p_trial_data) {
				p_stack->u.errcode_exception.ip =
					(address_t)hw_exception_post_handler;
				p_stack->u.errcode_exception.sp =
					(address_t)p_trial_data->saved_env;
				p_trial_data->fault_vector = vector_id;
				p_trial_data->error_code =
					(uint32_t)p_stack->u.errcode_exception.
					errcode;
				handled = TRUE;
			}
		}

		if (FALSE == handled) {
			if (NULL == isr_table[vector_id]) {
				MON_LOG(mask_anonymous,
					level_trace,
					"Interrupt vector(%d) handler is not registered\n",
					vector_id);
			} else {
				(isr_table[vector_id])(p_stack);
			}
		}
	}

	if (vector_id >= EXCEPTION_COUNT_VECTORS || interrut_during_emulation) {
		/* apparently interrupts were enabled
		 * but we don't process more than one interrupt per VMEXIT,
		 * and we don't process more than one interrupt per emulated
		 * instruction
		 * p_stack->flags is actually eflags / rflags on the stack
		 * clear flags.IF to prevent interrupt re-enabling */
		BIT_CLR(p_stack->u.exception.flags, RFLAGS_IF);
	}

	/* Before returning to the assmbler code, need to set pointer to the
	 * EXCEPTION_STACK ip member. */
	if (FALSE == interrut_during_emulation
	    && isr_error_code_required(vector_id)) {
		/* case exception code DO was pushed on the stack */
		p_stack->a.except_ip_ptr =
			(address_t)&p_stack->u.errcode_exception.ip;
	} else {
		/* case no exception code was pushed on the stack
		 * (external interrupts or exception without error code) */
		p_stack->a.except_ip_ptr = (address_t)&p_stack->u.exception.ip;
	}
}

/*-------------------------------------------------------*
*  FUNCTION     : isr_register_handler()
*  PURPOSE      : Registers ISR handler
*  ARGUMENTS    : func_mon_isr_handler_t handler - is called
*               : when vector interrupt/exception occurs
*               : vector_id_t vector_id
*  RETURNS      : void
*-------------------------------------------------------*/
void isr_register_handler(IN func_mon_isr_handler_t handler,
			  IN vector_id_t vector_id)
{
	isr_table[vector_id] = handler;
}

#ifdef DEBUG

static void print_exception_header(address_t cs USED_IN_DEBUG_ONLY,
				   address_t ip USED_IN_DEBUG_ONLY,
				   vector_id_t vector_id USED_IN_DEBUG_ONLY,
				   size_t errcode USED_IN_DEBUG_ONLY)
{
	cpu_id_t cpu_id = hw_cpu_id();

	MON_LOG_NOLOCK
	(
		"*****************************************************************\n");
	MON_LOG_NOLOCK
	(
		"*                                                               *\n");
	MON_LOG_NOLOCK
	(
		"*                  Intel Virtual Machine Monitor                *\n");
	MON_LOG_NOLOCK
	(
		"*                                                               *\n");
	MON_LOG_NOLOCK
	(
		"*****************************************************************\n");
	MON_LOG_NOLOCK
	(
		"\nException(%d) has occured on CPU(%d) at cs=%P ip=%P errcode=%Id",
		vector_id,
		cpu_id,
		cs,
		ip,
		errcode);
}


static void print_errcode_generic(address_t errcode)
{
	MON_LOG_NOLOCK("Error code: 0X%X", errcode);

	if ((errcode & ERROR_CODE_EXT_BIT) != 0) {
		MON_LOG_NOLOCK("External event\n");
	} else {
		MON_LOG_NOLOCK("Internal event\n");
	}

	if ((errcode & ERROR_CODE_IN_IDT) != 0) {
		MON_LOG_NOLOCK("index is in IDT\n");
	} else if ((errcode & ERROR_CODE_TI) != 0) {
		MON_LOG_NOLOCK("index is in LDT\n");
	} else {
		MON_LOG_NOLOCK("index is in GDT\n");
	}
}
#endif                          /* DEBUG */

static void exception_handler_default_no_errcode(isr_parameters_on_stack_t *
						 p_stack)
{
	MON_DEBUG_CODE(print_exception_header(p_stack->u.exception.cs,
			p_stack->u.exception.ip,
			(vector_id_t)p_stack->a.vector_id, 0));

	if (p_stack->a.vector_id < NELEMENTS(exception_message)) {
		MON_LOG_NOLOCK(" Error type: %s\n",
			exception_message[p_stack->a.vector_id]);
	}
}

static void exception_handler_default_with_errcode(isr_parameters_on_stack_t *
						   p_stack)
{
	MON_DEBUG_CODE(print_exception_header(p_stack->u.errcode_exception.cs,
			p_stack->u.errcode_exception.ip,
			(vector_id_t)p_stack->a.vector_id,
			p_stack->u.
			errcode_exception.errcode));

	if (p_stack->a.vector_id < NELEMENTS(exception_message)) {
		MON_LOG_NOLOCK(" Exception type: %s\n",
			exception_message[p_stack->a.vector_id]);
	}
}

static void exception_handler_default(isr_parameters_on_stack_t *p_stack)
{
	if (isr_error_code_required((vector_id_t)p_stack->a.vector_id)) {
		exception_handler_default_with_errcode(p_stack);
	} else {
		exception_handler_default_no_errcode(p_stack);
	}

	g_exception_stack = p_stack;
	MON_DEADLOOP();
}

static void exception_handler_page_fault(isr_parameters_on_stack_t *p_stack)
{
	guest_cpu_handle_t gcpu;
	vmcs_object_t *vmcs;

	MON_DEBUG_CODE(print_exception_header(p_stack->u.errcode_exception.cs,
			p_stack->u.errcode_exception.ip,
			(vector_id_t)p_stack->a.vector_id,
			p_stack->u.
			errcode_exception.errcode));

	if (p_stack->a.vector_id < NELEMENTS(exception_message)) {
		MON_LOG_NOLOCK(" Error type: %s\n",
			exception_message[p_stack->a.vector_id]);
	}

	MON_LOG_NOLOCK("Faulting address of page fault is %P   RSP=%P\n",
		hw_read_cr2(), p_stack->u.errcode_exception.sp);

	gcpu = mon_scheduler_current_gcpu();
	vmcs = mon_gcpu_get_vmcs(gcpu);
	MON_LOG_NOLOCK("Last VMEXIT reason = %d\n",
		(uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_REASON));

	g_exception_stack = p_stack;
	MON_DEADLOOP();
}

static void exception_handler_undefined_opcode(isr_parameters_on_stack_t *
					       p_stack)
{
#ifdef DEBUG
	uint64_t ip = p_stack->u.exception.ip;
	uint8_t *ip_ptr = (uint8_t *)ip;
#endif

	MON_DEBUG_CODE(print_exception_header(p_stack->u.exception.cs,
			p_stack->u.exception.ip,
			(vector_id_t)p_stack->a.vector_id, 0));

	if (p_stack->a.vector_id < NELEMENTS(exception_message)) {
		MON_LOG_NOLOCK(" Exception type: %s\n",
			exception_message[p_stack->a.vector_id]);
	}

	MON_LOG_NOLOCK("IP = %P\n", ip_ptr);

	MON_LOG_NOLOCK("Encoding: %2x %2x %2x %2x\n", *ip_ptr, *(ip_ptr + 1),
		*(ip_ptr + 2), *(ip_ptr + 3));

	g_exception_stack = p_stack;
	MON_DEADLOOP();
}

static void isr_install_default_handlers(void)
{
	unsigned vector_id;

	for (vector_id = 0; vector_id < INTERRUPT_COUNT_VECTORS; ++vector_id)
		isr_register_handler(exception_handler_default,
			(uint8_t)vector_id);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_DIVIDE_ERROR);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_DEBUG_BREAKPOINT);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_NMI);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_BREAKPOINT);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_OVERFLOW);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_BOUND_RANGE_EXCEEDED);
	isr_register_handler(exception_handler_undefined_opcode,
		IA32_EXCEPTION_VECTOR_UNDEFINED_OPCODE);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_NO_MATH_COPROCESSOR);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_DOUBLE_FAULT);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_INVALID_TASK_SEGMENT_SELECTOR);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_SEGMENT_NOT_PRESENT);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_STACK_SEGMENT_FAULT);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT);
	isr_register_handler(exception_handler_page_fault,
		IA32_EXCEPTION_VECTOR_PAGE_FAULT);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_MATH_FAULT);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_ALIGNMENT_CHECK);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_MACHINE_CHECK);
	isr_register_handler(exception_handler_default,
		IA32_EXCEPTION_VECTOR_SIMD_FLOATING_POINT_NUMERIC_ERROR);
}

/*----------------------------------------------------*
 *  FUNCTION     : isr_setup()
 *  PURPOSE      : Builds ISR wrappers, IDT tables and
 *               : default high level ISR handlers for all CPUs.
 *  ARGUMENTS    : void
 *  RETURNS      : void
 *-------------------------------------------------------*/
void isr_setup(void)
{
	hw_idt_setup();
	isr_install_default_handlers();
}

void isr_handling_start(void)
{
	hw_idt_load();
}

boolean_t isr_error_code_required(vector_id_t vector_id)
{
	switch (vector_id) {
	case IA32_EXCEPTION_VECTOR_DOUBLE_FAULT:
	case IA32_EXCEPTION_VECTOR_PAGE_FAULT:
	case IA32_EXCEPTION_VECTOR_INVALID_TASK_SEGMENT_SELECTOR:
	case IA32_EXCEPTION_VECTOR_SEGMENT_NOT_PRESENT:
	case IA32_EXCEPTION_VECTOR_STACK_SEGMENT_FAULT:
	case IA32_EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT:
	case IA32_EXCEPTION_VECTOR_ALIGNMENT_CHECK:
		return TRUE;
	default:
		return FALSE;
	}
}
