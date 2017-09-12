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

/*
 * configure IDT for 64-bit mode.
 */

#include "vmm_base.h"
#include "vmm_arch.h"
#include "heap.h"
#include "vmm_util.h"
#include "idt.h"
#include "dbg.h"

/*
 * EM64T Interrupt Descriptor Table - Gate Descriptor
 */
typedef struct {
	/* offset 0 */
	uint32_t offset_0_15:16;       /* Offset bits 15..0 */
	uint32_t css:16;               /* Command Segment Selector */

	/* offset 4 */
	uint32_t ist:3;                /* interrupt Stack Table */
	uint32_t reserved_0:5;         /* reserved. must be zeroes */
	uint32_t gate_type:4;          /* Gate Type.  See #defines above */
	uint32_t reserved2_0:1;        /* must be zero */
	uint32_t dpl:2;                /* Descriptor Privilege Level must be zero */
	uint32_t present:1;            /* Segment Present Flag */
	uint32_t offset_15_31:16;      /* Offset bits 31..16 */

	/* offset 8 */
	uint32_t	offset_32_63; /* Offset bits 32..63 */

	/* offset 12 */
	uint32_t	reserved3;
} idt_gate_desc_t;

#define IDT_VECTOR_COUNT 256

static idtr64_t idtr;

/* pointer to IDTs for all CPUs */
static idt_gate_desc_t idt[IDT_VECTOR_COUNT];

extern char isr_entries_start[];

#ifdef DEBUG
extern uint64_t isr_entries_end;
#endif

/*-------------------------------------------------------*
*  FUNCTION     : idt_register_handler()
*  PURPOSE      : Register interrupt handler at spec. vector
*  ARGUMENTS    : uint8_t vector_id
*               : uint64_t isr_handler_address - address of function
*  RETURNS      : void
*-------------------------------------------------------*/
static void idt_register_handler(uint8_t vector_id,
			     uint64_t isr_handler_address)
{
	/* fill IDT entries with it */
	idt[vector_id].offset_0_15 =
		(uint32_t)(isr_handler_address & 0xFFFFULL);
	idt[vector_id].offset_15_31 =
		(uint32_t)((isr_handler_address >> 16) & 0xFFFFULL);
	idt[vector_id].offset_32_63 =
		(uint32_t)(isr_handler_address >> 32);
	/* use IST for #DF only since in #DF the stack might be incorrect */
	idt[vector_id].ist =
		vector_id == EXCEPTION_DF ? 1 : 0;
	/* Config all IDT descriptors to interrupt gate type */
	idt[vector_id].gate_type = 0xE;

	/* Note: No need to set 0 in idt, it is a global variable
	 *       which is initialized with 0 by GCC */
	//idt[vector_id].dpl = 0;

	idt[vector_id].present = 1;
	idt[vector_id].css = GDT_CODE64_OFFSET;
}

/*-------------------------------------------------------*
*  FUNCTION     : idt_setup()
*  PURPOSE      : Build and populate IDT table, used by all CPUs
*  ARGUMENTS    : void
*  RETURNS      : void
*-------------------------------------------------------*/
void idt_setup(void)
{
	unsigned vector_id;

	D(VMM_ASSERT(IDT_VECTOR_COUNT * 16 ==
		(uint64_t)&isr_entries_end - (uint64_t)isr_entries_start));

	for (vector_id = 0; vector_id < IDT_VECTOR_COUNT; ++vector_id)
		idt_register_handler((uint8_t)vector_id,
			(uint64_t)(isr_entries_start + 16 * vector_id));

	idtr.base = (uint64_t)idt;
	idtr.limit = sizeof(idt) - 1;
}

/*-------------------------------------------------------*
*  FUNCTION     : idt_load()
*  PURPOSE      : Load IDT descriptor into IDTR of CPU, currently excuted
*  ARGUMENTS    : void
*  RETURNS      : void
*-------------------------------------------------------*/
void idt_load(void)
{
	VMM_ASSERT_EX((idtr.base && idtr.limit), "idtr is invalid\n");
	asm_lidt((void *)&idtr);
}
