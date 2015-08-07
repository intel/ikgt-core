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

#include "file_codes.h"
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(HOST_MEMORY_MANAGER_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(HOST_MEMORY_MANAGER_C, \
	__condition)
#include <host_memory_manager_api.h>
#include <memory_address_mapper_api.h>
#include <mon_arch_defs.h>
#include <e820_abstraction.h>
#include <mon_stack_api.h>
#include <common_libc.h>
#include <heap.h>
#include <efer_msr_abstraction.h>
#include <mtrrs_abstraction.h>
#include <hw_utils.h>
#include <idt.h>
#include <gdt.h>
#include <parse_image.h>
#include <pat_manager.h>
#include <mon_dbg.h>
#include <lock.h>
#include <ipc.h>
#include "host_memory_manager.h"


enum {
	HMM_INVALID_MEMORY_TYPE = (MAM_MAPPING_SUCCESSFUL + 1),
	HMM_THUNK_IMAGE_UNMAP_REASON,
	HMM_VIRT_MEMORY_NOT_FOR_USE,
};

static hmm_t g_hmm_s;
static hmm_t *const g_hmm = &g_hmm_s;

extern uint64_t g_additional_heap_pa;
extern uint32_t g_heap_pa_num;
extern uint64_t g_additional_heap_base;
extern uint32_t g_is_post_launch;

/*-----------------------------------------------------------*/

INLINE boolean_t hmm_is_page_available_for_allocation(
	mam_mapping_result_t result)
{
	return (result == MAM_UNKNOWN_MAPPING)
	       || (result == HMM_INVALID_MEMORY_TYPE);
}

static
boolean_t hmm_allocate_continuous_free_virtual_pages(uint32_t num_of_pages,
						     uint64_t *hva)
{
	uint64_t new_allocations_curr_ptr = hmm_get_new_allocations_curr_ptr(
		g_hmm);
	uint64_t new_allocations_curr_ptr_tmp = new_allocations_curr_ptr;
	uint32_t counter = 0;
	uint64_t loop_counter = 0;
	const uint64_t max_loop_counter =
		HMM_LAST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS -
		HMM_FIRST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS;
	hpa_t hpa;
	mam_attributes_t attrs;
	mam_handle_t hva_to_hpa = hmm_get_hva_to_hpa_mapping(g_hmm);
	mam_mapping_result_t result;

	MON_ASSERT(num_of_pages > 0);

	do {
		if (loop_counter >= max_loop_counter) {
			return FALSE; /* looked at all available ranges */
		}

		result =
			mam_get_mapping(hva_to_hpa,
				new_allocations_curr_ptr_tmp,
				&hpa,
				&attrs);
		if (!hmm_is_page_available_for_allocation(result)) {
			/* HVA is occupied */

			new_allocations_curr_ptr =
				new_allocations_curr_ptr_tmp + PAGE_4KB_SIZE;
			if (new_allocations_curr_ptr >=
			    HMM_LAST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS) {
				new_allocations_curr_ptr =
					HMM_FIRST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS;
			}
			new_allocations_curr_ptr_tmp = new_allocations_curr_ptr;
			counter = 0;
			loop_counter++;
			continue;
		}

		counter++;
		new_allocations_curr_ptr_tmp += PAGE_4KB_SIZE;

		if ((new_allocations_curr_ptr_tmp >=
		     HMM_LAST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS)
		    && (counter < num_of_pages)) {
			new_allocations_curr_ptr =
				HMM_FIRST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS;
			new_allocations_curr_ptr_tmp = new_allocations_curr_ptr;
			counter = 0;
			loop_counter++;
			continue;
		}

		loop_counter++;
	} while (counter != num_of_pages);

	*hva = new_allocations_curr_ptr;

	new_allocations_curr_ptr = new_allocations_curr_ptr_tmp;
	if (new_allocations_curr_ptr >=
	    HMM_LAST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS) {
		new_allocations_curr_ptr =
			HMM_FIRST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS;
	}

	hmm_set_new_allocations_curr_ptr(g_hmm, new_allocations_curr_ptr);

	return TRUE;
}

INLINE boolean_t hmm_allocate_free_virtual_page(uint64_t *hva)
{
	return hmm_allocate_continuous_free_virtual_pages(1, hva);
}

static
boolean_t hmm_get_next_non_existent_range(IN mam_handle_t mam_handle,
					  IN OUT mam_memory_ranges_iterator_t *
					  iter,
					  IN OUT uint64_t *last_covered_address,
					  OUT uint64_t *range_start,
					  OUT uint64_t *range_size)
{
	mam_mapping_result_t res = MAM_MAPPING_SUCCESSFUL;
	uint64_t tgt_addr;
	mam_attributes_t attrs;

	if (*iter == MAM_INVALID_MEMORY_RANGES_ITERATOR) {
		return FALSE;
	}

	do {
		*iter = mam_get_range_details_from_iterator(mam_handle,
			*iter,
			range_start, range_size);

		res = mam_get_mapping(mam_handle,
			*range_start,
			&tgt_addr,
			&attrs);

		if (res != MAM_UNKNOWN_MAPPING) {
			*last_covered_address = *range_start + *range_size;
		} else {
			*last_covered_address = *range_start;
		}
	} while ((*iter != MAM_INVALID_MEMORY_RANGES_ITERATOR) &&
		 (res != MAM_UNKNOWN_MAPPING));

	if (res == MAM_UNKNOWN_MAPPING) {
		return TRUE;
	}

	return FALSE;
}

static
boolean_t hmm_map_remaining_memory(IN mam_attributes_t mapping_attrs)
{
	mam_handle_t hva_to_hpa;
	mam_handle_t hpa_to_hva;
	mam_memory_ranges_iterator_t virt_ranges_iter;
	mam_memory_ranges_iterator_t phys_ranges_iter;
	uint64_t last_covered_virt_addr = 0;
	uint64_t last_covered_phys_addr = 0;
	uint64_t virt_range_start = 0;
	uint64_t virt_range_size = 0;
	uint64_t phys_range_start = 0;
	uint64_t phys_range_size = 0;
	const uint64_t size_4G = 0x100000000;
	e820_abstraction_range_iterator_t e820_iter;

	hva_to_hpa = hmm_get_hva_to_hpa_mapping(g_hmm);
	virt_ranges_iter = mam_get_memory_ranges_iterator(hva_to_hpa);

	hpa_to_hva = hmm_get_hpa_to_hva_mapping(g_hmm);
	phys_ranges_iter = mam_get_memory_ranges_iterator(hpa_to_hva);

	if (!hmm_get_next_non_existent_range
		    (hva_to_hpa, &virt_ranges_iter, &last_covered_virt_addr,
		    &virt_range_start, &virt_range_size)) {
		virt_range_start = last_covered_virt_addr;
		if (virt_range_start < size_4G) {
			virt_range_size = size_4G - virt_range_start;
		} else {
			virt_range_size = 0;
		}
	}

	if (!hmm_get_next_non_existent_range
		    (hpa_to_hva, &phys_ranges_iter, &last_covered_phys_addr,
		    &phys_range_start, &phys_range_size)) {
		phys_range_start = last_covered_phys_addr;
		if (phys_range_start < size_4G) {
			phys_range_size = size_4G - phys_range_start;
		} else {
			phys_range_size = 0;
		}
	}

	do {
		uint64_t actual_size;
		if (virt_range_size <= phys_range_size) {
			actual_size = virt_range_size;
		} else {
			actual_size = phys_range_size;
		}

		if (actual_size > 0) {
			if (!mam_insert_range
				    (hva_to_hpa, virt_range_start,
				    phys_range_start, actual_size,
				    mapping_attrs)) {
				return FALSE;
			}

			if (!mam_insert_range
				    (hpa_to_hva, phys_range_start,
				    virt_range_start, actual_size,
				    MAM_NO_ATTRIBUTES)) {
				return FALSE;
			}

			virt_range_start += actual_size;
			phys_range_start += actual_size;

			virt_range_size -= actual_size;
			phys_range_size -= actual_size;

			last_covered_virt_addr = virt_range_start;
			last_covered_phys_addr = phys_range_start;

			if (virt_range_size == 0) {
				if (!hmm_get_next_non_existent_range
					    (hva_to_hpa, &virt_ranges_iter,
					    &last_covered_virt_addr,
					    &virt_range_start,
					    &virt_range_size)) {
					virt_range_start =
						last_covered_virt_addr;
					if (virt_range_start < size_4G) {
						virt_range_size = size_4G -
								  virt_range_start;
					} else {
						virt_range_size = 0;
					}
				}
			}

			if (phys_range_size == 0) {
				if (!hmm_get_next_non_existent_range
					    (hpa_to_hva, &phys_ranges_iter,
					    &last_covered_phys_addr,
					    &phys_range_start,
					    &phys_range_size)) {
					phys_range_start =
						last_covered_phys_addr;
					if (phys_range_start < size_4G) {
						phys_range_size = size_4G -
								  phys_range_start;
					} else {
						phys_range_size = 0;
					}
				}
			}
		}
	} while (last_covered_phys_addr < size_4G);

	MON_ASSERT(last_covered_virt_addr <= last_covered_phys_addr);

	hmm_set_final_mapped_virt_address(g_hmm, last_covered_virt_addr);

	e820_iter = e820_abstraction_iterator_get_first(E820_ORIGINAL_MAP);
	while (e820_iter != E820_ABSTRACTION_NULL_ITERATOR) {
		const int15_e820_memory_map_entry_ext_t *e820_entry =
			e820_abstraction_iterator_get_range_details(e820_iter);

		if ((e820_entry->basic_entry.base_address +
		     e820_entry->basic_entry.length) > last_covered_phys_addr) {
			uint64_t base_addr_to_map;
			uint64_t length_to_map;
			if (e820_entry->basic_entry.base_address <
			    last_covered_phys_addr) {
				base_addr_to_map = last_covered_phys_addr;
				length_to_map =
					e820_entry->basic_entry.base_address +
					e820_entry->basic_entry.length -
					last_covered_phys_addr;
			} else {
				base_addr_to_map =
					e820_entry->basic_entry.base_address;
				length_to_map = e820_entry->basic_entry.length;
			}

			/* Round up to next page boundry */
			length_to_map = ALIGN_FORWARD(length_to_map,
				PAGE_4KB_SIZE);
			if (!mam_insert_range
				    (hva_to_hpa, base_addr_to_map,
				    base_addr_to_map, length_to_map,
				    mapping_attrs)) {
				return FALSE;
			}

			if (!mam_insert_range
				    (hpa_to_hva, base_addr_to_map,
				    base_addr_to_map, length_to_map,
				    MAM_NO_ATTRIBUTES)) {
				return FALSE;
			}

			hmm_set_final_mapped_virt_address(g_hmm,
				base_addr_to_map + length_to_map);
		}

		e820_iter =
			e820_abstraction_iterator_get_next(E820_ORIGINAL_MAP,
				e820_iter);
	}

	return TRUE;
}

static
void hmm_initalize_memory_types_table(void)
{
	uint32_t mtrr_type_index;
	uint32_t pat_type_index;

	for (mtrr_type_index = 0; mtrr_type_index <= MON_PHYS_MEM_LAST_TYPE;
	     mtrr_type_index++) {
		for (pat_type_index = 0;
		     pat_type_index <= MON_PHYS_MEM_LAST_TYPE;
		     pat_type_index++)
			g_hmm->mem_types_table[mtrr_type_index][pat_type_index]
				=
					MON_PHYS_MEM_UNDEFINED;
	}

	/* overwrite several cells */
	g_hmm->mem_types_table[MON_PHYS_MEM_UNCACHABLE][MON_PHYS_MEM_UNCACHABLE]
		=
			MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_UNCACHABLE][MON_PHYS_MEM_UNCACHED] =
		MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_UNCACHABLE]
	[MON_PHYS_MEM_WRITE_COMBINING] = MON_PHYS_MEM_WRITE_COMBINING;
	g_hmm->mem_types_table[MON_PHYS_MEM_UNCACHABLE][
		MON_PHYS_MEM_WRITE_THROUGH]
		= MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_UNCACHABLE][MON_PHYS_MEM_WRITE_BACK]
		=
			MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_UNCACHABLE]
	[MON_PHYS_MEM_WRITE_PROTECTED] = MON_PHYS_MEM_UNCACHABLE;

	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_COMBINING]
	[MON_PHYS_MEM_UNCACHABLE] = MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_COMBINING][
		MON_PHYS_MEM_UNCACHED]
		= MON_PHYS_MEM_WRITE_COMBINING;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_COMBINING]
	[MON_PHYS_MEM_WRITE_COMBINING] = MON_PHYS_MEM_WRITE_COMBINING;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_COMBINING]
	[MON_PHYS_MEM_WRITE_THROUGH] = MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_COMBINING]
	[MON_PHYS_MEM_WRITE_BACK] = MON_PHYS_MEM_WRITE_COMBINING;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_COMBINING]
	[MON_PHYS_MEM_WRITE_PROTECTED] = MON_PHYS_MEM_UNCACHABLE;

	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_THROUGH][
		MON_PHYS_MEM_UNCACHABLE]
		= MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_THROUGH][MON_PHYS_MEM_UNCACHED] =
		MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_THROUGH]
	[MON_PHYS_MEM_WRITE_COMBINING] = MON_PHYS_MEM_WRITE_COMBINING;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_THROUGH]
	[MON_PHYS_MEM_WRITE_THROUGH]
		= MON_PHYS_MEM_WRITE_THROUGH;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_THROUGH][
		MON_PHYS_MEM_WRITE_BACK]
		= MON_PHYS_MEM_WRITE_THROUGH;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_THROUGH]
	[MON_PHYS_MEM_WRITE_PROTECTED] = MON_PHYS_MEM_WRITE_PROTECTED;

	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_PROTECTED]
	[MON_PHYS_MEM_UNCACHABLE] = MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_PROTECTED][
		MON_PHYS_MEM_UNCACHED]
		= MON_PHYS_MEM_WRITE_COMBINING;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_PROTECTED]
	[MON_PHYS_MEM_WRITE_COMBINING] = MON_PHYS_MEM_WRITE_COMBINING;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_PROTECTED]
	[MON_PHYS_MEM_WRITE_THROUGH] = MON_PHYS_MEM_WRITE_THROUGH;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_PROTECTED]
	[MON_PHYS_MEM_WRITE_BACK] = MON_PHYS_MEM_WRITE_PROTECTED;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_PROTECTED]
	[MON_PHYS_MEM_WRITE_PROTECTED] = MON_PHYS_MEM_WRITE_PROTECTED;

	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_BACK][MON_PHYS_MEM_UNCACHABLE]
		=
			MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_BACK][MON_PHYS_MEM_UNCACHED] =
		MON_PHYS_MEM_UNCACHABLE;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_BACK]
	[MON_PHYS_MEM_WRITE_COMBINING] = MON_PHYS_MEM_WRITE_COMBINING;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_BACK][
		MON_PHYS_MEM_WRITE_THROUGH]
		= MON_PHYS_MEM_WRITE_THROUGH;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_BACK][MON_PHYS_MEM_WRITE_BACK]
		=
			MON_PHYS_MEM_WRITE_BACK;
	g_hmm->mem_types_table[MON_PHYS_MEM_WRITE_BACK]
	[MON_PHYS_MEM_WRITE_PROTECTED] = MON_PHYS_MEM_WRITE_PROTECTED;
}

static
void hmm_flash_tlb_callback(cpu_id_t from UNUSED, void *arg UNUSED)
{
	hw_flash_tlb();
}

static
boolean_t hmm_map_phys_page_full_attrs(IN hpa_t page_hpa,
				       IN mam_attributes_t attrs,
				       IN boolean_t flash_all_tlbs_if_needed,
				       OUT hva_t *page_hva)
{
	mam_handle_t hva_to_hpa = hmm_get_hva_to_hpa_mapping(g_hmm);
	mam_handle_t hpa_to_hva = hmm_get_hpa_to_hva_mapping(g_hmm);
	mam_attributes_t attrs_tmp;
	hpa_t hpa_tmp = 0;
	hva_t hva_tmp = 0;
	mam_mapping_result_t mapping_result;
	boolean_t result = TRUE;

	MON_ASSERT((page_hpa & 0xfff) == 0); /* must be aligned on page */

	lock_acquire(hmm_get_update_lock(g_hmm));

	if (mam_get_mapping(hpa_to_hva, page_hpa, &hva_tmp, &attrs_tmp) ==
	    MAM_MAPPING_SUCCESSFUL) {
		mapping_result =
			mam_get_mapping(hva_to_hpa,
				hva_tmp,
				&hpa_tmp,
				&attrs_tmp);
		MON_ASSERT(mapping_result == MAM_MAPPING_SUCCESSFUL);

		if (attrs_tmp.uint32 != attrs.uint32) {
			if (!mam_insert_range
				    (hva_to_hpa, hva_tmp, page_hpa,
				    PAGE_4KB_SIZE, attrs)) {
				result = FALSE;
				goto out;
			}

			if (flash_all_tlbs_if_needed) {
				ipc_destination_t dest;

				dest.addr_shorthand =
					IPI_DST_ALL_EXCLUDING_SELF;
				dest.addr = 0;
				ipc_execute_handler(dest,
					hmm_flash_tlb_callback,
					NULL);
				hw_flash_tlb();
			}
		}

		*page_hva = hva_tmp;

		result = TRUE;
		goto out;
	}

	/* Check 1-1 mapping */
	mapping_result =
		mam_get_mapping(hva_to_hpa, page_hpa, &hpa_tmp, &attrs_tmp);
	if (hmm_is_page_available_for_allocation(mapping_result)) {
		/* the 1-1 mapping is possible; */

		if (!mam_insert_range
			    (hva_to_hpa, page_hpa, page_hpa, PAGE_4KB_SIZE,
			    attrs)) {
			result = FALSE; /* insufficient memory */
			goto out;
		}

		if (!mam_insert_range
			    (hpa_to_hva, page_hpa, page_hpa, PAGE_4KB_SIZE,
			    MAM_NO_ATTRIBUTES)) {
			/* try to restore previous hva_to_hpa mapping */
			mam_insert_not_existing_range(hva_to_hpa,
				page_hpa,
				page_hpa,
				mapping_result);
			result = FALSE;
			goto out;
		}

		*page_hva = page_hpa;

		result = TRUE;
		goto out;
	}

	MON_ASSERT(hpa_tmp != page_hpa);

	if (!hmm_allocate_free_virtual_page(&hva_tmp)) {
		result = FALSE;
		goto out;
	}

	MON_ASSERT(mam_get_mapping(hva_to_hpa, hva_tmp, &hpa_tmp, &attrs_tmp) !=
		MAM_MAPPING_SUCCESSFUL);

	if (!mam_insert_range(hva_to_hpa, hva_tmp, page_hpa, PAGE_4KB_SIZE,
		    attrs)) {
		result = FALSE;
		goto out;
	}

	if (!mam_insert_range
		    (hpa_to_hva, page_hpa, hva_tmp, PAGE_4KB_SIZE,
		    MAM_NO_ATTRIBUTES)) {
		result = FALSE;
		goto out;
	}

	*page_hva = hva_tmp;
	result = TRUE;

out:
	lock_release(hmm_get_update_lock(g_hmm));
	return result;
}


boolean_t remove_initial_hva_to_hpa_mapping_for_extended_heap(void)
{
	boolean_t result = TRUE;
	uint32_t i;
	mam_handle_t hva_to_hpa = hmm_get_hva_to_hpa_mapping(g_hmm);
	mam_handle_t hpa_to_hva = hmm_get_hpa_to_hva_mapping(g_hmm);

	lock_acquire(hmm_get_update_lock(g_hmm));

	for (i = 0; i < g_heap_pa_num; i++) {
		uint64_t page_hpa;
		uint64_t page_hva;
		mam_mapping_result_t mapping_result;
		mam_attributes_t attrs_tmp;

		page_hva = g_additional_heap_base + (i * PAGE_4KB_SIZE);

		mapping_result =
			mam_get_mapping(hva_to_hpa,
				page_hva,
				&page_hpa,
				&attrs_tmp);
		MON_ASSERT(mapping_result == MAM_MAPPING_SUCCESSFUL);

		mapping_result =
			mam_get_mapping(hpa_to_hva,
				page_hpa,
				&page_hva,
				&attrs_tmp);
		MON_ASSERT(mapping_result == MAM_MAPPING_SUCCESSFUL);

		/* Remove old HVA-->HPA mapping */
		if (!mam_insert_not_existing_range
			    (hva_to_hpa, page_hva, PAGE_4KB_SIZE,
			    HMM_INVALID_MEMORY_TYPE)) {
			result = FALSE;
			break;
		}
	}

	lock_release(hmm_get_update_lock(g_hmm));
	return result;
}

boolean_t build_extend_heap_hpa_to_hva(void)
{
	boolean_t result = TRUE;
	uint32_t i;
	mam_handle_t hva_to_hpa = hmm_get_hva_to_hpa_mapping(g_hmm);
	mam_handle_t hpa_to_hva = hmm_get_hpa_to_hva_mapping(g_hmm);

	lock_acquire(hmm_get_update_lock(g_hmm));

	for (i = 0; i < g_heap_pa_num; i++) {
		uint64_t page_hpa;
		uint64_t page_hva;
		mam_mapping_result_t mapping_result;
		mam_attributes_t attrs_tmp;

		page_hva = g_additional_heap_base + (i * PAGE_4KB_SIZE);

		mapping_result =
			mam_get_mapping(hva_to_hpa,
				page_hva,
				&page_hpa,
				&attrs_tmp);
		MON_ASSERT(mapping_result == MAM_MAPPING_SUCCESSFUL);

		/* Insert new HPA-->HVA mapping */
		if (!mam_insert_range
			    (hpa_to_hva, page_hpa, page_hva, PAGE_4KB_SIZE,
			    MAM_NO_ATTRIBUTES)) {
			result = FALSE;
			break;
		}
	}

	lock_release(hmm_get_update_lock(g_hmm));
	return result;
}

static
boolean_t hmm_map_continuous_virtual_buffer_for_pages_internal(IN uint64_t *
							       hpas_array,
							       IN uint32_t
							       num_of_pages,
							       IN boolean_t
							       change_attributes,
							       IN mam_attributes_t
							       new_attrs,
							       IN boolean_t
							       remap_hpa,
							       OUT uint64_t *hva)
{
	uint64_t buffer_hva;
	boolean_t result = TRUE;
	uint32_t i;
	mam_handle_t hva_to_hpa = hmm_get_hva_to_hpa_mapping(g_hmm);
	mam_handle_t hpa_to_hva = hmm_get_hpa_to_hva_mapping(g_hmm);
	ipc_destination_t dest;

	lock_acquire(hmm_get_update_lock(g_hmm));

	if (!hmm_allocate_continuous_free_virtual_pages(num_of_pages,
		    &buffer_hva)) {
		result = FALSE;
		goto out;
	}

	if (!change_attributes) {
		/* If copying attributes from old mapping is requested, than all the
		 * pages must be mapped */
		for (i = 0; i < num_of_pages; i++) {
			uint64_t page_hpa = hpas_array[i];
			uint64_t page_hva;
			mam_attributes_t attrs_tmp;

			uint64_t page_hpa_tmp;

			if (mam_get_mapping(hpa_to_hva, page_hpa, &page_hva,
				    &attrs_tmp) !=
			    MAM_MAPPING_SUCCESSFUL) {
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: ERROR: There is hpa_t (%P) is not mapped and"
					" transferring attributes is requested, please map\n",
					__FUNCTION__,
					page_hpa);
				result = FALSE;
				goto out;
			}

			MON_ASSERT((mam_get_mapping
					    (hva_to_hpa, page_hva,
					    &page_hpa_tmp,
					    &attrs_tmp) ==
				    MAM_MAPPING_SUCCESSFUL)
				&& (page_hpa_tmp == page_hpa));
		}
	}

	for (i = 0; i < num_of_pages; i++) {
		uint64_t page_hpa = hpas_array[i];
		uint64_t page_hva;
		mam_attributes_t attrs;

		if (ALIGN_BACKWARD(page_hpa, PAGE_4KB_SIZE) != page_hpa) {
			MON_LOG(mask_anonymous,
				level_trace,
				"%s: ERROR: There is hpa_t (%P) which is not aligned\n",
				__FUNCTION__,
				page_hpa);
			result = FALSE;
			goto out;
		}

		if (change_attributes) {
			attrs.uint32 = new_attrs.uint32;
		} else {
			/* Take attributes from HVA-->HPA mapping */
			mam_mapping_result_t mapping_result;
			uint64_t page_hva_tmp;
			uint64_t page_hpa_tmp;
			mam_attributes_t attrs_tmp;

			mapping_result =
				mam_get_mapping(hpa_to_hva,
					page_hpa,
					&page_hva_tmp,
					&attrs_tmp);
			MON_ASSERT(mapping_result == MAM_MAPPING_SUCCESSFUL);

			mapping_result =
				mam_get_mapping(hva_to_hpa,
					page_hva_tmp,
					&page_hpa_tmp,
					&attrs);
			MON_ASSERT(mapping_result == MAM_MAPPING_SUCCESSFUL);
		}

		/* Add new HVA-->HPA mapping */
		page_hva = buffer_hva + (i * PAGE_4KB_SIZE);
		if (!mam_insert_range
			    (hva_to_hpa, page_hva, page_hpa, PAGE_4KB_SIZE,
			    attrs)) {
			MON_LOG(mask_anonymous,
				level_trace,
				"%s: Insufficient memory\n",
				__FUNCTION__);
			MON_ASSERT(0);
			result = FALSE;
			goto out;
		}

		if (remap_hpa) {
			mam_mapping_result_t mapping_result;
			uint64_t old_page_hva;
			mam_attributes_t attrs_tmp;

			mapping_result =
				mam_get_mapping(hpa_to_hva,
					page_hpa,
					&old_page_hva,
					&attrs_tmp);
			MON_ASSERT(mapping_result == MAM_MAPPING_SUCCESSFUL);

			/* Remove old HVA-->HPA mapping */
			if (!mam_insert_not_existing_range
				    (hva_to_hpa, old_page_hva, PAGE_4KB_SIZE,
				    HMM_INVALID_MEMORY_TYPE)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Insufficient memory\n",
					__FUNCTION__);
				MON_ASSERT(0);
				result = FALSE;
				goto out;
			}

			/* Insert new HPA-->HVA mapping */
			if (!mam_insert_range
				    (hpa_to_hva, page_hpa, page_hva,
				    PAGE_4KB_SIZE,
				    MAM_NO_ATTRIBUTES)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Insufficient memory\n",
					__FUNCTION__);
				MON_ASSERT(0);
				result = FALSE;
				goto out;
			}
		}
	}

	if (result != FALSE) {
		*hva = buffer_hva;
		dest.addr_shorthand = IPI_DST_ALL_EXCLUDING_SELF;
		dest.addr = 0;
		ipc_execute_handler(dest, hmm_flash_tlb_callback, NULL);
		hw_flash_tlb();
	}

out:
	lock_release(hmm_get_update_lock(g_hmm));
	return result;
}

boolean_t hmm_alloc_continuous_wb_virtual_buffer_for_pages(
	IN uint64_t *hpas_array,
	IN uint32_t num_of_pages,
	IN boolean_t is_writable,
	IN boolean_t
	is_executable,
	OUT uint64_t *hva)
{
	mam_attributes_t attrs;

	attrs.uint32 = 0;
	attrs.paging_attr.writable = is_writable ? 1 : 0;
	attrs.paging_attr.executable = is_executable ? 1 : 0;
	attrs.paging_attr.pat_index = hmm_get_wb_pat_index(g_hmm);

	return hmm_map_continuous_virtual_buffer_for_pages_internal(hpas_array,
		num_of_pages,
		TRUE, attrs,
		FALSE, hva);
}

/*-----------------------------------------------------------*/

boolean_t hmm_initialize(const mon_startup_struct_t *startup_struct)
{
	mam_handle_t hva_to_hpa;
	mam_handle_t hpa_to_hva;
	uint32_t curr_wb_index;
	uint32_t curr_uc_index;
	mam_attributes_t inner_mapping_attrs;
	mam_attributes_t final_mapping_attrs;
	uint64_t mon_page_tables_hpa = 0;
	cpu_id_t i;
	uint64_t first_page_hpa;
	uint64_t first_page_new_hva;
	mam_attributes_t attrs_tmp;
	exec_image_section_iterator_t image_iter;
	const exec_image_section_info_t *image_section_info;

	MON_LOG(mask_anonymous, level_trace, "\nHMM: Initializing...\n");

	lock_initialize(hmm_get_update_lock(g_hmm));

	/* Initialize table of MTRR X PAT types */
	hmm_initalize_memory_types_table();

	/* Get the index of Write Back caching policy */
	curr_wb_index =
		pat_mngr_retrieve_current_earliest_pat_index_for_mem_type
			(MON_PHYS_MEM_WRITE_BACK);
	if (curr_wb_index == PAT_MNGR_INVALID_PAT_INDEX) {
		MON_LOG(mask_anonymous, level_trace,
			"HMM ERROR: Write Back index doesn't exist"
			" in current PAT register\n");
		goto no_destroy_exit;
	}

	curr_uc_index =
		pat_mngr_retrieve_current_earliest_pat_index_for_mem_type
			(MON_PHYS_MEM_UNCACHABLE);
	if (curr_uc_index == PAT_MNGR_INVALID_PAT_INDEX) {
		MON_LOG(mask_anonymous, level_trace,
			"HMM ERROR: UNCACHABLE index doesn't exist"
			" in current PAT register\n");
		goto no_destroy_exit;
	}

	inner_mapping_attrs.uint32 = 0;
	inner_mapping_attrs.paging_attr.writable = 1;
	inner_mapping_attrs.paging_attr.executable = 1;
	inner_mapping_attrs.paging_attr.pat_index = curr_wb_index;

	/* Create HVA -> HPA mapping */
	hva_to_hpa = mam_create_mapping(inner_mapping_attrs);
	if (hva_to_hpa == MAM_INVALID_HANDLE) {
		MON_LOG(mask_anonymous, level_trace,
			"HMM ERROR: Failed to create hva_t -> hpa_t mapping\n");
		goto no_destroy_exit;
	}

	/* / Create HPA -> HVA mapping */
	hpa_to_hva = mam_create_mapping(MAM_NO_ATTRIBUTES);
	if (hpa_to_hva == MAM_INVALID_HANDLE) {
		MON_LOG(mask_anonymous, level_trace,
			"HMM ERROR: Failed to create hpa_t -> hva_t mapping\n");
		goto destroy_hva_to_hpa_mapping_exit;
	}

	hmm_set_hva_to_hpa_mapping(g_hmm, hva_to_hpa);
	hmm_set_hpa_to_hva_mapping(g_hmm, hpa_to_hva);
	hmm_set_current_mon_page_tables(g_hmm, HMM_INVALID_MON_PAGE_TABLES);
	hmm_set_new_allocations_curr_ptr(
		g_hmm, HMM_FIRST_VIRTUAL_ADDRESS_FOR_NEW_ALLOCATIONS);
	hmm_set_final_mapped_virt_address(g_hmm, 0);
	hmm_set_wb_pat_index(g_hmm, curr_wb_index);
	hmm_set_uc_pat_index(g_hmm, curr_uc_index);

	MON_LOG(mask_anonymous, level_trace,
		"HMM: Successfully created hva_t <--> hpa_t mappings\n");

	/* Fill HPA <-> HVA mappings with initial data */
	final_mapping_attrs.uint32 = 0;
	final_mapping_attrs.paging_attr.writable = 1;
	final_mapping_attrs.paging_attr.pat_index = curr_wb_index;
	/* the mapping is not executable */

	/* Map other memory up to 4G + existing memory above 4G */
	if (!hmm_map_remaining_memory(final_mapping_attrs)) {
		MON_LOG(mask_anonymous, level_trace,
			"HMM ERROR: Failed to set initial mapping\n");
		goto destroy_hpa_to_hva_mapping_exit;
	}
	MON_LOG(mask_anonymous, level_trace,
		"HMM: Created initial mapping for range 0 - 4G (+ existing"
		" memory above 4G) with WRITE permissions\n");

	/* Update permissions for MON image */
	MON_LOG(mask_anonymous, level_trace,
		"HMM: Updating permissions to MON image:\n");
	image_section_info = exec_image_section_first((const void *)
		startup_struct->mon_memory_layout[mon_image].base_address,
		startup_struct->mon_memory_layout[mon_image].image_size,
		&image_iter);
	while (image_section_info != NULL) {
		uint64_t section_start = (uint64_t)image_section_info->start;
		uint64_t section_end =
			ALIGN_FORWARD(section_start + image_section_info->size,
				PAGE_4KB_SIZE);
		uint64_t section_size = section_end - section_start;

		MON_ASSERT(ALIGN_BACKWARD(section_start, PAGE_4KB_SIZE) ==
			section_start);

		if (!image_section_info->writable) {
			mam_attributes_t attributes_to_remove;

			attributes_to_remove.uint32 = 0;
			attributes_to_remove.paging_attr.writable = 1;

			if (!mam_remove_permissions_from_existing_mapping
				    (hva_to_hpa, section_start, section_size,
				    attributes_to_remove)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to remove WRITABLE permission"
					" from MON section: [%P - %P]\n",
					section_start,
					section_end);
				goto destroy_hpa_to_hva_mapping_exit;
			}
			MON_LOG(mask_anonymous,
				level_trace,
				"\tHMM: Removed WRITABLE permissions to section [%P - %P]\n",
				section_start,
				section_end);
		}

		if (image_section_info->executable) {
			mam_attributes_t attributes_to_add;

			attributes_to_add.uint32 = 0;
			attributes_to_add.paging_attr.executable = 1;

			if (!mam_add_permissions_to_existing_mapping
				    (hva_to_hpa, section_start, section_size,
				    attributes_to_add)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to add EXECUTABLE permission to"
					" MON section: [%P - %P]\n",
					section_start,
					section_end);
				goto destroy_hpa_to_hva_mapping_exit;
			}
			MON_LOG(mask_anonymous,
				level_trace,
				"\tHMM: Added EXECUTABLE permissions to section [%P - %P]\n",
				section_start,
				section_end);
		}

		image_section_info = exec_image_section_next(&image_iter);
	}

	/* update permissions for the thunk image */
	if (startup_struct->mon_memory_layout[thunk_image].image_size != 0) {
		mam_attributes_t attributes_to_remove;
		mam_attributes_t attributes_to_add;
		mam_attributes_t attr_tmp;
		uint64_t thunk_image_start_hva;

		if (mam_get_mapping
			    (hpa_to_hva,
			    startup_struct->mon_memory_layout[thunk_image].
			    base_address,
			    &thunk_image_start_hva,
			    &attr_tmp) != MAM_MAPPING_SUCCESSFUL) {
			/* Mapping must exist */
			MON_LOG(mask_anonymous, level_trace,
				"hpa_t %P is not mapped to hva_t\n",
				startup_struct->
				mon_memory_layout[thunk_image].base_address);
			MON_ASSERT(0);
			goto destroy_hpa_to_hva_mapping_exit;
		}

		/* remove "writable" permission */
		attributes_to_remove.uint32 = 0;
		attributes_to_remove.paging_attr.writable = 1;
		if (!mam_remove_permissions_from_existing_mapping
			    (hva_to_hpa, thunk_image_start_hva,
			    startup_struct->mon_memory_layout[thunk_image].
			    image_size,
			    attributes_to_remove)) {
			goto destroy_hpa_to_hva_mapping_exit;
		}

		/* add "executable" permission */
		attributes_to_add.uint32 = 0;
		attributes_to_add.paging_attr.executable = 1;
		if (!mam_add_permissions_to_existing_mapping
			    (hva_to_hpa, thunk_image_start_hva,
			    startup_struct->mon_memory_layout[thunk_image].
			    image_size,
			    attributes_to_add)) {
			goto destroy_hpa_to_hva_mapping_exit;
		}
	}

	/* Remap the first virtual page */
	if (mam_get_mapping(hva_to_hpa, 0, &first_page_hpa, &attrs_tmp) !=
	    MAM_MAPPING_SUCCESSFUL) {
		MON_ASSERT(0);
		goto destroy_hpa_to_hva_mapping_exit;
	}

	if (!mam_insert_not_existing_range
		    (hva_to_hpa, 0, PAGE_4KB_SIZE, HMM_INVALID_MEMORY_TYPE)) {
		MON_LOG(mask_anonymous, level_trace,
			"Failed to remove mapping of first page\n");
		goto destroy_hpa_to_hva_mapping_exit;
	}

	MON_LOG(mask_anonymous, level_trace,
		"HMM: Successfully unmapped first virtual page hva_t(%P)"
		" (which was mapped to hpa_t(%P))\n",
		0, first_page_hpa);

	if (!hmm_allocate_free_virtual_page(&first_page_new_hva)) {
		MON_ASSERT(0);
		goto destroy_hpa_to_hva_mapping_exit;
	}

	if (!mam_insert_range
		    (hva_to_hpa, first_page_new_hva, first_page_hpa,
		    PAGE_4KB_SIZE,
		    final_mapping_attrs)) {
		MON_LOG(mask_anonymous,
			level_trace,
			"Failed to remap first page\n");
		goto destroy_hpa_to_hva_mapping_exit;
	}

	if (!mam_insert_range
		    (hpa_to_hva, first_page_hpa, first_page_new_hva,
		    PAGE_4KB_SIZE,
		    MAM_NO_ATTRIBUTES)) {
		MON_LOG(mask_anonymous,
			level_trace,
			"Failed to remap first page\n");
		goto destroy_hpa_to_hva_mapping_exit;
	}

	MON_LOG(mask_anonymous,
		level_trace,
		"HMM: Successfully remapped hpa_t(%P) to hva_t(%P)\n",
		first_page_hpa,
		first_page_new_hva);

	/* Unmap the last page of each stack. */
	MON_ASSERT(mon_stack_is_initialized());
	MON_LOG(mask_anonymous, level_trace,
		"HMM: Remapping the exception stacks:\n");

	for (i = 0; i < startup_struct->number_of_processors_at_boot_time;
	     i++) {
		hva_t page;
		hpa_t page_hpa;
		uint32_t exception_stack_index;
		uint64_t page_to_assign_hva;

		hva_t page_hva_tmp;
		hpa_t page_hpa_tmp;

		for (exception_stack_index = 0;
		     exception_stack_index < idt_get_extra_stacks_required();
		     exception_stack_index++) {
			uint64_t current_extra_stack_hva;

			if (!mon_stacks_get_exception_stack_for_cpu
				    (i, exception_stack_index, &page)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to retrieve page to guard"
					" from the stack\n");
				MON_ASSERT(0);
				goto destroy_hpa_to_hva_mapping_exit;
			}

			if (!mon_hmm_hva_to_hpa(page, &page_hpa)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to map hva_t (%P) -> hpa_t\n",
					page);
				MON_ASSERT(0);
				goto destroy_hpa_to_hva_mapping_exit;
			}

			if (!mam_insert_not_existing_range
				    (hva_to_hpa, page, PAGE_4KB_SIZE,
				    HMM_INVALID_MEMORY_TYPE)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to remove hva_t -> hpa_t mapping\n");
				MON_ASSERT(0);
				goto destroy_hpa_to_hva_mapping_exit;
			}

			if (!mam_insert_not_existing_range
				    (hpa_to_hva, page_hpa, PAGE_4KB_SIZE,
				    HMM_INVALID_MEMORY_TYPE)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to remove hpa_t -> hva_t mapping\n");
				MON_ASSERT(0);
				goto destroy_hpa_to_hva_mapping_exit;
			}

			MON_LOG(mask_anonymous,
				level_trace,
				"\tRemoved the map for hva_t (%P) <--> hpa_t (%P).\n",
				page,
				page_hpa);

			MON_ASSERT(!mon_hmm_hva_to_hpa(page, &page_hpa_tmp));
			MON_ASSERT(!mon_hmm_hpa_to_hva(page_hpa,
					&page_hva_tmp));

			if (!hmm_allocate_continuous_free_virtual_pages
				    (3, &current_extra_stack_hva)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to allocate pages for"
					" extra stacks\n");
				MON_ASSERT(0);
				goto destroy_hpa_to_hva_mapping_exit;
			}

			MON_ASSERT(!mon_hmm_hva_to_hpa(current_extra_stack_hva,
					&page_hpa_tmp));
			MON_ASSERT(!mon_hmm_hva_to_hpa
					(current_extra_stack_hva +
					PAGE_4KB_SIZE,
					&page_hpa_tmp));
			MON_ASSERT(!mon_hmm_hva_to_hpa
					(current_extra_stack_hva +
					(PAGE_4KB_SIZE * 2),
					&page_hpa_tmp));

			if (!mam_insert_not_existing_range
				    (hva_to_hpa, current_extra_stack_hva,
				    PAGE_4KB_SIZE,
				    HMM_VIRT_MEMORY_NOT_FOR_USE)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to mark hva_t (%P) as 'not for use'.\n",
					current_extra_stack_hva);
				MON_ASSERT(0);
				goto destroy_hpa_to_hva_mapping_exit;
			}

			page_to_assign_hva = current_extra_stack_hva +
					     PAGE_4KB_SIZE;

			if (!mam_insert_range
				    (hva_to_hpa, page_to_assign_hva, page_hpa,
				    PAGE_4KB_SIZE,
				    final_mapping_attrs)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to remap hva_t (%P) to hpa_t (%P).\n",
					page_to_assign_hva,
					page_hpa);
				MON_ASSERT(0);
				goto destroy_hpa_to_hva_mapping_exit;
			}

			if (!mam_insert_range
				    (hpa_to_hva, page_hpa, page_to_assign_hva,
				    PAGE_4KB_SIZE,
				    MAM_NO_ATTRIBUTES)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to remap hpa_t (%P) to hva_t (%P).\n",
					page_hpa,
					page_to_assign_hva);
				MON_ASSERT(0);
				goto destroy_hpa_to_hva_mapping_exit;
			}

			if (!mam_insert_not_existing_range
				    (hva_to_hpa, current_extra_stack_hva +
				    (PAGE_4KB_SIZE * 2),
				    PAGE_4KB_SIZE,
				    HMM_VIRT_MEMORY_NOT_FOR_USE)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to mark hva_t (%P) as 'not for use'.\n",
					current_extra_stack_hva +
					(PAGE_4KB_SIZE * 2));
				MON_ASSERT(0);
				goto destroy_hpa_to_hva_mapping_exit;
			}

			MON_ASSERT(mon_hmm_hva_to_hpa(page_to_assign_hva,
					&page_hpa_tmp)
				&& (page_hpa_tmp == page_hpa));
			MON_ASSERT(mon_hmm_hpa_to_hva(page_hpa, &page_hva_tmp)
				&& (page_hva_tmp == page_to_assign_hva));

			MON_LOG(mask_anonymous,
				level_trace,
				"\tRemapped hva_t (%P) <--> hpa_t (%P)\n",
				page_to_assign_hva,
				page_hpa);

			/* The lower and higher pages should remain unmapped in order to
			 * track stack overflow and underflow */
			hw_gdt_set_ist_pointer((cpu_id_t)i,
				(uint8_t)exception_stack_index,
				(address_t)page_to_assign_hva +
				PAGE_4KB_SIZE);
			MON_LOG(mask_anonymous,
				level_trace,
				"\tPage %P (hva_t) is set as exception stack#%d for cpu %d.\n",
				page_to_assign_hva,
				exception_stack_index,
				i);
			MON_LOG(mask_anonymous,
				level_trace,
				"\tPages %P and %P (hva_t) remain unmapped - protecting"
				" exception stack#%d of cpu %d from overflow and underflow.\n",
				page_to_assign_hva - PAGE_4KB_SIZE,
				page_to_assign_hva + PAGE_4KB_SIZE,
				exception_stack_index,
				i);
		}
	}

	/* For late launch support additional heap
	 * Patch the MAM to build non-contiguous pa memory to a contiguous va for
	 * the heap */
	if (g_is_post_launch) {
		if (g_additional_heap_pa) {
			boolean_t ret;

			/* hmm_remap_physical_pages_to_continuous_wb_virtal_addr( */
			ret = hmm_alloc_continuous_wb_virtual_buffer_for_pages(
				(uint64_t *)g_additional_heap_pa, g_heap_pa_num,
				TRUE, FALSE, &g_additional_heap_base);
			MON_LOG(mask_anonymous, level_trace,
				"HMM: Additional heap is mapped to VA = %p\n",
				(void *)g_additional_heap_base);
			if ((!ret) || (g_additional_heap_base == 0)) {
				MON_DEADLOOP();
			}

			if (!remove_initial_hva_to_hpa_mapping_for_extended_heap()) {
				MON_DEADLOOP();
			}
		}
	}

	/* Make the HVA -> HPA mapping hardware compliant, i.e. create mon page
	 * tables */
	if (!mam_convert_to_64bit_page_tables(hva_to_hpa,
		    &mon_page_tables_hpa)) {
		MON_LOG(mask_anonymous, level_trace,
			"HMM ERROR: Failed to MON page tables\n");
		goto destroy_hpa_to_hva_mapping_exit;
	}

	hmm_set_current_mon_page_tables(g_hmm, mon_page_tables_hpa);

	return TRUE;

destroy_hpa_to_hva_mapping_exit:
	mon_mam_destroy_mapping(hpa_to_hva);
destroy_hva_to_hpa_mapping_exit:
	mon_mam_destroy_mapping(hva_to_hpa);
no_destroy_exit:
	return FALSE;
}


hpa_t hmm_get_mon_page_tables(void)
{
	uint64_t curr_page_tables = hmm_get_current_mon_page_tables(g_hmm);

	MON_ASSERT(efer_msr_is_nxe_bit_set(efer_msr_read_reg()));
	return *((hpa_t *)(&curr_page_tables));
}

boolean_t mon_hmm_hva_to_hpa(IN hva_t hva, OUT hpa_t *hpa)
{
	mam_handle_t hva_to_hpa = hmm_get_hva_to_hpa_mapping(g_hmm);
	uint64_t hpa_tmp;
	mam_attributes_t attrs_tmp;
	uint64_t hva_tmp = (uint64_t)hva;

	/* Before hpa/hva mapping is setup, assume 1:1 mapping */
	if (hva_to_hpa == MAM_INVALID_HANDLE) {
		*hpa = (hpa_t)hva;
		return TRUE;
	}

	if (mam_get_mapping(hva_to_hpa, hva_tmp, &hpa_tmp, &attrs_tmp) ==
	    MAM_MAPPING_SUCCESSFUL) {
		*hpa = *((hpa_t *)(&hpa_tmp));
		return TRUE;
	}

	return FALSE;
}

boolean_t mon_hmm_hpa_to_hva(IN hpa_t hpa, OUT hva_t *hva)
{
	mam_handle_t hpa_to_hva = hmm_get_hpa_to_hva_mapping(g_hmm);
	uint64_t hva_tmp;
	mam_attributes_t attrs_tmp;
	uint64_t hpa_tmp = (uint64_t)hpa;

	/* Before hpa/hva mapping is setup, assume 1:1 mapping */
	if (hpa_to_hva == MAM_INVALID_HANDLE) {
		*hva = (hva_t)hpa;
		return TRUE;
	}

	if (mam_get_mapping(hpa_to_hva, hpa_tmp, &hva_tmp, &attrs_tmp) ==
	    MAM_MAPPING_SUCCESSFUL) {
		*hva = *((hva_t *)(&hva_tmp));
		return TRUE;
	}

	return FALSE;
}

boolean_t hmm_is_new_pat_value_consistent(uint64_t pat_value)
{
	uint32_t new_wb_index =
		pat_mngr_get_earliest_pat_index_for_mem_type(
			MON_PHYS_MEM_WRITE_BACK,
			pat_value);
	uint32_t new_uc_index =
		pat_mngr_get_earliest_pat_index_for_mem_type(
			MON_PHYS_MEM_UNCACHABLE,
			pat_value);

	if ((new_wb_index != hmm_get_wb_pat_index(g_hmm)) ||
	    (new_uc_index != hmm_get_uc_pat_index(g_hmm))) {
		return FALSE;
	}

	return TRUE;
}

boolean_t hmm_unmap_hpa(IN hpa_t hpa,
			uint64_t size,
			boolean_t flush_tlbs_on_all_cpus)
{
	mam_handle_t hpa_to_hva = hmm_get_hpa_to_hva_mapping(g_hmm);
	mam_handle_t hva_to_hpa = hmm_get_hva_to_hpa_mapping(g_hmm);
	hva_t hva;
	boolean_t result = TRUE;

	hpa_t hpa_tmp;

	lock_acquire(hmm_get_update_lock(g_hmm));

	if ((ALIGN_BACKWARD(hpa, PAGE_4KB_SIZE) != hpa) ||
	    (ALIGN_FORWARD(size, PAGE_4KB_SIZE) != size)) {
		result = FALSE;
		goto out;
	}

	while (size != 0) {
		if (mon_hmm_hpa_to_hva(hpa, &hva)) {
			MON_ASSERT(mon_hmm_hva_to_hpa(hva,
					&hpa_tmp) && (hpa_tmp == hpa));
			MON_ASSERT(ALIGN_BACKWARD(hva, PAGE_4KB_SIZE) == hva);

			if (!mam_insert_not_existing_range
				    (hpa_to_hva, hpa, PAGE_4KB_SIZE,
				    HMM_INVALID_MEMORY_TYPE)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to unmap hpa_t (%P) mapping\n",
					hpa);
				result = FALSE;
				goto out;
			}

			if (!mam_insert_not_existing_range
				    (hva_to_hpa, hva, PAGE_4KB_SIZE,
				    HMM_INVALID_MEMORY_TYPE)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"HMM ERROR: Failed to unmap hpa_t (%P) mapping\n",
					hpa);
				result = FALSE;
				goto out;
			}
		}

		size -= PAGE_4KB_SIZE;
		hpa += PAGE_4KB_SIZE;
	}

	hw_flash_tlb();

	if (flush_tlbs_on_all_cpus) {
		ipc_destination_t dest;

		dest.addr_shorthand = IPI_DST_ALL_EXCLUDING_SELF;
		dest.addr = 0;
		ipc_execute_handler(dest, hmm_flash_tlb_callback, NULL);
	}

out:

	lock_release(hmm_get_update_lock(g_hmm));
	return result;
}

mon_phys_mem_type_t hmm_get_hpa_type(IN hpa_t hpa)
{
	return mtrrs_abstraction_get_memory_type(hpa);
}

boolean_t hmm_does_memory_range_have_specified_memory_type(IN hpa_t start_hpa,
							   IN uint64_t size,
							   mon_phys_mem_type_t
							   mem_type)
{
	uint64_t checked_size = 0;
	hpa_t curr_hpa = ALIGN_BACKWARD(start_hpa, PAGE_4KB_SIZE);

	if (mem_type == MON_PHYS_MEM_UNDEFINED) {
		return FALSE;
	}

	while ((curr_hpa + checked_size) < (start_hpa + size)) {
		if (hmm_get_hpa_type(curr_hpa) != mem_type) {
			return FALSE;
		}
		curr_hpa += PAGE_4KB_SIZE;
		checked_size += PAGE_4KB_SIZE;
	}

	return TRUE;
}

void hmm_disable_page_level_write_protection(void)
{
	/* Clear WP bit */
	uint64_t cr0 = hw_read_cr0();

	MON_ASSERT((cr0 & HMM_WP_BIT_MASK) != 0);
	cr0 &= (~HMM_WP_BIT_MASK);
	hw_write_cr0(cr0);
}

void hmm_enable_page_level_write_protection(void)
{
	/* Clear WP bit */
	uint64_t cr0 = hw_read_cr0();

	MON_ASSERT((cr0 & HMM_WP_BIT_MASK) == 0);
	cr0 |= HMM_WP_BIT_MASK;
	hw_write_cr0(cr0);
}

void hmm_set_required_values_to_control_registers(void)
{
	hw_write_cr0(hw_read_cr0() | HMM_WP_BIT_MASK);
	efer_msr_set_nxe();     /* Make sure EFER.NXE is set */
}

boolean_t hmm_map_uc_physical_page(IN hpa_t page_hpa,
				   IN boolean_t is_writable,
				   IN boolean_t is_executable,
				   IN boolean_t flash_all_tlbs_if_needed,
				   OUT hva_t *page_hva)
{
	mam_attributes_t attrs;

	attrs.uint32 = 0;
	attrs.paging_attr.writable = (is_writable) ? 1 : 0;
	attrs.paging_attr.executable = (is_executable) ? 1 : 0;
	attrs.paging_attr.pat_index = hmm_get_uc_pat_index(g_hmm);
	return hmm_map_phys_page_full_attrs(page_hpa, attrs,
		flash_all_tlbs_if_needed, page_hva);
}

boolean_t hmm_remap_physical_pages_to_continuous_wb_virtal_addr(IN uint64_t *
								hpas_array,
								IN uint32_t
								num_of_pages,
								IN boolean_t
								is_writable,
								IN boolean_t
								is_executable,
								OUT uint64_t *hva)
{
	mam_attributes_t attrs;

	attrs.uint32 = 0;
	attrs.paging_attr.writable = (is_writable) ? 1 : 0;
	attrs.paging_attr.executable = (is_executable) ? 1 : 0;
	attrs.paging_attr.pat_index = hmm_get_wb_pat_index(g_hmm);

	return hmm_map_continuous_virtual_buffer_for_pages_internal(hpas_array,
		num_of_pages, TRUE,                                                     /* change attributes */
		attrs, TRUE,                                                            /* remap * hpa */
		hva);
}

