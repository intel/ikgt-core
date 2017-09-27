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
#include "vmm_objects.h"
#include "host_cpu.h"
#include "modules/vmcall.h"
#include "stack.h"
#include "dbg.h"

#define VMCALL_STACK_PROFILE 0x6C696E02
uint64_t rsp_min[MAX_CPU_NUM];
volatile uint32_t flags = 0;

/* below function is copied from host_cpu_id(), with attribute "no_instrument_function" */
static uint16_t __attribute__((no_instrument_function)) __host_cpu_id(void)
{
	uint16_t tr;
	__asm__ volatile (
		"str %0"
		: "=r"(tr)
	);
	return (tr - GDT_TSS64_OFFSET) / 16;
}
static void __attribute__((no_instrument_function)) update_min_stack_pointer(uint16_t cpuid)
{
	uint64_t rsp;

	if (cpuid >= host_cpu_num)
		return;

	__asm__ volatile (
		"mov %%rsp, %0\n\t"
		: "=r"(rsp)
	);

	if (rsp < rsp_min[cpuid])
		rsp_min[cpuid] = rsp;

	return;
}

static void trusty_vmcall_dump_stack_profile(UNUSED guest_cpu_handle_t gcpu)
{
	uint32_t i;
	for (i = 0; i < host_cpu_num; i++) {
		print_info("cpu(%d) min stack pointer = 0x%llx\n stack start = 0x%llx\n, max size = 0x%llx\n",
				i, rsp_min[i], stack_get_cpu_sp(i), stack_get_cpu_sp(i) - rsp_min[i]);
	}
	return;
}

void stack_profile_init(void)
{
	uint32_t i;
	for (i = 0; i < MAX_CPU_NUM; i++) {
		rsp_min[i] = (uint64_t)-1;
	}
	flags = 1;
	vmcall_register(0, VMCALL_STACK_PROFILE, trusty_vmcall_dump_stack_profile);

	return;
}

/* all sub function be called in below two functions must add attribute no_instrument_function for this feature */
void __attribute__((no_instrument_function)) __cyg_profile_func_enter(UNUSED void *this_fn, UNUSED void *call_site)
{
	if (flags != 0) {
		update_min_stack_pointer(__host_cpu_id());
	}
	return;
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(UNUSED void *this_fn, UNUSED void *call_site)
{
	if (flags != 0) {
		update_min_stack_pointer(__host_cpu_id());
	}
	return;
}
