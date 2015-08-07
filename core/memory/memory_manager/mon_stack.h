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

#ifndef MON_STACK_H
#define MON_STACK_H

#ifdef DEBUG
#define MON_STACK_DEBUG_CODE
#endif

typedef struct {
	uint64_t	stacks_base;
	uint32_t	size_of_single_stack;
	uint32_t	max_allowed_cpus;
	uint32_t	num_of_exception_stacks;
	boolean_t	is_initialized;
} mon_stacks_info_t;

INLINE
uint64_t mon_stacks_info_get_stacks_base(const mon_stacks_info_t *stacks_info)
{
	return stacks_info->stacks_base;
}

INLINE
void mon_stacks_info_set_stacks_base(mon_stacks_info_t *stacks_info,
				     uint64_t base)
{
	stacks_info->stacks_base = base;
}

INLINE
uint32_t mon_stacks_info_get_size_of_single_stack(const mon_stacks_info_t *
						  stacks_info)
{
	return stacks_info->size_of_single_stack;
}

INLINE
void mon_stacks_info_set_size_of_single_stack(mon_stacks_info_t *stacks_info,
					      uint32_t size)
{
	stacks_info->size_of_single_stack = size;
}

INLINE
uint32_t mon_stacks_info_get_max_allowed_cpus(const mon_stacks_info_t *
					      stacks_info)
{
	return stacks_info->max_allowed_cpus;
}

INLINE
void mon_stacks_info_set_max_allowed_cpus(mon_stacks_info_t *stacks_info,
					  uint32_t cpus_num)
{
	stacks_info->max_allowed_cpus = cpus_num;
}

INLINE
uint32_t mon_stacks_info_get_num_of_exception_stacks(const mon_stacks_info_t *
						     stacks_info)
{
	return stacks_info->num_of_exception_stacks;
}

INLINE
void mon_stacks_info_set_num_of_exception_stacks(mon_stacks_info_t *
						 stacks_info,
						 uint32_t num_of_stacks)
{
	stacks_info->num_of_exception_stacks = num_of_stacks;
}

INLINE boolean_t mon_stacks_is_initialized(const mon_stacks_info_t *stacks_info)
{
	return stacks_info->is_initialized;
}

INLINE void mon_stacks_set_initialized(mon_stacks_info_t *stacks_info)
{
	stacks_info->is_initialized = TRUE;
}

#endif
