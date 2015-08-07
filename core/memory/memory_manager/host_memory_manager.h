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

#ifndef HOST_MEMORY_MANAGER_H
#define HOST_MEMORY_MANAGER_H

#include <mon_defs.h>
#include <memory_address_mapper_api.h>
#include <lock.h>

#define HMM_MAX_LOW_ADDRESS 0x7FFFFFFFFFFF
#define HMM_FIRST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS 0x8000000000
#define HMM_LAST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS  0x800000000000

#define HMM_WP_BIT_MASK ((uint64_t)0x10000)

typedef struct {
	mam_handle_t		hva_to_hpa_mapping;
	mam_handle_t		hpa_to_hva_mapping;
	uint64_t		current_mon_page_tabless_hpa;
	mon_lock_t		update_lock;
	hva_t			new_allocations_curr_ptr;
	mon_phys_mem_type_t	mem_types_table[MON_PHYS_MEM_LAST_TYPE + 1]
	[MON_PHYS_MEM_LAST_TYPE + 1];
	uint64_t		final_mapped_virt_address;
	uint32_t		wb_pat_index;
	uint32_t		uc_pat_index;
} hmm_t;   /* Host Memory Manager */

INLINE mam_handle_t hmm_get_hva_to_hpa_mapping(hmm_t *hmm)
{
	return hmm->hva_to_hpa_mapping;
}

INLINE void hmm_set_hva_to_hpa_mapping(hmm_t *hmm, mam_handle_t mapping)
{
	hmm->hva_to_hpa_mapping = mapping;
}

INLINE mam_handle_t hmm_get_hpa_to_hva_mapping(hmm_t *hmm)
{
	return hmm->hpa_to_hva_mapping;
}

INLINE void hmm_set_hpa_to_hva_mapping(hmm_t *hmm, mam_handle_t mapping)
{
	hmm->hpa_to_hva_mapping = mapping;
}

INLINE uint64_t hmm_get_current_mon_page_tables(hmm_t *hmm)
{
	return hmm->current_mon_page_tabless_hpa;
}

INLINE void hmm_set_current_mon_page_tables(hmm_t *hmm, uint64_t value)
{
	hmm->current_mon_page_tabless_hpa = value;
}

INLINE mon_lock_t *hmm_get_update_lock(hmm_t *hmm)
{
	return &(hmm->update_lock);
}

INLINE hva_t hmm_get_new_allocations_curr_ptr(hmm_t *hmm)
{
	return hmm->new_allocations_curr_ptr;
}

INLINE void hmm_set_new_allocations_curr_ptr(hmm_t *hmm, hva_t new_value)
{
	hmm->new_allocations_curr_ptr = new_value;
}

#ifdef DEBUG
INLINE uint64_t hmm_get_final_mapped_virt_address(hmm_t *hmm)
{
	return hmm->final_mapped_virt_address;
}
#endif

INLINE void hmm_set_final_mapped_virt_address(hmm_t *hmm, uint64_t addr)
{
	hmm->final_mapped_virt_address = addr;
}

INLINE uint32_t hmm_get_wb_pat_index(hmm_t *hmm)
{
	return hmm->wb_pat_index;
}

INLINE void hmm_set_wb_pat_index(hmm_t *hmm, uint32_t wb_pat_index)
{
	hmm->wb_pat_index = wb_pat_index;
}

INLINE uint32_t hmm_get_uc_pat_index(hmm_t *hmm)
{
	return hmm->uc_pat_index;
}

INLINE void hmm_set_uc_pat_index(hmm_t *hmm, uint32_t uc_pat_index)
{
	hmm->uc_pat_index = uc_pat_index;
}

#endif
