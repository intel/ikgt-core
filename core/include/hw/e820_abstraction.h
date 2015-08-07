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

#ifndef E820_ABSTRACTION_H
#define E820_ABSTRACTION_H

#include <mon_arch_defs.h>
#include <mon_objects.h>

typedef void *e820_abstraction_range_iterator_t;
#define E820_ABSTRACTION_NULL_ITERATOR ((e820_abstraction_range_iterator_t)NULL)

typedef void *e820_handle_t;
#define E820_ORIGINAL_MAP ((e820_handle_t)NULL)


/*--------------------------------------------------------------------------
 * Function: e820_abstraction_initialize
 *  Description: This function will copy the e820_memory_map to internal data
 *               structures.
 *  Return Value: TRUE in case the initialization is successful.
 *--------------------------------------------------------------------------*/
boolean_t e820_abstraction_initialize(IN const int15_e820_memory_map_t *
				      e820_memory_map,
				      IN uint32_t int15_handler_address);


/*--------------------------------------------------------------------------
 * Function: e820_abstraction_is_initialized
 *  Description: Using this function the other components can receive the
 *               information, whether the component was successfully
 *               initialized.
 *  Return Value: TRUE in case the component was initialized.
 *--------------------------------------------------------------------------*/
boolean_t e820_abstraction_is_initialized(void);


/*--------------------------------------------------------------------------
 * Function: e820_abstraction_get_map
 *  Description: The function returns the e820. This function may be called if
 *               work with iterator is inconvenient.
 *  Input:
 *        e820_handle - handle returned by "e820_abstraction_create_new_map"
 *                      or E820_ORIGINAL_MAP if iteration over original map is
 *                      required
 *  Return Value: Pointer to e820 map
 *--------------------------------------------------------------------------*/
const int15_e820_memory_map_t *e820_abstraction_get_map(
	e820_handle_t e820_handle);


/*--------------------------------------------------------------------------
 * Function: e820_abstraction_iterator_get_first
 *  Description: The function returns the iterator for existing memory ranges.
 *               It is possible to retrieve information of all existing ranges
 *               using this iterator.
 *  Input:
 *        e820_handle - handle returned by "e820_abstraction_create_new_map"
 *                      or E820_ORIGINAL_MAP if iteration over original map is
 *                      required
 *  Return Value: Iterator. Null iterator is: E820_ABSTRACTION_NULL_ITERATOR.
 *--------------------------------------------------------------------------*/
e820_abstraction_range_iterator_t
e820_abstraction_iterator_get_first(e820_handle_t e820_handle);

/*--------------------------------------------------------------------------
 * Function: e820_abstraction_iterator_get_next
 *  Description: The function moves the iterator to the next range.
 *  Input:
 *         e820_handle - handle returned by "e820_abstraction_create_new_map"
 *                       or E820_ORIGINAL_MAP if iteration over original map is
 *                       required
 *         iter - current iterator.
 *  Return Value: new value of iterator. In case there is no next element the
 *                E820_ABSTRACTION_NULL_ITERATOR value will be returned.
 *--------------------------------------------------------------------------*/
e820_abstraction_range_iterator_t
e820_abstraction_iterator_get_next(e820_handle_t e820_handle,
				   e820_abstraction_range_iterator_t iter);


/*--------------------------------------------------------------------------
 * Function: e820_abstraction_iterator_get_range_details
 *  Description: This function provided the details of current memory range.
 *               Please, see the content of "int15_e820_memory_map_entry_ext_t"
 *               structure.
 *               Node, that the structure cannot be updated.
 *  Input: iter - current iterator.
 *  Return Value: pointer to memory region details. In iterator is invalid, NULL
 *                will be returned.
 *--------------------------------------------------------------------------*/
const int15_e820_memory_map_entry_ext_t *
e820_abstraction_iterator_get_range_details(IN e820_abstraction_range_iterator_t
					    iter);


/*--------------------------------------------------------------------------
 * Function: e820_abstraction_create_new_map
 *  Description: This function is used to create new e820 map for filling.
 *  Output: handle - new e820 handle. It may be used as parameter for other
 *                   function
 *  Return Value: TRUE in case when memory allocation succeeded.
 *                FALSE in case when memory allocation failed.
 *--------------------------------------------------------------------------*/
boolean_t e820_abstraction_create_new_map(OUT e820_handle_t *handle);


/*--------------------------------------------------------------------------
 * Function: e820_abstraction_create_new_map
 *  Description: This function is used to destroy created e820 map.
 *  Input: handle - handle returned by "e820_abstraction_create_new_map"
 *                  function.
 *                  It is forbidden to pass E820_ORIGINAL_MAP as parameter.
 *--------------------------------------------------------------------------*/
void e820_abstraction_destroy_map(IN e820_handle_t handle);


/*--------------------------------------------------------------------------
 * Function: e820_abstraction_add_new_range
 *  Description: This function is used add new range for created e820 map.
 *               The range mustn't intersect with already inseted ones and
 *               must be in chronological order.
 *  Input: handle - handle returned by "e820_abstraction_create_new_map"
 *                  function.
 *                  It is forbidden to pass E820_ORIGINAL_MAP as parameter.
 *         base_address - base address to be recorded
 *         length - length of range to be recorded.
 *         address_range_type - type of range to be recorded
 *         extended_attributes - extended attributes to be recorded.
 *--------------------------------------------------------------------------*/
boolean_t e820_abstraction_add_new_range(IN e820_handle_t handle,
					 IN uint64_t base_address,
					 IN uint64_t length,
					 IN int15_e820_range_type_t address_range_type,
					 IN int15_e820_memory_map_ext_attributes_t extended_attributes);

typedef struct {
	uint16_t	em_es;
	uint64_t	es_base;
	uint32_t	es_lim;
	uint32_t	es_attr;
	uint16_t	em_ss;
	uint64_t	ss_base;
	uint32_t	ss_lim;
	uint32_t	ss_attr;
	uint64_t	em_rip;
	uint64_t	em_rflags;
	uint64_t	em_rax;
	uint64_t	em_rbx;
	uint64_t	em_rcx;
	uint64_t	em_rdx;
	uint64_t	em_rdi;
	uint64_t	em_rsp;
} PACKED e820_guest_state_t;

typedef struct {
	e820_guest_state_t			e820_guest_state;
	e820_handle_t				emu_e820_handle;
	const int15_e820_memory_map_entry_ext_t	*emu_e820_memory_map;
	uint16_t				emu_e820_memory_map_size;       /* in entries */
	uint16_t				emu_e820_continuation_value;    /* entry no */
	guest_handle_t				guest_handle;
	gpm_handle_t				guest_phy_memory;
} PACKED e820_map_state_t;

/* original INT15 handler will be embed into mon hookup code to handle other
 * type of INT15 interrupt besides E820 */
#define ORIG_HANDLER_OFFSET      6
/* this is derived from update_int15_handling()
 * assumption: vmcall is at this offset from
 * start of the interrupt handling routine */
#define VMCALL_OFFSET            0xA

#define INT15_VECTOR_LOCATION    (0x15 * 4)

#define SEGMENT_OFFSET_TO_LINEAR(__seg, __ofs) \
	((((__seg) & 0xffff) << 4) + ((__ofs) & 0xffff))
#define RFLAGS_CARRY             1


void e820_save_guest_state(guest_cpu_handle_t gcpu, e820_map_state_t *emap);
void e820_restore_guest_state(guest_cpu_handle_t gcpu, e820_map_state_t *emap);
boolean_t e820_int15_handler(e820_map_state_t *emap);
extern boolean_t gpm_create_e820_map(IN gpm_handle_t gpm_handle,
				     OUT e820_handle_t *e820_handle);
extern void gpm_destroy_e820_map(IN e820_handle_t e820_handle);

#ifdef DEBUG

/*--------------------------------------------------------------------------
 * Function: e820_abstraction_print_memory_map
 *  Description: This function is used to print the memory map
 *  Input: handle - handle returned by "e820_abstraction_create_new_map"
 *                  function or E820_ORIGINAL_MAP.
 *--------------------------------------------------------------------------*/
void e820_abstraction_print_memory_map(IN e820_handle_t handle);
#endif

#endif
