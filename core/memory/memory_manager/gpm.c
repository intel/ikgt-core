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
#include <gpm_api.h>
#include <memory_address_mapper_api.h>
#include <host_memory_manager_api.h>
#include <e820_abstraction.h>
#include <heap.h>
#include <mon_dbg.h>
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(GPM_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(GPM_C, __condition)

#define GPM_INVALID_MAPPING (MAM_MAPPING_SUCCESSFUL + 1)
#define GPM_MMIO            (MAM_MAPPING_SUCCESSFUL + 2)

typedef struct {
	mam_handle_t	gpa_to_hpa;
	mam_handle_t	hpa_to_gpa;
} gpm_t;

static boolean_t
gpm_get_range_details_and_advance_mam_iterator(IN mam_handle_t mam_handle,
					       IN OUT mam_memory_ranges_iterator_t
					       *mem_ranges_iter,
					       OUT uint64_t *range_start,
					       OUT uint64_t *range_size)
{
	mam_memory_ranges_iterator_t iter = *mem_ranges_iter;
	mam_mapping_result_t res;

	MON_ASSERT(*mem_ranges_iter != MAM_INVALID_MEMORY_RANGES_ITERATOR);

	do {
		uint64_t tgt_addr;
		mam_attributes_t attrs;

		iter =
			mam_get_range_details_from_iterator(mam_handle,
				iter,
				range_start,
				range_size);

		res = mam_get_mapping(mam_handle,
			*range_start,
			&tgt_addr,
			&attrs);
	} while ((res != MAM_MAPPING_SUCCESSFUL) &&
		 (res != GPM_MMIO) &&
		 (iter != MAM_INVALID_MEMORY_RANGES_ITERATOR));

	*mem_ranges_iter = iter;

	if (iter != MAM_INVALID_MEMORY_RANGES_ITERATOR) {
		return TRUE;
	}

	if ((res == MAM_MAPPING_SUCCESSFUL) || (res == GPM_MMIO)) {
		return TRUE;
	}

	return FALSE;
}

static
boolean_t gpm_remove_all_relevant_hpa_to_gpa_mapping(gpm_t *gpm, gpa_t gpa,
						     uint64_t size)
{
	mam_handle_t gpa_to_hpa;
	mam_handle_t hpa_to_gpa;
	uint64_t gpa_tmp;

	gpa_to_hpa = gpm->gpa_to_hpa;
	hpa_to_gpa = gpm->hpa_to_gpa;

	/* Remove all hpa mappings */
	for (gpa_tmp = gpa; gpa_tmp < gpa + size; gpa_tmp += PAGE_4KB_SIZE) {
		uint64_t hpa;
		mam_attributes_t attrs;

		if (mam_get_mapping(gpa_to_hpa, gpa_tmp, &hpa, &attrs) ==
		    MAM_MAPPING_SUCCESSFUL) {
			if (!mam_insert_not_existing_range
				    (hpa_to_gpa, hpa, PAGE_4KB_SIZE,
				    GPM_INVALID_MAPPING)) {
				return FALSE;
			}
		}
	}

	return TRUE;
}

gpm_handle_t mon_gpm_create_mapping(void)
{
	gpm_t *gpm;
	mam_handle_t gpa_to_hpa;
	mam_handle_t hpa_to_gpa;

	gpm = (gpm_t *)mon_memory_alloc(sizeof(gpm_t));
	if (gpm == NULL) {
		goto failed_to_allocated_gpm;
	}

	gpa_to_hpa = mam_create_mapping(MAM_NO_ATTRIBUTES);
	if (gpa_to_hpa == MAM_INVALID_HANDLE) {
		goto failed_to_allocate_gpa_to_hpa_mapping;
	}

	hpa_to_gpa = mam_create_mapping(MAM_NO_ATTRIBUTES);
	if (hpa_to_gpa == MAM_INVALID_HANDLE) {
		goto failed_to_allocate_hpa_to_gpa_mapping;
	}

	gpm->gpa_to_hpa = gpa_to_hpa;
	gpm->hpa_to_gpa = hpa_to_gpa;
	return (gpm_handle_t)gpm;

failed_to_allocate_hpa_to_gpa_mapping:
	mon_mam_destroy_mapping(gpa_to_hpa);
failed_to_allocate_gpa_to_hpa_mapping:
	mon_memory_free(gpm);
failed_to_allocated_gpm:
	return GPM_INVALID_HANDLE;
}

boolean_t mon_gpm_add_mapping(IN gpm_handle_t gpm_handle,
			      IN gpa_t gpa,
			      IN hpa_t hpa,
			      IN uint64_t size,
			      mam_attributes_t attrs)
{
	gpm_t *gpm = (gpm_t *)gpm_handle;
	mam_handle_t gpa_to_hpa;
	mam_handle_t hpa_to_gpa;

	if (gpm_handle == GPM_INVALID_HANDLE) {
		return FALSE;
	}

	gpa_to_hpa = gpm->gpa_to_hpa;
	hpa_to_gpa = gpm->hpa_to_gpa;

	if (!mam_insert_range(gpa_to_hpa, (uint64_t)gpa, (uint64_t)hpa, size,
		    attrs)) {
		return FALSE;
	}
	return TRUE;
}

boolean_t mon_gpm_remove_mapping(IN gpm_handle_t gpm_handle,
				 IN gpa_t gpa,
				 IN uint64_t size)
{
	gpm_t *gpm = (gpm_t *)gpm_handle;
	mam_handle_t gpa_to_hpa;

	if (gpm_handle == GPM_INVALID_HANDLE) {
		return FALSE;
	}

	gpa_to_hpa = gpm->gpa_to_hpa;
	return (boolean_t)mam_insert_not_existing_range(gpa_to_hpa,
		(uint64_t)gpa,
		size,
		GPM_INVALID_MAPPING);
}

boolean_t gpm_add_mmio_range(IN gpm_handle_t gpm_handle,
			     IN gpa_t gpa,
			     IN uint64_t size)
{
	gpm_t *gpm = (gpm_t *)gpm_handle;
	mam_handle_t gpa_to_hpa;

	if (gpm_handle == GPM_INVALID_HANDLE) {
		return FALSE;
	}

	gpa_to_hpa = gpm->gpa_to_hpa;
	return (boolean_t)mam_insert_not_existing_range(gpa_to_hpa,
		(uint64_t)gpa,
		size, GPM_MMIO);
}

boolean_t mon_gpm_is_mmio_address(IN gpm_handle_t gpm_handle, IN gpa_t gpa)
{
	gpm_t *gpm = (gpm_t *)gpm_handle;
	mam_handle_t gpa_to_hpa;
	uint64_t hpa_tmp;
	mam_mapping_result_t res;
	mam_attributes_t attrs;

	if (gpm_handle == GPM_INVALID_HANDLE) {
		return FALSE;
	}

	gpa_to_hpa = gpm->gpa_to_hpa;
	res = (boolean_t)mam_get_mapping(gpa_to_hpa,
		(uint64_t)gpa,
		&hpa_tmp,
		&attrs);
	if (res != GPM_MMIO) {
		return FALSE;
	}
	return TRUE;
}

boolean_t mon_gpm_gpa_to_hpa(IN gpm_handle_t gpm_handle,
			     IN gpa_t gpa,
			     OUT hpa_t *hpa,
			     OUT mam_attributes_t *hpa_attrs)
{
	gpm_t *gpm = (gpm_t *)gpm_handle;
	mam_handle_t gpa_to_hpa;
	uint64_t hpa_tmp;
	mam_mapping_result_t res;
	mam_attributes_t attrs;

	if (gpm_handle == GPM_INVALID_HANDLE) {
		return FALSE;
	}

	gpa_to_hpa = gpm->gpa_to_hpa;
	res = mam_get_mapping(gpa_to_hpa, (uint64_t)gpa, &hpa_tmp, &attrs);
	if (res != MAM_MAPPING_SUCCESSFUL) {
		return FALSE;
	}

	*hpa = *((hpa_t *)(&hpa_tmp));
	*hpa_attrs = *((mam_attributes_t *)(&attrs));
	return TRUE;
}

boolean_t gpm_gpa_to_hva(IN gpm_handle_t gpm_handle,
			 IN gpa_t gpa,
			 OUT hva_t *hva)
{
	gpm_t *gpm = (gpm_t *)gpm_handle;
	mam_handle_t gpa_to_hpa;
	uint64_t hpa_tmp;
	uint64_t hva_tmp;
	hpa_t hpa;
	mam_mapping_result_t res;
	mam_attributes_t attrs;

	if (gpm_handle == GPM_INVALID_HANDLE) {
		return FALSE;
	}

	gpa_to_hpa = gpm->gpa_to_hpa;
	res = (boolean_t)mam_get_mapping(gpa_to_hpa,
		(uint64_t)gpa,
		&hpa_tmp,
		&attrs);
	if (res != MAM_MAPPING_SUCCESSFUL) {
		return FALSE;
	}

	hpa = *((hpa_t *)(&hpa_tmp));
	res = mon_hmm_hpa_to_hva(hpa, &hva_tmp);
	if (res) {
		*hva = *((hva_t *)(&hva_tmp));
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"Warning!!! Failed Translation Host Physical"
			" to Host Virtual\n");
	}
	return res;
}

boolean_t mon_gpm_hpa_to_gpa(IN gpm_handle_t gpm_handle,
			     IN hpa_t hpa,
			     OUT gpa_t *gpa)
{
	gpm_t *gpm = (gpm_t *)gpm_handle;
	mam_handle_t hpa_to_gpa;
	uint64_t gpa_tmp;
	mam_mapping_result_t res;
	mam_attributes_t attrs;

	if (gpm_handle == GPM_INVALID_HANDLE) {
		return FALSE;
	}

	hpa_to_gpa = gpm->hpa_to_gpa;
	res = mam_get_mapping(hpa_to_gpa, (uint64_t)hpa, &gpa_tmp, &attrs);
	if (res != MAM_MAPPING_SUCCESSFUL) {
		return FALSE;
	}

	*gpa = *((gpa_t *)(&gpa_tmp));
	return TRUE;
}

boolean_t gpm_create_e820_map(IN gpm_handle_t gpm_handle,
			      OUT e820_handle_t *e820_handle)
{
	gpm_t *gpm = (gpm_t *)gpm_handle;
	mam_handle_t gpa_to_hpa;
	e820_handle_t e820_map;
	e820_abstraction_range_iterator_t e820_iter;
	mam_memory_ranges_iterator_t mem_ranges_iter;
	gpa_t range_start;
	uint64_t range_size;
	gpa_t addr;
	uint64_t size;

	if (gpm_handle == GPM_INVALID_HANDLE) {
		return FALSE;
	}

	gpa_to_hpa = gpm->gpa_to_hpa;
	if (!e820_abstraction_create_new_map(&e820_map)) {
		return FALSE;
	}

	e820_iter = e820_abstraction_iterator_get_first(E820_ORIGINAL_MAP);
	mem_ranges_iter = mam_get_memory_ranges_iterator(gpa_to_hpa);

	if ((mem_ranges_iter == MAM_INVALID_MEMORY_RANGES_ITERATOR) ||
	    (e820_iter == E820_ABSTRACTION_NULL_ITERATOR)) {
		return FALSE;
	}

	if (!gpm_get_range_details_and_advance_mam_iterator
		    (gpa_to_hpa, &mem_ranges_iter, &range_start, &range_size)) {
		/* No appropriate ranges exist */
		return FALSE;
	}

	while (e820_iter != E820_ABSTRACTION_NULL_ITERATOR) {
		const int15_e820_memory_map_entry_ext_t *orig_map_entry =
			e820_abstraction_iterator_get_range_details(e820_iter);

		if (((uint64_t)range_start >=
		     orig_map_entry->basic_entry.base_address)
		    && ((uint64_t)range_start + range_size <=
			orig_map_entry->basic_entry.base_address +
			orig_map_entry->basic_entry.length)) {
			boolean_t encountered_non_contigues_regions = FALSE;

			while ((mem_ranges_iter !=
				MAM_INVALID_MEMORY_RANGES_ITERATOR) &&
			       ((uint64_t)range_start + range_size <=
				orig_map_entry->basic_entry.base_address +
				orig_map_entry->basic_entry.length)) {
				if (!
				    gpm_get_range_details_and_advance_mam_iterator
					    (gpa_to_hpa, &mem_ranges_iter,
					    &addr, &size)) {
					/* There are no more ranges */
					break;
				}

				if (addr > (range_start + range_size)) {
					if (!e820_abstraction_add_new_range
						    (e820_map, range_start,
						    range_size,
						    orig_map_entry->basic_entry.
						    address_range_type,
						    orig_map_entry->
						    extended_attributes)) {
						goto failed_to_fill;
					}

					range_start = addr;
					range_size = size;
					encountered_non_contigues_regions =
						TRUE;
					break;
				} else {
					MON_ASSERT(addr ==
						(range_start + range_size));
					range_size += size;
				}
			}

			if (encountered_non_contigues_regions) {
				/* resume outer loop iterations. */
				continue;
			}

			if ((uint64_t)range_start + range_size >
			    orig_map_entry->basic_entry.base_address +
			    orig_map_entry->basic_entry.length) {
				/* There are global_case for it */
				continue;
			}

			MON_ASSERT(
				mem_ranges_iter ==
				MAM_INVALID_MEMORY_RANGES_ITERATOR);
			if (!e820_abstraction_add_new_range
				    (e820_map, range_start, range_size,
				    orig_map_entry->basic_entry.
				    address_range_type,
				    orig_map_entry->extended_attributes)) {
				goto failed_to_fill;
			}
			/* There are no more gpm ranges */
			break;
		} else if ((range_start + range_size) <=
			   orig_map_entry->basic_entry.base_address) {
			/* Skip the range */
			if (mem_ranges_iter ==
			    MAM_INVALID_MEMORY_RANGES_ITERATOR) {
				/* No more valid ranges */
				break;
			}
			if (!gpm_get_range_details_and_advance_mam_iterator
				    (gpa_to_hpa, &mem_ranges_iter, &range_start,
				    &range_size)) {
				break;
			}
			continue;
		} else if (orig_map_entry->basic_entry.base_address +
			   orig_map_entry->basic_entry.length <= range_start) {
			e820_iter =
				e820_abstraction_iterator_get_next(
					E820_ORIGINAL_MAP,
					e820_iter);
			continue;
		} else if ((range_start <
			    orig_map_entry->basic_entry.base_address) &&
			   (range_start + range_size >
			    orig_map_entry->basic_entry.base_address)) {
			range_size =
				range_start + range_size -
				orig_map_entry->basic_entry.base_address;
			range_start = orig_map_entry->basic_entry.base_address;
			continue;
		} else {
			uint64_t new_size =
				orig_map_entry->basic_entry.base_address +
				orig_map_entry->basic_entry.length -
				range_start;
			MON_ASSERT(
				range_start >=
				orig_map_entry->basic_entry.base_address);
			MON_ASSERT(range_start + range_size >
				orig_map_entry->basic_entry.base_address +
				orig_map_entry->basic_entry.length);
			MON_ASSERT(new_size > 0);

			if (!e820_abstraction_add_new_range
				    (e820_map, range_start, new_size,
				    orig_map_entry->basic_entry.
				    address_range_type,
				    orig_map_entry->extended_attributes)) {
				goto failed_to_fill;
			}
			range_start += new_size;
			range_size -= new_size;
			continue;
		}
	}

	*e820_handle = e820_map;

	/* MON_DEBUG_CODE(
	 * MON_LOG(mask_anonymous, level_trace,"Guest Memory map\n");
	 * e820_abstraction_print_memory_map(e820_map);
	 * ) */

	return TRUE;

failed_to_fill:
	e820_abstraction_destroy_map(e820_map);
	return FALSE;
}

void gpm_destroy_e820_map(IN e820_handle_t e820_handle)
{
	e820_abstraction_destroy_map(e820_handle);
}

mam_memory_ranges_iterator_t
gpm_advance_mam_iterator_to_appropriate_range(mam_handle_t mam_handle,
					      mam_memory_ranges_iterator_t iter)
{
	uint64_t src_addr;
	uint64_t tgt_addr;
	mam_attributes_t attr;
	mam_mapping_result_t res;
	uint64_t size;

	src_addr = mam_get_range_start_address_from_iterator(mam_handle, iter);

	res = mam_get_mapping(mam_handle, src_addr, &tgt_addr, &attr);
	while ((res != MAM_MAPPING_SUCCESSFUL) &&
	       (res != GPM_MMIO) &&
	       (iter != MAM_INVALID_MEMORY_RANGES_ITERATOR)) {
		iter =
			mam_get_range_details_from_iterator(mam_handle,
				iter,
				&src_addr,
				&size);
		src_addr = mam_get_range_start_address_from_iterator(mam_handle,
			iter);
		res = mam_get_mapping(mam_handle, src_addr, &tgt_addr, &attr);
	}

	return iter;
}

gpm_ranges_iterator_t gpm_get_ranges_iterator(IN gpm_handle_t gpm_handle)
{
	gpm_t *gpm = (gpm_t *)gpm_handle;
	mam_handle_t gpa_to_hpa;
	mam_memory_ranges_iterator_t iter;

	if (gpm_handle == GPM_INVALID_HANDLE) {
		return GPM_INVALID_RANGES_ITERATOR;
	}

	gpa_to_hpa = gpm->gpa_to_hpa;

	iter = mam_get_memory_ranges_iterator(gpa_to_hpa);

	if (iter == MAM_INVALID_MEMORY_RANGES_ITERATOR) {
		return GPM_INVALID_RANGES_ITERATOR;
	}

	iter = gpm_advance_mam_iterator_to_appropriate_range(gpa_to_hpa, iter);
	if (iter == MAM_INVALID_MEMORY_RANGES_ITERATOR) {
		return GPM_INVALID_RANGES_ITERATOR;
	}

	return (gpm_ranges_iterator_t)iter;
}

gpm_ranges_iterator_t gpm_get_range_details_from_iterator(IN gpm_handle_t
							  gpm_handle,
							  IN gpm_ranges_iterator_t
							  iter,
							  OUT gpa_t *gpa_out,
							  OUT uint64_t *size_out)
{
	gpm_t *gpm = (gpm_t *)gpm_handle;
	mam_handle_t gpa_to_hpa;
	mam_memory_ranges_iterator_t mam_iter =
		(mam_memory_ranges_iterator_t)iter;

	MON_ASSERT(gpm_handle != GPM_INVALID_HANDLE);

	if (iter == GPM_INVALID_RANGES_ITERATOR) {
		*gpa_out = ~((uint64_t)0);
		*size_out = 0;
		return GPM_INVALID_RANGES_ITERATOR;
	}

	gpa_to_hpa = gpm->gpa_to_hpa;
	mam_iter =
		mam_get_range_details_from_iterator(gpa_to_hpa,
			mam_iter,
			gpa_out,
			size_out);
	mam_iter =
		gpm_advance_mam_iterator_to_appropriate_range(gpa_to_hpa,
			mam_iter);

	if (mam_iter == MAM_INVALID_MEMORY_RANGES_ITERATOR) {
		return GPM_INVALID_RANGES_ITERATOR;
	}
	return (gpm_ranges_iterator_t)mam_iter;
}


void gpm_print(gpm_handle_t gpm_handle USED_IN_DEBUG_ONLY)
{
	MON_DEBUG_CODE(
		e820_handle_t guest_e820 = NULL;
		gpm_ranges_iterator_t gpm_iter = GPM_INVALID_RANGES_ITERATOR;
		boolean_t status = FALSE;
		gpa_t guest_range_addr = 0;
		uint64_t guest_range_size = 0;
		hpa_t host_range_addr = 0;
		mam_attributes_t attrs;
		MON_LOG(mask_anonymous, level_trace, "GPM ranges:\r\n");
		gpm_iter = gpm_get_ranges_iterator(gpm_handle);
		while (GPM_INVALID_RANGES_ITERATOR != gpm_iter) {
			gpm_iter =
				gpm_get_range_details_from_iterator(gpm_handle,
					gpm_iter,
					&guest_range_addr,
					&guest_range_size);
			status =
				mon_gpm_gpa_to_hpa(gpm_handle, guest_range_addr,
					&host_range_addr, &attrs);
			if (FALSE == status) {
				MON_LOG(mask_anonymous, level_trace,
					"GPM no mapping for gpa %p\r\n",
					guest_range_addr);
			} else {
				MON_LOG(mask_anonymous, level_trace,
					"  base %p size %p\r\n",
					guest_range_addr, guest_range_size);
			}
		}

		gpm_create_e820_map(gpm_handle, &guest_e820);
		e820_abstraction_print_memory_map(guest_e820);
		)
}

boolean_t mon_gpm_copy(gpm_handle_t src,
		       gpm_handle_t dst,
		       boolean_t override_attrs,
		       mam_attributes_t set_attrs)
{
	gpm_ranges_iterator_t src_iter = GPM_INVALID_RANGES_ITERATOR;
	gpa_t guest_range_addr = 0;
	uint64_t guest_range_size = 0;
	hpa_t host_range_addr = 0;
	boolean_t status = FALSE;
	mam_attributes_t attrs;

	src_iter = gpm_get_ranges_iterator(src);

	while (GPM_INVALID_RANGES_ITERATOR != src_iter) {
		src_iter = gpm_get_range_details_from_iterator(src,
			src_iter,
			&guest_range_addr,
			&guest_range_size);
		status = mon_gpm_gpa_to_hpa(src,
			guest_range_addr,
			&host_range_addr,
			&attrs);
		if (FALSE == status) {
			/* no mapping - is it mmio? */
			if (mon_gpm_is_mmio_address(src, guest_range_addr)) {
				status = gpm_add_mmio_range(dst,
					guest_range_addr,
					guest_range_size);
				if (FALSE == status) {
					goto failure;
				}
			} else {
				/* normal memory - should not fail the mapping translation */
				goto failure;
			}
		} else {
			if (override_attrs) {
				status = mon_gpm_add_mapping(dst,
					guest_range_addr,
					host_range_addr,
					guest_range_size,
					set_attrs);
			} else {
				status = mon_gpm_add_mapping(dst,
					guest_range_addr,
					host_range_addr,
					guest_range_size,
					attrs);
			}
			if (FALSE == status) {
				goto failure;
			}
		}
	}

	return TRUE;

failure:
	return FALSE;
}
