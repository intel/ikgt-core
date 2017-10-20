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
#include "vmm_util.h"
#include "host_cpu.h"
#include "gcpu.h"
#include "idt.h"
#include "isr.h"
#include "dbg.h"
#include "vmm_arch.h"
#include "vmcs.h"
#include "scheduler.h"

typedef struct {
	/* assembler code sets this as an input to C handler */
	uint64_t	vector_id;
	uint64_t	errcode;
	uint64_t	rip;
	uint64_t	cs;
	uint64_t	rflags;
	uint64_t	rsp;
	uint64_t	ss;
} isr_parameters_on_stack_t;

typedef void (*isr_handler_t) (isr_parameters_on_stack_t *p_stack);

/*---------------- local variables ------------------ */

static isr_handler_t isr_table[EXCEPTION_COUNT];

/*---------------------- Code ----------------------- */

static void print_data_in_stack(UNUSED isr_parameters_on_stack_t *p_stack)
{
	print_info("\nException/Interrupt occured on CPU(%u):\n",
		host_cpu_id());
	print_info("\tvector_id=%llu, cs=0x%llX, rip=0x%llX, rsp=0x%llX, ",
		p_stack->vector_id, p_stack->cs, p_stack->rip, p_stack->rsp);
	print_info("ss=0x%llX, rflags=0x%llX, errcode=%llu\n",
		p_stack->ss, p_stack->rflags, p_stack->errcode);
}

/*-------------------------------------------------------*
*  FUNCTION     : isr_c_handler()
*  PURPOSE      : Generic ISR handler which calls registered
*               : vector specific handlers.
*  ARGUMENTS    : IN isr_parameters_on_stack_t *p_stack - points
*               : to the stack, where FLAGS register stored
*               : as a part of return from interrupt cycle
*  RETURNS      : void
*-------------------------------------------------------*/
void isr_c_handler(IN isr_parameters_on_stack_t *p_stack)
{
	uint8_t vector_id = (uint8_t)p_stack->vector_id;
	isr_handler_t handler = NULL;

	if (vector_id < EXCEPTION_COUNT)
		handler = isr_table[vector_id];

	if (NULL == handler) {
		print_data_in_stack(p_stack);
		VMM_DEADLOOP();
	} else {
		handler(p_stack);
	}
}

/*-------------------------------------------------------*
*  FUNCTION     : isr_register_handler()
*  PURPOSE      : Registers ISR handler
*  ARGUMENTS    : isr_handler_t handler - is called
*               : when vector interrupt/exception occurs
*               : uint8_t vector_id
*  RETURNS      : void
*-------------------------------------------------------*/
static void isr_register_handler(IN isr_handler_t handler,
				IN uint8_t vector_id)
{
	D(VMM_ASSERT(vector_id < EXCEPTION_COUNT));

	D(VMM_ASSERT(isr_table[vector_id] == NULL));

	isr_table[vector_id] = handler;
}

static void exception_handler_page_fault(isr_parameters_on_stack_t *p_stack)
{
	print_data_in_stack(p_stack);
	print_panic("\t#PF: Faulting address=0x%llx\n", asm_get_cr2());
	asm_set_cr2(0);

	VMM_DEADLOOP();
}

static void exception_handler_undefined_opcode(isr_parameters_on_stack_t *
					       p_stack)
{
	uint64_t ip = p_stack->rip;
	UNUSED uint8_t *ip_ptr = (uint8_t *)ip;

	print_data_in_stack(p_stack);
	print_panic("\t#UD: [IP]=%2x %2x %2x %2x\n", *ip_ptr, *(ip_ptr+1),
					*(ip_ptr+2), *(ip_ptr+3));

	VMM_DEADLOOP();
}

static void nmi_interrupt_handler(UNUSED isr_parameters_on_stack_t *p_stack)
{
	host_cpu_inc_pending_nmi();

	/* NOTE: DO NOT print anything in NMI handler. The NMI might occur
	 *       when vmm_printf() is being executed. It may lead to deadloop
	 *       due to print_lock. */
	//print_trace("hcpu%d, %s(): nmi=%d\n", host_cpu_id(),
	//			__FUNCTION__, host_cpu_get_pending_nmi());
}

/*----------------------------------------------------*
 *  FUNCTION     : isr_setup()
 *  PURPOSE      : Builds ISR wrappers, IDT tables and
 *               : default ISR handlers for all CPUs.
 *  ARGUMENTS    : void
 *  RETURNS      : void
 *-------------------------------------------------------*/
void isr_setup(void)
{
	isr_register_handler(nmi_interrupt_handler,
		EXCEPTION_NMI);
	isr_register_handler(exception_handler_undefined_opcode,
		EXCEPTION_UD);
	isr_register_handler(exception_handler_page_fault,
		EXCEPTION_PF);
}
