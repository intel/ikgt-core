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

#ifndef HOST_MEMORY_MANAGER_API_H
#define HOST_MEMORY_MANAGER_API_H

#include <mon_defs.h>
#include <mon_startup.h>
#include <common_libc.h>
#include <mon_phys_mem_types.h>

#define HMM_INVALID_MON_PAGE_TABLES (~((uint64_t)0x0))


/*-------------------------------------------------------------------------
 * Function: hmm_initialize
 *  Description: This function should be called in order to
 *               initialize Host Memory Manager. This function must be called
 *               first.
 *  Input: startup_struct - pointer to startup data structure
 *  Return Value: TRUE in case the initialization is successful.
 *------------------------------------------------------------------------- */
boolean_t hmm_initialize(const mon_startup_struct_t *startup_struct);


/*-------------------------------------------------------------------------
 * Function: hmm_get_mon_page_tables
 *  Description: This function will create existing mapping to hardware
 *               compliant.
 *               IMPORTANT: It will use its own existing HVA-->HPA mapping in
 *               order to create hardware compliant page tables, hence the
 *               mapping must already exist.
 *  Return Value: Host Physical Address of MON page tables. In case of failure
 *                (tables weren't created) the HMM_INVALID_MON_PAGE_TABLES value
 *                will be returned.
 *------------------------------------------------------------------------- */
hpa_t hmm_get_mon_page_tables(void);


/*-------------------------------------------------------------------------
 * Function: mon_hmm_hva_to_hpa
 *  Description: This function is used in order to convert Host Virtual Address
 *               to Host Physical Address (HVA-->HPA).
 *  Input: hva - host virtual address.
 *  Output: hpa - host physical address.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t mon_hmm_hva_to_hpa(IN hva_t hva, OUT hpa_t *hpa);

/*-------------------------------------------------------------------------
 * Function: mon_hmm_hpa_to_hva
 *  Description: This function is used in order to convert Host Physical Address
 *               to Host Virtual Address (HPA-->HVA), i.e. converting physical
 *               address to pointer.
 *  Input: hpa - host physical address.
 *  Output: hva - host virtual address.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t mon_hmm_hpa_to_hva(IN hpa_t hpa, OUT hva_t *hva);

/*-------------------------------------------------------------------------
 * Function: hmm_is_new_pat_value_consistent
 *  Description: This function is used to check whether HMM could work with
 *                new PAT value.
 *------------------------------------------------------------------------- */
boolean_t hmm_is_new_pat_value_consistent(uint64_t pat_value);


/*-------------------------------------------------------------------------
 * Function: hmm_unmap_hpa
 *  Description: This function is used in order to unmap HVA -> HPA references
 *               to physical address
 *  Input: hpa - host physical address - must be aligned on page
 *         size - size in bytes, but must be alinged on page size (4K, 8K, ...)
 *         flush_tlbs_on_all_cpus - TRUE in case when flush TLB on all cpus is
 *                                  required
 *  Return Value: TRUE in case the unmap was successful
 *                FALSE in case the operation failed. In this case the state of
 *                inner mappings is undefined.
 *------------------------------------------------------------------------- */
boolean_t hmm_unmap_hpa(IN hpa_t hpa,
			uint64_t size,
			boolean_t flush_tlbs_on_all_cpus);


/*-------------------------------------------------------------------------
 * Function: hmm_get_hpa_type
 *  Description: returns the memory type of physical address (MTRR type)
 *  Input: hpa - host physical address
 *  Return Value: Memory type
 *------------------------------------------------------------------------- */
mon_phys_mem_type_t hmm_get_hpa_type(IN hpa_t hpa);


/*-------------------------------------------------------------------------
 * Function: hmm_does_memory_range_have_specified_memory_type
 *  Description: Checks whether the given physical address range has given
 *               memory type
 *  Input: hpa - host physical address
 *         size - size of the range,
 *         mem_type - expected memory type.
 *  Return Value: TRUE in case the memory range has given memory type
 *                FALSE otherwise
 *------------------------------------------------------------------------- */
boolean_t
hmm_does_memory_range_have_specified_memory_type(IN hpa_t start_hpa,
						 IN uint64_t size,
						 mon_phys_mem_type_t mem_type);


/*-------------------------------------------------------------------------
 * Function: hmm_set_required_values_to_control_registers
 *  Description: Sets required bits to CRs and EFER and must be called
 *               on all cpus after HMM initialization
 *               Currently the functions sets WP in CR0 and NXE in EFER
 *------------------------------------------------------------------------- */
void hmm_set_required_values_to_control_registers(void);

/*-------------------------------------------------------------------------
 * Function: hmm_map_physical_page
 *  Description: Maps "uncachable" physical page to MON page tables
 *  Ret. value: TRUE in case of success. FALSE in case of insufficient memory
 *              In case of failure the state of internal mappings may be
 *              inconsistent.
 *------------------------------------------------------------------------- */
boolean_t hmm_map_uc_physical_page(IN hpa_t page_hpa,
				   IN boolean_t is_writable,
				   IN boolean_t is_executable,
				   IN boolean_t flash_all_tlbs_if_needed,
				   OUT hva_t *page_hva);

/*-------------------------------------------------------------------------
 * Function: hmm_alloc_additional_continuous_virtual_buffer
 *  Description: Maps additional temporary virtual buffer to existing physical
 *               pages, the buffer can be unmapped by using
 *               "hmm_free_continuous_virtual_buffer" function.
 *               Note, that the existing HPA <-> HVA mapping for the given
 *               tables are not updated,
 *               just new mapping of HVA->HPA is added to virtual tables of HMM
 *  Arguments:
 *             current_hva - currently mapped HVA
 *             additional_hva - additional address of virtual buffer
 *             num_of_pages - size of the buffer in pages
 *             is_writable - set buffer writable
 *             is_executable - set buffer executable
 *             pat_index - pat index
 *  Ret. value: TRUE in case of success. FALSE in case when mapping is
 *              impossible.
 *  Remark: In case of insufficient memory internal DEADLOOP will occur.
 *------------------------------------------------------------------------- */
boolean_t hmm_alloc_additional_continuous_virtual_buffer(
	IN uint64_t current_hva,
	IN uint64_t additional_hva,
	IN uint32_t num_of_pages,
	IN boolean_t is_writable,
	IN boolean_t is_executable,
	IN uint32_t pat_index);

/*-------------------------------------------------------------------------
 * Function: hmm_free_continuous_virtual_buffer
 *  Description: Removes mapping of TEMPORARY virtual buffer.
 *  Ret. value: TRUE in case of success. FALSE in case of insufficient memory
 *              In case of failure the state of internal mappings may be
 *              inconsistent.
 *------------------------------------------------------------------------- */
boolean_t hmm_free_continuous_virtual_buffer(uint64_t buffer_hva,
					     uint32_t num_of_pages);


/*-------------------------------------------------------------------------
 * Function: hmm_make_phys_page_uncachable
 *  Description: Updates MON page tables, so that access to given physical page
 *               will be UC (Uncachable).
 * Ret. value: TRUE in case of success. FALSE in case of insufficient memory
 *              In case of failure the state of internal mappings may be
 *              inconsistent.
 *------------------------------------------------------------------------- */
boolean_t hmm_make_phys_page_uncachable(uint64_t page_hpa);

/*-------------------------------------------------------------------------
 * Function: hmm_remap_virtual_memory
 *  Description: Remaps virtual memory buffer with newly given attributes.
 *  Arguments:
 *             from_hva - currently mapped virtual address
 *             to_hva - new virtual address
 *             size - size of the buffer
 *             is_writable - set buffer "writable"
 *             is_executable - set buffer "executable"
 *             pat_index - pat index
 *             flash_tlbs - flash all hardware TLBs
 *  Ret. value: TRUE in case of success. FALSE in case when mapping is
 *              impossible.
 *  Remark: In case of insufficient memory internal DEADLOOP will occur.
 *------------------------------------------------------------------------- */
boolean_t hmm_remap_virtual_memory(hva_t from_hva,
				   hva_t to_hva,
				   uint32_t size,
				   boolean_t is_writable,
				   boolean_t is_executable,
				   uint32_t pat_index,
				   boolean_t flash_tlbs);


/*-------------------------------------------------------------------------
 * Function: hmm_remap_physical_pages_to_continuous_wb_virtal_addr
 *  Description: Remaps given physical pages to new continuous virtual buffer
 *               with HPA->HVA mapping udpate.
 *               The pages are mapped with WB caching attributes
 *  Arguments:
 *             hpas_array - array of HPAs
 *             num_of_pages - number of array elements
 *             is_writable - writable permission
 *             is_executable - executable permission
 *  Output:
 *             hva - HVA of virtual buffer
 *  Ret. value: TRUE in case of success. FALSE in case when mapping is
 *              impossible.
 *  Remark: In case of insufficient memory internal ASSERT will occur.
 *------------------------------------------------------------------------- */
boolean_t hmm_remap_physical_pages_to_continuous_wb_virtal_addr(
	IN uint64_t *hpas_array,
	IN uint32_t num_of_pages,
	IN boolean_t is_writable,
	IN boolean_t is_executable,
	OUT uint64_t *hva);
#endif
