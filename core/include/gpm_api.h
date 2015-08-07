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

#ifndef GPM_API_H
#define GPM_API_H

#include <mon_defs.h>
#include <common_libc.h>
#include <e820_abstraction.h>
#include <mon_objects.h>
#include <memory_address_mapper_api.h>

typedef void *gpm_vm_handle_t;
typedef void *gpm_vm_cpu_handle_t;

#define GPM_INVALID_HANDLE ((gpm_handle_t)NULL)

typedef uint64_t gpm_ranges_iterator_t;
#define GPM_INVALID_RANGES_ITERATOR (~((uint64_t)0x0))

/*--------------------------------------------------------------------------
 * Function: gpm_create_mapping
 * Description: This function should be called in order to create new
 *              GPA -> HPA mapping. Afterwards the mapping may be filled by
 *              using functions "gpm_add_mapping" and "gpm_remove_mapping".
 * Return Value: Handle which will be used in all other
 *               functions. In case of failure, GPM_INVALID_HANDLE will be
 *               returned;
 *--------------------------------------------------------------------------*/
gpm_handle_t mon_gpm_create_mapping(void);

/*--------------------------------------------------------------------------
 * Function: gpm_add_mapping
 * Description: This function should be called in order to create add new
 *              GPA -> HPA mapping. If the mapping intersects with already
 *              existing one, the old data will be overwritten with new one
 * Input: gpm_handle - handle received from "gpm_create_mapping"
 *        gpa - guest physical address to map (must be aligned on page)
 *        hpa - target host physical address (must be aligned on page)
 *        size - size of the inserted range (must be aligned on page)
 * Return Value: TRUE in case of success
 *               FALSE in case of failure. In this case the state of the
 *               remainging mapping is undefined;
 *--------------------------------------------------------------------------*/
boolean_t mon_gpm_add_mapping(IN gpm_handle_t gpm_handle,
			      IN gpa_t gpa,
			      IN hpa_t hpa,
			      IN uint64_t size,
			      mam_attributes_t attrs);

/*--------------------------------------------------------------------------
 * Function: gpm_remove_mapping
 * Description: This function should be called in order to remove
 *              GPA -> HPA mapping. If the mapping doesn't exist nothing
 *              wrong will occur.
 * Input: gpm_handle - handle received from "gpm_create_mapping"
 *        gpa - guest physical address (must be aligned on page)
 *        size - size of the removed range (must be aligned on page)
 * Return Value: TRUE in case of success
 *               FALSE in case of failure. In this case the state of the
 *               remainging mapping is undefined;
 *--------------------------------------------------------------------------*/
boolean_t mon_gpm_remove_mapping(IN gpm_handle_t gpm_handle,
				 IN gpa_t gpa,
				 IN uint64_t size);

/*--------------------------------------------------------------------------
 * Function: gpm_add_mmio_range
 * Description: This function should be called in order to insert MMIO range
 *              to mapping.
 * Input: gpm_handle - handle received from "gpm_create_mapping"
 *        gpa - guest physical address to map (must be aligned on page)
 *        size - size of the inserted range (must be aligned on page)
 * Return Value: TRUE in case of success
 *               FALSE in case of failure. In this case the state of the
 *               remainging mapping is undefined;
 *--------------------------------------------------------------------------*/
boolean_t gpm_add_mmio_range(IN gpm_handle_t gpm_handle,
			     IN gpa_t gpa,
			     IN uint64_t size);

/*--------------------------------------------------------------------------
 * Function: gpm_is_mmio_address
 * Description: This function gives an information whether given address is
 * in MMIO range.
 * Input: gpm_handle - handle received from "gpm_create_mapping"
 *        gpa - guest physical address to map (must be aligned on page)
 * Return Value: TRUE or FALSE
 *--------------------------------------------------------------------------*/
boolean_t mon_gpm_is_mmio_address(IN gpm_handle_t gpm_handle, IN gpa_t gpa);

/*--------------------------------------------------------------------------
 * Function: gpm_gpa_to_hpa
 * Description: This function should be called in order to retrieve
 *              information about GPA -> HPA mapping.
 * Input: gpm_handle - handle received from "gpm_create_mapping"
 *        gpa - guest physical address
 * Output: hpa - host physical address
 * Return Value: TRUE when mapping exists
 *               FALSE when mapping doesn't exist
 *--------------------------------------------------------------------------*/
boolean_t mon_gpm_gpa_to_hpa(IN gpm_handle_t gpm_handle,
			     IN gpa_t gpa,
			     OUT hpa_t *hpa,
			     OUT mam_attributes_t *hpa_attrs);

/*--------------------------------------------------------------------------
 * Function: gpm_gpa_to_hva
 * Description: This function should be called as a shortcut * to GPA -> HPA,
 *              HPA -> HVA mapping.
 * Input: gpm_handle - handle received from "gpm_create_mapping"
 *        gpa - guest physical address
 * Output: hva - host virtual address
 * Return Value: TRUE when mapping exists
 *               FALSE when mapping doesn't exist
 *--------------------------------------------------------------------------*/
boolean_t gpm_gpa_to_hva(IN gpm_handle_t gpm_handle,
			 IN gpa_t gpa,
			 OUT hva_t *hva);

/*--------------------------------------------------------------------------
 * Function: gpm_hpa_to_gpa
 * Description: This function should be called in order to retrieve
 *              information about HPA -> GPA mapping.
 * Input: gpm_handle - handle received from "gpm_create_mapping"
 *        hpa - host physical address
 * Output: gpa - guest physical address
 * Return Value: TRUE when mapping exists
 *               FALSE when mapping doesn't exist
 *--------------------------------------------------------------------------*/
boolean_t mon_gpm_hpa_to_gpa(IN gpm_handle_t gpm_handle,
			     IN hpa_t hpa,
			     OUT gpa_t *gpa);

/*--------------------------------------------------------------------------
 * Function: gpm_create_e820_map
 * Description: This function is used in order to create e820 memory map as
 *               merge between original e820 map and existing GPA -> HPA
 *               mappings.
 * Input: gpm_handle - handle received upon creation
 * Output: e820_handle - e820 handle. It may be used as parameter for
 *                       e820_abstraction module.
 * Return Value: TRUE when operation is successful
 *               FALSE when operation has failed
 *--------------------------------------------------------------------------*/
boolean_t gpm_create_e820_map(IN gpm_handle_t gpm_handle,
			      OUT e820_handle_t *e820_handle);

/*--------------------------------------------------------------------------
 * Function: gpm_destroy_e820_map
 * Description: This function is used in order to destroy e820 mapping
 *              created by "gpm_create_e820_map" function
 * Input: e820_handle - handle received from "gpm_create_e820_map" function
 *--------------------------------------------------------------------------*/
void gpm_destroy_e820_map(IN e820_handle_t e820_handle);

/*--------------------------------------------------------------------------
 * Function: gpm_get_ranges_iterator
 * Description: This function returns the iterator, using which it is possible
 *              to iterate over existing mappings.
 * Input: mam_handle - handle created by "gpm_create_mapping";
 * Return value: Iterator value. NULL iterator has value:
 *               GPM_INVALID_RANGES_ITERATOR
 *--------------------------------------------------------------------------*/
gpm_ranges_iterator_t gpm_get_ranges_iterator(IN gpm_handle_t gpm_handle);

/*--------------------------------------------------------------------------
 * Function: gpm_get_range_details_from_iterator
 * Description: Use this function in order to retrieve the details of memory
 *              range pointed by iterator.
 * Input: gpm_handle - handle created by "gpm_create_mapping";
 *        iter - iterator value
 * Output: src_addr - source address of the existing mapping range. In case
 *                    the "iter" has GPM_INVALID_RANGES_ITERATOR value,
 *                    src_addr will * have 0xfffffffffffffff value.
 *         size - size of the range. In case the "iter" has
 *                GPM_INVALID_RANGES_ITERATOR * value, size will be 0.
 * Ret value: - next iterator. Note that this is the only way to get next
 *              iterator value.
 *--------------------------------------------------------------------------*/
gpm_ranges_iterator_t gpm_get_range_details_from_iterator(IN gpm_handle_t
							  gpm_handle,
							  IN gpm_ranges_iterator_t
							  iter,
							  OUT gpa_t *gpa,
							  OUT uint64_t *size);

void gpm_print(gpm_handle_t gpm_handle);

boolean_t mon_gpm_copy(gpm_handle_t src,
		       gpm_handle_t dst,
		       boolean_t override_attrs,
		       mam_attributes_t set_attrs);

#endif
