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
/*
 *
 * API description of Memory Address Mapper
 *
 */

#ifndef MEMORY_ADDRESS_MAPPER_API_H
#define MEMORY_ADDRESS_MAPPER_API_H

#include <mon_defs.h>


typedef void *mam_handle_t;
#define MAM_INVALID_HANDLE ((mam_handle_t)NULL)

typedef uint64_t mam_memory_ranges_iterator_t;
#define MAM_INVALID_MEMORY_RANGES_ITERATOR (~((uint64_t)0x0))

typedef union {
	uint32_t uint32;

	struct {
		uint32_t writable:1;
		uint32_t user:1;
		uint32_t executable:1;
		uint32_t global:1;
		uint32_t pat_index:3;
		uint32_t unused:1;
		uint32_t noaccess:1;
		uint32_t reserved:23; /* must be zero */
	} paging_attr;

	struct {
		uint32_t readable:1;
		uint32_t writable:1;
		uint32_t executable:1;
		uint32_t igmt:1;
		uint32_t emt:3;
		uint32_t suppress_ve:1;
		uint32_t reserved:24; /* must be zero */
	} ept_attr;

	struct {
		uint32_t readable:1;
		uint32_t writable:1;
		uint32_t snoop:1;
		uint32_t tm:1;
		uint32_t reserved:28; /* must be zero */
	} vtdpt_attr;
} mam_attributes_t;

extern const mam_attributes_t mam_no_attributes;
extern mam_attributes_t mam_rwx_attrs;
extern mam_attributes_t mam_rw_attrs;
extern mam_attributes_t mam_ro_attrs;

#define EPT_NO_PERM 0x0 /* no rwx */
#define EPT_WO_PERM 0x2 /* write only no rx */
#define EPT_XO_PERM 0x4 /* exec no rw */
#define EPT_WX_PERM 0x6 /* write exec no r */

#define MAM_NO_ATTRIBUTES mam_no_attributes

typedef uint32_t mam_mapping_result_t;
#define MAM_MAPPING_SUCCESSFUL   ((mam_mapping_result_t)0x0)
#define MAM_UNKNOWN_MAPPING ((mam_mapping_result_t)0x7fffffff)


/*-------------------------------------------------------------------------
 * Function: mam_create_mapping
 *  Description: This function should be called in order to
 *               create new mapping.
 *               Afterwards the mapping may be filled by using functions
 *               "mam_insert_range" and "mam_insert_not_existing_range".
 *  Input: inner_level_attributes - attributes for inner levels (relevant for
 *                                  hardware compliant mappings)
 *                                  If attributes are not relevant, use
 *                                  MAM_NO_ATTRIBUTES
 *                                  as the value
 *  Output: Handle which will be used in all other functions.
 *------------------------------------------------------------------------- */
mam_handle_t mam_create_mapping(mam_attributes_t inner_level_attributes);


/*-------------------------------------------------------------------------
 * Function: mon_mam_destroy_mapping
 *  Description: Destroys all data structures relevant to particular
 *               mapping.
 *  Input: handle created by "mam_create_mapping".
 *------------------------------------------------------------------------- */
void mon_mam_destroy_mapping(IN mam_handle_t mam_handle);

/*-------------------------------------------------------------------------
 * Function: mam_clone
 *  Description: Clone the existing mapping.
 *  Input: handle created by "mam_create_mapping".
 *------------------------------------------------------------------------- */
mam_handle_t mam_clone(IN mam_handle_t mam_handle);

/*-------------------------------------------------------------------------
 * Function: mam_get_mapping
 *  Description: return the information inserted by "mam_insert_range" and
 *               "mam_insert_not_existing_range" functions.
 *  Input: mam_handle - handle created by "mam_create_mapping";
 *         src_addr   - source address
 *  Return Value:
 *                - MAM_MAPPING_SUCCESSFUL in this case there are additional
 *                  output parameters: tgt_addr and attrs
 *                - MAM_UNKNOWN_MAPPING in case when query is performed and
 *                  address
 *                  that was never inserted neither by "mam_insert_range" nor by
 *                  "mam_insert_not_existing_range" functions.
 *                - other value of type mam_mapping_result_t in case when query
 *                  is performed for address which was updated by
 *                  "mam_insert_not_existing_range" before with this value as
 *                  "reason" parameter.
 *  Output:
 *          - tgt_addr - mapped address in case when "Return Value" is
 *                       MAM_MAPPING_SUCCESSFUL
 *          - attrs    - attributes of mapped address when "Return Value"
 *                       is MAM_MAPPING_SUCCESSFUL
 *------------------------------------------------------------------------- */
mam_mapping_result_t mam_get_mapping(IN mam_handle_t mam_handle,
				     IN uint64_t src_addr,
				     OUT uint64_t *tgt_addr,
				     OUT mam_attributes_t *attrs);


/*-------------------------------------------------------------------------
 * Function: mam_insert_range
 *  Description: Inserts new mapping into the data structures. It is
 *               possible to overwrite existing mappings. For example,
 *               create 4G of identical mapping:
 *                           mam_insert_range(handle, 0, 0, 4G, attrs)
 *               then overwrite some range in the middle:
 *                           mam_insert_range(handle, src, tgt, 1M, other_attrs)
 *  Input: mam_handle  - handle created by "mam_create_mapping";
 *         src_addr    - source address
 *         tgt_addr    - target address
 *         size        - size of the range
 *         attrs       - attributes
 *  Return value: - TRUE in case of success, the query done by
 *                  "mam_get_mapping" function on any src_addr inside the
 *                  mapped range will be successful and return
 *                  value will be MAM_MAPPING_SUCCESSFUL.
 *                - FALSE when there was not enough memory to allocate for
 *                  internal data structures, hence the mapping wasn't
 *                  successful. In this case the internal
 *                  mapping may be partial, i.e. only part of the requested
 *                  range will be mapped, and the remaining part will remain
 *                  with previous information.
 *------------------------------------------------------------------------- */
boolean_t mam_insert_range(IN mam_handle_t mam_handle,
			   IN uint64_t src_addr,
			   IN uint64_t tgt_addr,
			   IN uint64_t size,
			   IN mam_attributes_t attrs);


/*-------------------------------------------------------------------------
 * Function: mam_insert_not_existing_range
 *  Description: Inserts new information (reason) or overwrites existing
 *               one: why the current range is unmapped. It is possible
 *               to overwrite the existing information in a similar way
 *               as with "mam_insert_range" function.
 *  Input: mam_handle - handle created by "mam_create_mapping";
 *         src_addr   - source address
 *         size       - size of the range
 *         reason     - this value will be returned by "mam_get_mapping"
 *                      function on any query inside the range. This value
 *                      should be defined by client and it mustn't be equal to
 *                      one of the predefined values: "MAM_MAPPING_SUCCESSFUL"
 *                      and "MAM_UNKNOWN_MAPPING"
 *  Return value: - TRUE in case of success, the query done by
 *                  "mam_get_mapping" function on any src_addr inside the
 *                  mapped range will return the "reason".
 *                - FALSE when there was not enough memory to allocate for
 *                  internal data structures, hence the mapping wasn't
 *                  successful. In this case the internal
 *                  mapping may be partial, i.e. only part of the requested
 *                  range will be mapped, and the remaining part will remain
 *                  with previous information.
 *------------------------------------------------------------------------- */
boolean_t mam_insert_not_existing_range(IN mam_handle_t mam_handle,
					IN uint64_t src_addr,
					IN uint64_t size,
					IN mam_mapping_result_t reason);


/*-------------------------------------------------------------------------
 * Function: mam_add_permissions_to_existing_mapping
 *  Description: This function enables adding permissions to already existing
 *               ones. If mapping doesn't exist nothing will happen.
 *               Note that "pat_index" for page tables attributes and "emt"
 *               for ept attributes should be "0" in added attributes.
 *               Otherwise the updated pat_index or "emt" will be updated in
 *               a way that is not expected by the caller.
 *  Input: mam_handle  - handle created by "mam_create_mapping";
 *         src_addr    - source address
 *         size        -
 *         attrs       - attributes to add.
 *  Return value: - TRUE in this case of success. In this case there is
 *                  additional output parameter
 *                - FALSE in case of insufficient memory.
 *------------------------------------------------------------------------- */
boolean_t mam_add_permissions_to_existing_mapping(IN mam_handle_t mam_handle,
						  IN uint64_t src_addr,
						  IN uint64_t size,
						  IN mam_attributes_t attrs);


/*-------------------------------------------------------------------------
 * Function: mam_remove_permissions_from_existing_mapping
 *  Description: This function enables removing permissions to already existing
 *               mappings. If mapping doesn't exist nothing will happen.
 *               Note that "pat_index" for page tables attributes and "emt" for
 *               ept attributes should be "0" in removed attributes. Otherwise
 *               the updated pat_index or "emt" will be updated in a way
 *               that is not expected by the caller.
 *  Input: nam_handle  - handle created by "mam_create_mapping";
 *         src_addr    - source address
 *         size        -
 *         attrs       - attributes to remove.
 *  Return value: - TRUE in this case of success. In this case there is
 *                  additional output parameter
 *                  first_table_out - Host Physical Address of first table
 *                  (PML4T).
 *                - FALSE in case of insufficient memory.
 *------------------------------------------------------------------------- */
boolean_t mam_remove_permissions_from_existing_mapping(
	IN mam_handle_t mam_handle,
	IN uint64_t src_addr,
	IN uint64_t size,
	IN mam_attributes_t attrs);

/*-------------------------------------------------------------------------
 * Function: mam_convert_to_64bit_page_tables
 *  Description: This functions converts internal optimized mapping to 64 bits
 *               page Tables.
 *               From now on there is no way back to optimized mapping.
 *               All the operations will be performed on unoptimized mapping.
 *  Input: mam_handle - handle created by "mam_create_mapping";
 *  Return value: - TRUE in this case of success. In this case there is
 *                  additional output parameter
 *                  first_table_out - Host Physical Address of first table
 *                  (PML4T).
 *                - FALSE in case of insufficient memory.
 *------------------------------------------------------------------------- */
boolean_t mam_convert_to_64bit_page_tables(IN mam_handle_t mam_handle,
					   OUT uint64_t *pml4t_hpa);

/*-------------------------------------------------------------------------
 * Function: mam_convert_to_32bit_pae_page_tables
 *  Description: This functions converts internal optimized mapping to 32 bit
 *               PAE page tables.
 *               From now on there is no way back to optimized mapping.
 *               All the operations will be performed on unoptimized mapping.
 *  Input: mam_handle - handle created by "gam_create_mapping";
 *  Return value: - TRUE in this case of success. In this case there is
 *                  additional output parameter
 *                  first_table_out - Host Physical Address of first table
 *                  (PDPT).
 *                - FALSE in case of insufficient memory.
 *------------------------------------------------------------------------- */
boolean_t mam_convert_to_32bit_pae_page_tables(IN mam_handle_t mam_handle,
					       OUT uint32_t *pdpt_hpa);

/*-------------------------------------------------------------------------
 * Function: mam_get_memory_ranges_iterator
 *  Description: This function returns the iterator, using which it is possible
 *               to iterate
 *               over existing mappings.
 *  Input: mam_handle  - handle created by "mam_create_mapping";
 *  Ret value: - Iterator value. NULL iterator has value:
 *               MAM_INVALID_MEMORY_RANGES_ITERATOR
 *------------------------------------------------------------------------- */
mam_memory_ranges_iterator_t
mam_get_memory_ranges_iterator(IN mam_handle_t mam_handle);


/*-------------------------------------------------------------------------
 * Function: mam_get_range_details_from_iterator
 *  Description: Use this function in order to retrieve the details of memory
 *               range pointed by iterator.
 *  Input: mam_handle  - handle created by "mam_create_mapping";
 *         iter - iterator value
 *  Output: src_addr - source address of the existing mapping range. In case
 *                     the "iter" has MAM_INVALID_MEMORY_RANGES_ITERATOR value,
 *                     src_addr will
 *                     have 0xfffffffffffffff value.
 *          size     - size of the range. In case the "iter" has
 *                     MAM_INVALID_MEMORY_RANGES_ITERATOR
 *                     value, size will be 0.
 *  Ret value: - new iterator.
 *------------------------------------------------------------------------- */
mam_memory_ranges_iterator_t
mam_get_range_details_from_iterator(IN mam_handle_t mam_handle,
				    IN mam_memory_ranges_iterator_t iter,
				    OUT uint64_t *src_addr,
				    OUT uint64_t *size);

/*-------------------------------------------------------------------------
 * Function: mam_get_range_start_address_from_iterator
 *  Description: Use this function in order to retrieve the source address of
 *               the range pointed by iterator.
 *  Input: mam_handle  - handle created by "mam_create_mapping";
 *         iter - iterator value
 *  Ret value: - address. In case when iterator has value
 *               MAM_INVALID_MEMORY_RANGES_ITERATOR, the 0xffffffffffffffff is
 *               returned.
 *------------------------------------------------------------------------- */
uint64_t
mam_get_range_start_address_from_iterator(IN mam_handle_t mam_handle,
					  IN mam_memory_ranges_iterator_t iter);

typedef uint32_t mam_ept_super_page_support_t;
#define MAM_EPT_NO_SUPER_PAGE_SUPPORT 0x0
#define MAM_EPT_SUPPORT_2MB_PAGE 0x1
#define MAM_EPT_SUPPORT_1GB_PAGE 0x2
#define MAM_EPT_SUPPORT_512_GB_PAGE 0x4

typedef enum {
	MAM_EPT_21_BITS_GAW = 0,
	MAM_EPT_30_BITS_GAW,
	MAM_EPT_39_BITS_GAW,
	MAM_EPT_48_BITS_GAW
} mam_ept_supported_gaw_t;

/*-------------------------------------------------------------------------
 * Function: mon_mam_convert_to_ept
 *  Description: This functions converts internal optimized mapping to ept.
 *               From now on there is no way back to optimized mapping.
 *               All the operations will be performed on unoptimized mapping.
 *  Input: mam_handle - handle created by "mam_create_mapping";
 *         ept_super_page_support - information of which super page can be
 *                                  supported by hardware (bitmask)
 *         ept_supported_gaw - Information of how many internal level should
 *                             there be.
 *                             Note that this information should be compliant
 *                             with one which will be recorded in EPTR!!!
 *         ept_hw_ve_support - Hardware #VE support flag
 *  Return value: - TRUE in this case of success. In this case there is
 *                  additional output parameter
 *                  first_table_out - Host Physical Address of first table.
 *                - FALSE in case of insufficient memory.
 *------------------------------------------------------------------------- */
boolean_t mon_mam_convert_to_ept(IN mam_handle_t mam_handle,
				 IN mam_ept_super_page_support_t ept_super_page_support,
				 IN mam_ept_supported_gaw_t ept_supported_gaw,
				 IN boolean_t ept_hw_ve_support,
				 OUT uint64_t *first_table_hpa);

typedef uint8_t mam_vtdpt_super_page_support_t;
#define MAM_VTDPT_SUPPORT_2MB_PAGE 0x1
#define MAM_VTDPT_SUPPORT_1GB_PAGE 0x2
#define MAM_VTDPT_SUPPORT_512_GB_PAGE 0x4

typedef uint8_t mam_vtdpt_snoop_behavior_t;
typedef uint8_t mam_vtdpt_trans_mapping_t;

typedef enum {
	MAM_VTDPT_21_BITS_GAW = 0,
	MAM_VTDPT_30_BITS_GAW,
	MAM_VTDPT_39_BITS_GAW,
	MAM_VTDPT_48_BITS_GAW
} mam_vtdpt_supported_gaw_t;

/*-------------------------------------------------------------------------
 * Function: mam_convert_to_vtdpt
 *  Description: This functions converts internal optimized mapping to VT-d
 *               page table.
 *               From now on there is no way back to optimized mapping.
 *               All the operations will be performed on unoptimized mapping.
 *  Input: mam_handle - handle created by "mam_create_mapping";
 *         vtdpt_super_page_support - information of which super page can be
 *                                    supported by hardware (bitmask)
 *         vtdpt_snoop_behavior - snoop behavior supported by the hardware,
 *                                treated as reserved:
 *                                1. Always in non-leaf entries
 *                                2. In leaf entries, as hardware
 *                                 implementations reporting SC (Snoop Control)
 *                                  as clear in the extended capability register
 *         vtdpt_trans_mapping - transient mapping supported by hardware,
 *                               treated as reserved:
 *                               1. Always in non-leaf entries
 *                               2. In leaf entries, as hardware implementations
 *                                  reporting CH (Caching Hints)
 *                                  and Device-IOTLB (DI) fields as clear in
 *                                  the extended capability register
 *         vtdpt_supported_gaw - Information of how many internal level
 *                               supported by the hardware
 *  Return value: - TRUE in this case of success. In this case there is
 *                  additional output parameter
 *                  first_table_out - Host Physical Address of first table.
 *                - FALSE in case of insufficient memory.
 *------------------------------------------------------------------------- */
boolean_t mam_convert_to_vtdpt(IN mam_handle_t mam_handle,
			       IN mam_vtdpt_super_page_support_t vtdpt_super_page_support,
			       IN mam_vtdpt_snoop_behavior_t vtdpt_snoop_behavior,
			       IN mam_vtdpt_trans_mapping_t vtdpt_trans_mapping,
			       IN uint32_t sagaw_index_bit,
			       OUT uint64_t *first_table_hpa);

void mam_print_page_usage(IN mam_handle_t mam_handle);

#endif
