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

#include <vmm_base.h>
#include <evmm_desc.h>
#include <stack.h>
#include <idt.h>
#include "dbg.h"
#include "host_cpu.h"

/* Default size for VMM stack, in pages */

#define VMM_DEFAULT_STACK_SIZE_PAGES   10
#define VMM_STACK_SIZE_PER_CPU ((VMM_DEFAULT_STACK_SIZE_PAGES + \
			VMM_IDT_EXTRA_STACKS) * PAGE_4K_SIZE)

static uint64_t stack_base;

/****************************************
*          the layout of stack:
*                  hpa
*          -------------------
*          | exception stack |
*          -------------------
*          |    cpu stack    |
*          -------------------
*                 ....
*          -------------------
*          | exception stack |
*          -------------------
*          |    cpu stack    |
*          -------------------
*          |  page of zero   |
*          -------------------
*
*****************************************/

static inline uint64_t get_stack_base_for_cpu(uint16_t cpu_id)
{
	D(VMM_ASSERT(cpu_id < host_cpu_num));
	D(VMM_ASSERT(stack_base != 0));

	return stack_base + VMM_STACK_SIZE_PER_CPU * cpu_id;
}

void stack_initialize(evmm_desc_t *evmm_desc)
{
	D(VMM_ASSERT_EX(evmm_desc, "evmm_desc is NULL\n"));

	stack_base = ALIGN_F((evmm_desc->evmm_file.runtime_addr +
				evmm_desc->evmm_file.runtime_image_size),
				PAGE_4K_SIZE);
}

void stack_get_details(uint64_t *total_stack_base, uint32_t *total_stack_size)
{

	D(VMM_ASSERT_EX((total_stack_base && total_stack_size),
		"total_stack_base or total_stack_size is NULL\n"));

	*total_stack_base = stack_base;
	/* add zero page as last page in stack */
	*total_stack_size = host_cpu_num *
			VMM_STACK_SIZE_PER_CPU + PAGE_4K_SIZE;
}

uint64_t stack_get_cpu_sp(uint16_t cpu_id)
{
	uint64_t stack_pointer;

	stack_pointer = get_stack_base_for_cpu(cpu_id);

	return stack_pointer + VMM_STACK_SIZE_PER_CPU;
}

uint64_t stack_get_exception_sp(uint16_t cpu_id)
{
	uint64_t stack_pointer;

	stack_pointer = get_stack_base_for_cpu(cpu_id);

	return stack_pointer + VMM_IDT_EXTRA_STACKS * PAGE_4K_SIZE;
}

#ifdef DEBUG
void stack_print(void)
{
	uint16_t cpu_id;

	vmm_printf("\nVMM STACKS:\n");
	vmm_printf("=================\n");
	vmm_printf("\tstack base: 0x%llX\n", stack_base);
	vmm_printf("\tstack size per cpu: 0x%llx\n", VMM_STACK_SIZE_PER_CPU);
	vmm_printf("\t-----------------\n");
	for (cpu_id = 0;
		cpu_id < host_cpu_num;
		cpu_id++) {
		uint64_t rsp;
		uint64_t page;

		vmm_printf("\tCPU%d:\n", cpu_id);

		page = stack_get_exception_sp(cpu_id);

		vmm_printf("\t[0x%llX - 0x%llX] : exception stack \n",
				page - PAGE_4K_SIZE, page);

		rsp = stack_get_cpu_sp(cpu_id);

		vmm_printf("\t[0x%llX - 0x%llX] : regular stack - initial RSP = 0x%llX\n",
			page, rsp, rsp);

		vmm_printf("\t-----------------\n");
	}
	vmm_printf("\n");
}
#endif
