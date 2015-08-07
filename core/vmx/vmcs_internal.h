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

#ifndef VMCS_INTERNAL_H
#define VMCS_INTERNAL_H

#include <mon_defs.h>

void vmcs_destroy_all_msr_lists_internal(vmcs_object_t *vmcs,
					 boolean_t addresses_are_in_hpa);

void vmcs_add_msr_to_list(vmcs_object_t *vmcs,
			  uint32_t msr_index,
			  uint64_t value,
			  vmcs_field_t list_address,
			  vmcs_field_t list_count,
			  uint32_t *max_msrs_counter,
			  boolean_t is_addres_hpa);

void vmcs_delete_msr_from_list(vmcs_object_t *vmcs,
			       uint32_t msr_index,
			       vmcs_field_t list_address,
			       vmcs_field_t list_count,
			       boolean_t is_addres_hpa);

INLINE
void vmcs_add_msr_to_vmexit_store_list_internal(vmcs_object_t *vmcs,
						uint32_t msr_index,
						uint64_t value,
						boolean_t
						is_msr_list_addr_hpa)
{
	vmcs_add_msr_to_list(vmcs,
		msr_index,
		value,
		VMCS_EXIT_MSR_STORE_ADDRESS,
		VMCS_EXIT_MSR_STORE_COUNT,
		&vmcs->max_num_of_vmexit_store_msrs,
		is_msr_list_addr_hpa);
}

INLINE
void vmcs_add_msr_to_vmexit_load_list_internal(vmcs_object_t *vmcs,
					       uint32_t msr_index,
					       uint64_t value,
					       boolean_t is_msr_list_addr_hpa)
{
	vmcs_add_msr_to_list(vmcs, msr_index, value, VMCS_EXIT_MSR_LOAD_ADDRESS,
		VMCS_EXIT_MSR_LOAD_COUNT,
		&vmcs->max_num_of_vmexit_load_msrs,
		is_msr_list_addr_hpa);
}

INLINE
void vmcs_add_msr_to_vmenter_load_list_internal(vmcs_object_t *vmcs,
						uint32_t msr_index,
						uint64_t value,
						boolean_t
						is_msr_list_addr_hpa)
{
	vmcs_add_msr_to_list(vmcs,
		msr_index,
		value,
		VMCS_ENTER_MSR_LOAD_ADDRESS,
		VMCS_ENTER_MSR_LOAD_COUNT,
		&vmcs->max_num_of_vmenter_load_msrs,
		is_msr_list_addr_hpa);
}

void vmcs_add_msr_to_vmexit_store_and_vmenter_load_lists_internal(
	vmcs_object_t *vmcs,
	uint32_t msr_index,
	uint64_t value,
	boolean_t is_msr_list_addr_hpa);

INLINE void vmcs_delete_msr_from_vmexit_store_list_internal(
	vmcs_object_t *vmcs,
	uint32_t msr_index,
	boolean_t is_msr_list_addr_hpa)
{
	vmcs_delete_msr_from_list(vmcs, msr_index, VMCS_EXIT_MSR_STORE_ADDRESS,
		VMCS_EXIT_MSR_STORE_COUNT, is_msr_list_addr_hpa);
}

INLINE void vmcs_delete_msr_from_vmexit_load_list_internal(
	vmcs_object_t *vmcs,
	uint32_t msr_index,
	boolean_t is_msr_list_addr_hpa)
{
	vmcs_delete_msr_from_list(vmcs, msr_index, VMCS_EXIT_MSR_LOAD_ADDRESS,
		VMCS_EXIT_MSR_LOAD_COUNT, is_msr_list_addr_hpa);
}

INLINE void vmcs_delete_msr_from_vmenter_load_list_internal(
	vmcs_object_t *vmcs,
	uint32_t msr_index,
	boolean_t is_msr_list_addr_hpa)
{
	vmcs_delete_msr_from_list(vmcs, msr_index, VMCS_ENTER_MSR_LOAD_ADDRESS,
		VMCS_ENTER_MSR_LOAD_COUNT, is_msr_list_addr_hpa);
}

void vmcs_delete_msr_from_vmexit_store_and_vmenter_load_lists_internal(
	vmcs_object_t *vmcs,
	uint32_t msr_index,
	boolean_t is_msr_list_addr_hpa);

typedef void (*func_vmcs_add_msr_t) (vmcs_object_t *vmcs, uint32_t msr_index,
				     uint64_t value);
typedef void (*func_vmcs_clear_msr_list_t) (vmcs_object_t *vmcs);
typedef boolean_t (*func_vmcs_is_msr_in_list_t) (vmcs_object_t *vmcs,
						 uint32_t msr_index);

#endif
