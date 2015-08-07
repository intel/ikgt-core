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

#ifndef FLAT_PAGE_TABLES_H
#define FLAT_PAGE_TABLES_H

#include <mon_defs.h>

typedef void *fpt_flat_page_tables_handle_t;
#define FPT_INVALID_HANDLE ((fpt_flat_page_tables_handle_t)NULL)

/*--------------------------------------------------------------------------
 * Function: fpt_create_32_bit_flat_page_tables
 * Description: This function is used in order to create 32 bit PAE flat
 *              page tables for guest according to information recording in
 *              GPM with full write/user/execute permissions and
 *              WB caching.
 *              The following bits must be set in the control registers:
 *              CR4.PAE, CR4.PSE, CR0.PG * * Input: * gpm_handle - handle
 *              received from "gpm_create_mapping"
 * Output:      flat_page_table_handle - handle with which
 *              the destroying of flat tables will be possible
 *              pdpt - host physical address of created flat page tables
 * Return Value: TRUE when creation is successful
 *               FALSE when creation has failed
 *--------------------------------------------------------------------------*/
boolean_t fpt_create_32_bit_flat_page_tables(IN guest_cpu_handle_t gcpu,
					     OUT fpt_flat_page_tables_handle_t *
					     flat_page_tables_handle,
					     OUT uint32_t *pdpt);
/*--------------------------------------------------------------------------
 * Function: fpt_create_32_bit_flat_page_tables_under_4G
 * Description: This function is used to create page tables for at most
 *              highest_address or 4G and the memory which holds those page
 *              tables is located under 4G physical RAM.
 *--------------------------------------------------------------------------*/
boolean_t fpt_create_32_bit_flat_page_tables_under_4G(
	IN uint64_t highest_address);

/*--------------------------------------------------------------------------
 * Function: fpt_create_64_bit_flat_page_tables
 * Description: This function is used in order to create 64 bit flat page
 *              tables for guest according to information recording in GPM
 *              with full write/user/execute permissions and WB caching.
 *              The following bits must be set in the control registers:
 *              CR4.PAE, EFER.LME, CR0.PG
 * Input: gpm_handle - handle received from "gpm_create_mapping"
 * Output: flat_page_table_handle - handle with which the destroying of flat
 *         tables will be possible
 *         pml4t - host physical address of created flat page tables
 * Return Value: TRUE when creation is successful
 *               FALSE when creation has failed
 *--------------------------------------------------------------------------*/
boolean_t fpt_create_64_bit_flat_page_tables(IN guest_cpu_handle_t gcpu,
					     OUT fpt_flat_page_tables_handle_t *
					     flat_page_tables_handle,
					     OUT uint64_t *pml4t);

/*--------------------------------------------------------------------------
 * Function: fpt_destroy_flat_page_tables
 * Description: This function is used in order to destroy created flat page
 *              tables by one of the functions
 *              "fpt_create_32_bit_flat_page_tables" or
 *              "fpt_create_64_bit_flat_page_tables"
 * Input: flat_page_table_handle - handle received upon creation
 * Return Value: TRUE when operation is successful
 *               FALSE when operation has failed
 *--------------------------------------------------------------------------*/
boolean_t fpt_destroy_flat_page_tables(IN fpt_flat_page_tables_handle_t
				       flat_page_tables_handle);


typedef uint64_t fpt_ranges_iterator_t;
#define FPT_INVALID_ITERAROR (~((uint64_t)0))

/*--------------------------------------------------------------------------
 * Function: fpt_iterator_get_range
 * Description: This function returns the information about range from
 *              iterator.
 * Input: flat_page_tables_handle - handle returned upon creation;
 *        iter - iterator
 * Output: src_addr - start address of the range
 *         size - size of the range
 * Return value: - TRUE in case the query is successful,
 *                 FALSE otherwise
 *--------------------------------------------------------------------------*/
boolean_t fpt_iterator_get_range(IN fpt_flat_page_tables_handle_t
				 flat_page_tables_handle,
				 IN fpt_ranges_iterator_t iter,
				 OUT uint64_t *src_addr,
				 OUT uint64_t *size);

/*--------------------------------------------------------------------------
 * Function: fpt_iterator_get_next
 * Description: Advances the iterator to next range.
 * Input: flat_page_tables_handle - handle returned upon creation;
 *        iter - iterator
 * Return value: - new iterator. In case there is no more
 *                 ranges, FPT_INVALID_ITERAROR is returned
 *--------------------------------------------------------------------------*/
fpt_ranges_iterator_t fpt_iterator_get_next(IN fpt_flat_page_tables_handle_t
					    flat_page_tables_handle,
					    IN fpt_ranges_iterator_t iter);

#endif
