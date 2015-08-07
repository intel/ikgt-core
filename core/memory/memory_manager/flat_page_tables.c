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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(FLAT_PAGE_TABLES_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(FLAT_PAGE_TABLES_C, __condition)
#include <mon_defs.h>
#include <guest.h>
#include <guest_cpu.h>
#include <gpm_api.h>
#include <mon_dbg.h>
#include <memory_address_mapper_api.h>
#include <mon_phys_mem_types.h>
#include <pat_manager.h>
#include <flat_page_tables.h>

#define FTP_INVALID_RANGE (MAM_MAPPING_SUCCESSFUL + 1)

typedef struct {
	mam_handle_t		mapping;
	mam_attributes_t	default_attrs;
	uint32_t		padding;
} fpt_t;

fpt_t *save_fpt_32 = NULL;
uint32_t save_first_table_32 = 0;
fpt_t *save_fpt_64 = NULL;
uint64_t save_first_table_64 = 0;

static
boolean_t fpt_create_flat_page_tables(IN guest_cpu_handle_t gcpu,
				      IN boolean_t is_32_bit,
				      IN mon_phys_mem_type_t mem_type,
				      OUT fpt_t **flat_tables_handle,
				      OUT uint64_t *first_table)
{
	guest_handle_t guest;
	gpm_handle_t gpm;
	gpm_ranges_iterator_t iter;
	mam_handle_t flat_tables;
	mam_attributes_t attrs_full_perm, attrs;
	uint64_t pat;
	uint32_t pat_index;
	fpt_t *fpt = NULL;

	if (is_32_bit) {
		if (save_fpt_32 != NULL) {
			*flat_tables_handle = save_fpt_32;
			*first_table = save_first_table_32;
			return TRUE;
		}
	} else {
		if (save_fpt_64 != NULL) {
			*flat_tables_handle = save_fpt_64;
			*first_table = save_first_table_64;
			return TRUE;
		}
	}

	MON_ASSERT(gcpu != NULL);

	guest = mon_gcpu_guest_handle(gcpu);
	MON_ASSERT(guest != NULL);

	gpm = gcpu_get_current_gpm(guest);
	MON_ASSERT(gpm != NULL);

	if (gpm == GPM_INVALID_HANDLE) {
		return FALSE;
	}

	fpt = (fpt_t *)mon_malloc(sizeof(fpt_t));
	if (fpt == NULL) {
		return FALSE;
	}

	pat = gcpu_get_msr_reg(gcpu, IA32_MON_MSR_PAT);
	pat_index = pat_mngr_get_earliest_pat_index_for_mem_type(mem_type, pat);
	if (pat_index == PAT_MNGR_INVALID_PAT_INDEX) {
		MON_LOG(mask_anonymous, level_trace,
			"%s: Failed to retrieve PAT index for mem_type=%d\n",
			__FUNCTION__, mem_type);
		MON_DEADLOOP();
		goto failed_to_retrieve_pat_index;
	}

	/* MON_LOG(mask_anonymous, level_trace,"%s: Created flat page tables with
	 * pat_index=%d\n", __FUNCTION__, pat_index); */

	attrs_full_perm.uint32 = 0;
	attrs_full_perm.paging_attr.writable = 1;
	attrs_full_perm.paging_attr.user = 1;
	attrs_full_perm.paging_attr.executable = 1;
	attrs_full_perm.paging_attr.pat_index = pat_index;

	fpt->default_attrs = attrs_full_perm;

	flat_tables = mam_create_mapping(attrs_full_perm);
	if (flat_tables == MAM_INVALID_HANDLE) {
		goto failed_to_create_flat_page_tables;
	}

	fpt->mapping = flat_tables;

	iter = gpm_get_ranges_iterator(gpm);
	if (iter == GPM_INVALID_RANGES_ITERATOR) {
		goto failed_to_get_iterator;
	}

	while (iter != GPM_INVALID_RANGES_ITERATOR) {
		gpa_t curr_gpa;
		uint64_t curr_size;
		hpa_t curr_hpa;

		iter =
			gpm_get_range_details_from_iterator(gpm, iter,
				(uint64_t *)&curr_gpa,
				&curr_size);
		MON_ASSERT(curr_size != 0);

		if (is_32_bit) {
			if (curr_gpa >= (uint64_t)4 GIGABYTES) {
				break; /* no more mappings */
			}

			if ((curr_gpa + curr_size) > (uint64_t)4 GIGABYTES) {
				curr_size = ((uint64_t)4 GIGABYTES) - curr_gpa;
			}
		}

		if (mon_gpm_gpa_to_hpa(gpm, curr_gpa, &curr_hpa, &attrs)) {
			if (!mam_insert_range
				    (flat_tables, curr_gpa, curr_hpa, curr_size,
				    attrs_full_perm)) {
				goto inset_to_flat_tables_failed;
			}
		}
	}

	if (is_32_bit) {
		uint32_t first_table_tmp;
		if (!mam_convert_to_32bit_pae_page_tables
			    (flat_tables, &first_table_tmp)) {
			goto failed_to_get_hardware_compliant_tables;
		}
		*first_table = first_table_tmp;

		save_fpt_32 = fpt;
		save_first_table_32 = first_table_tmp;
	} else {
		if (!mam_convert_to_64bit_page_tables(flat_tables,
			    first_table)) {
			goto failed_to_get_hardware_compliant_tables;
		}

		save_fpt_64 = fpt;
		save_first_table_64 = *first_table;
	}

	*flat_tables_handle = fpt;
	return TRUE;

failed_to_get_hardware_compliant_tables:
inset_to_flat_tables_failed:
failed_to_get_iterator:
	mon_mam_destroy_mapping(flat_tables);
failed_to_create_flat_page_tables:
failed_to_retrieve_pat_index:
	mon_mfree(fpt);
	*flat_tables_handle = (fpt_t *)FPT_INVALID_HANDLE;
	*first_table = ~((uint64_t)0);
	return FALSE;
}

/* allocate 32bit FPTs in physical memory under 4G
 * used only in NO-UG machines, after S3.
 * assumptions:
 * 1) the memory type is MON_PHYS_MEM_WRITE_BACK
 * 2) the GPA<->HPA is identity map at the OS S3 resume time.
 *
 * we made this assumptions since:
 * 1) when this function is called, the handles of data structures, e.g. gcpu,
 * guest, gpm are not ready.
 * 2) and we cannot call mon_gpm_gpa_to_hpa to convert GPA to HPA.
 * 3) we also assume all the processors use the same 32bit FTP tables. */
boolean_t fpt_create_32_bit_flat_page_tables_under_4G(uint64_t highest_address)
{
	mam_handle_t flat_tables;
	mam_attributes_t attrs_full_perm;
	fpt_t *fpt = NULL;
	uint32_t first_table_tmp;
	gpa_t curr_gpa = 0;       /* start from zero. */
	uint64_t curr_size;
	hpa_t curr_hpa;
	gpa_t max_physical_addr =
		highest_address <
		(uint64_t)4 GIGABYTES ? highest_address : (uint64_t)4 GIGABYTES;

	fpt = (fpt_t *)mon_memory_alloc(sizeof(fpt_t));
	if (fpt == NULL) {
		return FALSE;
	}

	attrs_full_perm.uint32 = 0;
	attrs_full_perm.paging_attr.writable = 1;
	attrs_full_perm.paging_attr.user = 1;
	attrs_full_perm.paging_attr.executable = 1;
	/* assume -- * MON_PHYS_MEM_WRITE_BACK */
	attrs_full_perm.paging_attr.pat_index = 0;

	fpt->default_attrs = attrs_full_perm;

	flat_tables = mam_create_mapping(attrs_full_perm);
	if (flat_tables == MAM_INVALID_HANDLE) {
		goto failed_to_create_flat_page_tables;
	}

	fpt->mapping = flat_tables;

	/* align to the 4K */
	max_physical_addr = ALIGN_FORWARD(max_physical_addr, PAGE_4KB_SIZE);

	/* make sure the max_physical_addr is less than 4G. */
	if (max_physical_addr > (uint64_t)4 GIGABYTES) {
		max_physical_addr -= PAGE_4KB_SIZE;
	}

	curr_size = max_physical_addr - curr_gpa;

	/* assume GPA=HPA
	 * we cannot use gpa_to_hpa() function since its mapping structure is not
	 * ready at this time. */
	curr_hpa = curr_gpa;

	if (!mam_insert_range
		    (flat_tables, curr_gpa, curr_hpa, curr_size,
		    attrs_full_perm)) {
		goto inset_to_flat_tables_failed;
	}

	if (!mam_convert_to_32bit_pae_page_tables(flat_tables,
		    &first_table_tmp)) {
		goto failed_to_get_hardware_compliant_tables;
	}

	/* cache them */
	save_fpt_32 = fpt;
	save_first_table_32 = first_table_tmp;

	return TRUE;

failed_to_get_hardware_compliant_tables:
inset_to_flat_tables_failed:
	mon_mam_destroy_mapping(flat_tables);
	flat_tables = MAM_INVALID_HANDLE;
failed_to_create_flat_page_tables:
	mon_memory_free(fpt);
	fpt = NULL;

	return FALSE;
}

boolean_t fpt_create_32_bit_flat_page_tables(IN guest_cpu_handle_t gcpu,
					     OUT fpt_flat_page_tables_handle_t *
					     flat_page_tables_handle,
					     OUT uint32_t *pdpt)
{
	uint64_t pdpt_tmp = *pdpt;
	boolean_t result;

	if (gcpu == NULL) {
		MON_LOG(mask_anonymous, level_trace,
			"%s: gcpu == NULL, returning FALSE\n", __FUNCTION__);
		return FALSE;
	}

	result =
		fpt_create_flat_page_tables(gcpu, TRUE, MON_PHYS_MEM_WRITE_BACK,
			(fpt_t **)flat_page_tables_handle,
			&pdpt_tmp);
	MON_ASSERT(pdpt_tmp <= 0xffffffff);
	*pdpt = (uint32_t)pdpt_tmp;
	return result;
}

boolean_t fpt_create_64_bit_flat_page_tables(IN guest_cpu_handle_t gcpu,
					     OUT fpt_flat_page_tables_handle_t *
					     flat_page_tables_handle,
					     OUT uint64_t *pml4t)
{
	if (gcpu == NULL) {
		MON_LOG(mask_anonymous, level_trace,
			"%s: gcpu == NULL, returning FALSE\n", __FUNCTION__);
		return FALSE;
	}
	return fpt_create_flat_page_tables(gcpu, FALSE, MON_PHYS_MEM_WRITE_BACK,
		(fpt_t **)flat_page_tables_handle, pml4t);
}

boolean_t fpt_destroy_flat_page_tables(IN fpt_flat_page_tables_handle_t
				       flat_page_tables_handle)
{
	if (flat_page_tables_handle == FPT_INVALID_HANDLE) {
		MON_LOG(mask_anonymous, level_trace,
			"%s: Invalid handle, returning FALSE\n", __FUNCTION__);
		return FALSE;
	}

	/* do not destroy the FPTs, cache those page tables
	 * to support S3 resume on NHM machine with x64 OS installed. */
	return TRUE;
}

boolean_t fpt_iterator_get_range(IN fpt_flat_page_tables_handle_t
				 flat_page_tables_handle,
				 IN fpt_ranges_iterator_t iter,
				 OUT uint64_t *src_addr, OUT uint64_t *size)
{
	fpt_t *fpt = (fpt_t *)flat_page_tables_handle;
	mam_handle_t flat_tables_mapping;
	mam_memory_ranges_iterator_t mam_iter =
		(mam_memory_ranges_iterator_t)iter;

	if (flat_page_tables_handle == FPT_INVALID_HANDLE) {
		MON_LOG(mask_anonymous, level_trace,
			"%s: Invalid handle, returning FALSE\n", __FUNCTION__);
		return FALSE;
	}

	if (iter == FPT_INVALID_ITERAROR) {
		MON_LOG(mask_anonymous, level_trace,
			"%s: Invalid iterator, returning FALSE\n",
			__FUNCTION__);
		return FALSE;
	}

	flat_tables_mapping = fpt->mapping;

	if (flat_tables_mapping == MAM_INVALID_HANDLE) {
		MON_LOG(mask_anonymous, level_trace,
			"%s: Something is wrong with handle, returning FALSE\n",
			__FUNCTION__);
		MON_ASSERT(0);
		return FALSE;
	}

	MON_ASSERT(mam_iter != MAM_INVALID_MEMORY_RANGES_ITERATOR);

	mam_get_range_details_from_iterator(flat_tables_mapping,
		mam_iter,
		src_addr,
		size);

	return TRUE;
}
