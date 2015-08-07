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

#include <mon_defs.h>
#include <mon_startup.h>
#include <mon_stack_api.h>
#include <mon_stack.h>
#include <idt.h>
#include "mon_dbg.h"
#include <libc.h>
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(MON_STACK_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(MON_STACK_C, __condition)

static mon_stacks_info_t g_stacks_infos_s;
static mon_stacks_info_t *const g_stacks_infos = &g_stacks_infos_s;

/*-----------------------------------------------------------*/

INLINE uint32_t mon_stack_get_stack_size_per_cpu(uint32_t num_of_requested_pages)
{
	/* adding one more page for exceptions stack */
	return (num_of_requested_pages +
		idt_get_extra_stacks_required()) * PAGE_4KB_SIZE;
}

INLINE
uint64_t mon_stack_caclulate_stack_pointer_for_cpu(
	uint64_t mon_stack_base_address,
	uint32_t mon_stack_size_per_cpu,
	cpu_id_t cpu_id)
{
	uint64_t end_of_block =
		mon_stack_base_address +
		(mon_stack_size_per_cpu * (cpu_id + 1));

	/* leave one page to protect from underflow */
	return end_of_block - PAGE_4KB_SIZE;
}

INLINE
uint64_t mon_stack_get_stacks_base(uint64_t mon_base_address, uint32_t mon_size)
{
	return ((mon_base_address + mon_size) + PAGE_4KB_SIZE -
		1) & (~((uint64_t)PAGE_4KB_MASK));
}

INLINE
uint64_t mon_stacks_retrieve_stacks_base_addr_from_startup_struct(
	const mon_startup_struct_t *startup_struct)
{
	uint64_t mon_base_address =
		startup_struct->mon_memory_layout[mon_image].base_address;
	uint32_t mon_size =
		startup_struct->mon_memory_layout[mon_image].image_size;

	return mon_stack_get_stacks_base(mon_base_address, mon_size);
}

INLINE
uint32_t mon_stack_retrieve_max_allowed_cpus_from_startup_struct(
	const mon_startup_struct_t *startup_struct)
{
	return startup_struct->number_of_processors_at_boot_time;
}

INLINE
uint32_t mon_stacks_retrieve_stack_size_per_cpu_from_startup_struct(
	const mon_startup_struct_t *startup_struct)
{
	return mon_stack_get_stack_size_per_cpu(
		startup_struct->size_of_mon_stack);
}

/*****************************************************************************
 * Function name: mon_stack_calculate_stack_pointer
 * Parameters: Input validation for startup_struct is performed in caller
 *             functions. Function assumes valid input.
 ******************************************************************************/
boolean_t mon_stack_caclulate_stack_pointer(IN const mon_startup_struct_t *
					    startup_struct, IN cpu_id_t cpu_id,
					    OUT hva_t *stack_pointer)
{
	uint64_t mon_stack_base_address =
		mon_stacks_retrieve_stacks_base_addr_from_startup_struct
			(startup_struct);
	uint32_t mon_stack_size_per_cpu =
		mon_stacks_retrieve_stack_size_per_cpu_from_startup_struct
			(startup_struct);
	uint32_t mon_max_allowed_cpus =
		mon_stack_retrieve_max_allowed_cpus_from_startup_struct(
			startup_struct);
	uint64_t stack_pointer_tmp;

	if (cpu_id >= mon_max_allowed_cpus) {
		return FALSE;
	}

	stack_pointer_tmp =
		mon_stack_caclulate_stack_pointer_for_cpu(
			mon_stack_base_address,
			mon_stack_size_per_cpu,
			cpu_id);
	*stack_pointer = *((hva_t *)(&stack_pointer_tmp));
	return TRUE;
}

boolean_t mon_stack_initialize(IN const mon_startup_struct_t *startup_struct)
{
	uint64_t mon_stack_base_address;
	uint32_t mon_stack_size_per_cpu;
	uint32_t mon_max_allowed_cpus;

	if (startup_struct == NULL) {
		return FALSE;
	}

	mon_memset(&g_stacks_infos_s, 0, sizeof(g_stacks_infos_s));

	mon_stack_base_address =
		mon_stacks_retrieve_stacks_base_addr_from_startup_struct
			(startup_struct);
	mon_stack_size_per_cpu =
		mon_stacks_retrieve_stack_size_per_cpu_from_startup_struct
			(startup_struct);
	mon_max_allowed_cpus =
		mon_stack_retrieve_max_allowed_cpus_from_startup_struct(
			startup_struct);

	mon_stacks_info_set_stacks_base(g_stacks_infos, mon_stack_base_address);
	mon_stacks_info_set_size_of_single_stack(g_stacks_infos,
		mon_stack_size_per_cpu);
	mon_stacks_info_set_max_allowed_cpus(g_stacks_infos,
		mon_max_allowed_cpus);
	mon_stacks_info_set_num_of_exception_stacks(g_stacks_infos,
		idt_get_extra_stacks_required
			());
	mon_stacks_set_initialized(g_stacks_infos);
	return TRUE;
}

boolean_t mon_stack_is_initialized(void)
{
	return mon_stacks_is_initialized(g_stacks_infos);
}

boolean_t mon_stack_get_stack_pointer_for_cpu(IN cpu_id_t cpu_id,
					      OUT hva_t *stack_pointer)
{
	uint64_t mon_stack_base_address;
	uint32_t mon_stack_size_per_cpu;
	uint64_t stack_pointer_tmp;

	MON_ASSERT(mon_stack_is_initialized());

	if (cpu_id >= mon_stacks_info_get_max_allowed_cpus(g_stacks_infos)) {
		return FALSE;
	}

	mon_stack_base_address =
		mon_stacks_info_get_stacks_base(g_stacks_infos);
	mon_stack_size_per_cpu =
		mon_stacks_info_get_size_of_single_stack(g_stacks_infos);

	stack_pointer_tmp =
		mon_stack_caclulate_stack_pointer_for_cpu(
			mon_stack_base_address,
			mon_stack_size_per_cpu,
			cpu_id);
	*stack_pointer = *((hva_t *)(&stack_pointer_tmp));
	return TRUE;
}

void mon_stacks_get_details(OUT hva_t *lowest_addr_used, OUT uint32_t *size)
{
	uint64_t base;
	uint32_t single_size;
	uint32_t num_of_cpus;

	MON_ASSERT(mon_stack_is_initialized());

	base = mon_stacks_info_get_stacks_base(g_stacks_infos);
	single_size = mon_stacks_info_get_size_of_single_stack(g_stacks_infos);
	num_of_cpus = mon_stacks_info_get_max_allowed_cpus(g_stacks_infos);

	*lowest_addr_used = *((hva_t *)(&base));
	*size = num_of_cpus * single_size;
}

/****************************************************************************
 * Function Name: mon_stacks_get_exception_stack_for_cpu
 * Parameters: Validation for cpu_id is performed by caller function. Function
 *             assumes valid input.
 *****************************************************************************/
boolean_t mon_stacks_get_exception_stack_for_cpu(IN cpu_id_t cpu_id,
						 IN uint32_t stack_num,
						 OUT hva_t *page_addr)
{
	uint64_t base;
	uint32_t single_size;

	MON_ASSERT(mon_stack_is_initialized());

	if (stack_num >=
	    mon_stacks_info_get_num_of_exception_stacks(g_stacks_infos)) {
		return FALSE;
	}

	base = mon_stacks_info_get_stacks_base(g_stacks_infos);
	single_size = mon_stacks_info_get_size_of_single_stack(g_stacks_infos);
	if (stack_num ==
	    (mon_stacks_info_get_num_of_exception_stacks(g_stacks_infos) - 1)) {
		/* The last page of the range */
		*page_addr =
			mon_stack_caclulate_stack_pointer_for_cpu(base,
				single_size,
				cpu_id);
	} else {
		uint64_t base_for_cpu = base + (single_size * cpu_id);
		*page_addr = base_for_cpu + (PAGE_4KB_SIZE * stack_num);
	}
	return TRUE;
}

#ifdef DEBUG
void mon_stacks_print(void)
{
	cpu_id_t cpu_id;

	MON_LOG(mask_anonymous, level_trace, "\nMON STACKS:\n");
	MON_LOG(mask_anonymous, level_trace, "=================\n");
	for (cpu_id = 0;
	     cpu_id < mon_stacks_info_get_max_allowed_cpus(g_stacks_infos);
	     cpu_id++) {
		uint32_t regular_stack_size;
		hva_t rsp;
		hva_t page;
		boolean_t res;
		uint32_t stack_id;

		MON_LOG(mask_anonymous, level_trace, "\tCPU%d:\n", cpu_id);
		for (stack_id = 0;
		     stack_id <
		     mon_stacks_info_get_num_of_exception_stacks(g_stacks_infos)
		     - 1;
		     stack_id++) {
			res =
				mon_stacks_get_exception_stack_for_cpu(cpu_id,
					stack_id,
					&page);
			MON_ASSERT(res);

			MON_LOG(mask_anonymous, level_trace,
				"\t[%P - %P] : exception stack #%d \n", page,
				page + PAGE_4KB_SIZE, stack_id);
		}

		res = mon_stack_get_stack_pointer_for_cpu(cpu_id, &rsp);
		MON_ASSERT(res);

		regular_stack_size =
			mon_stacks_info_get_size_of_single_stack(g_stacks_infos)
			-
			(mon_stacks_info_get_num_of_exception_stacks(
				 g_stacks_infos) *
			 PAGE_4KB_SIZE);
		MON_LOG(mask_anonymous, level_trace,
			"\t[%P - %P] : regular stack - initial RSP = %P\n",
			rsp - regular_stack_size, rsp, rsp);

		stack_id =
			mon_stacks_info_get_num_of_exception_stacks(
				g_stacks_infos) - 1;
		res = mon_stacks_get_exception_stack_for_cpu(cpu_id,
			stack_id,
			&page);
		MON_ASSERT(res);

		MON_LOG(mask_anonymous, level_trace,
			"\t[%P - %P] : exception stack #%d \n", page,
			page + PAGE_4KB_SIZE, stack_id);
		MON_LOG(mask_anonymous, level_trace, "\t-----------------\n");
	}
	MON_LOG(mask_anonymous, level_trace, "\n");
}
#endif
