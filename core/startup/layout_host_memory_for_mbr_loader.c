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
#define MON_DEADLOOP() \
	MON_DEADLOOP_LOG(LAYOUT_HOST_MEMORY_FOR_MBR_LOADER_C)
#define MON_ASSERT(__condition) \
	MON_ASSERT_LOG(LAYOUT_HOST_MEMORY_FOR_MBR_LOADER_C, __condition)
#include "layout_host_memory.h"
#include "e820_abstraction.h"
#include "gpm_api.h"
#include "guest.h"
#include "guest_cpu.h"
#include "mon_dbg.h"

#define FOUR_GIGABYTE 0x100000000

/**************************************************************************
 *
 * Read input data structure and create all guests
 *
 * NOTE: current implementation is valid only for MBR loader. For
 *       driver-loading scenario it should be changed
 *
 ************************************************************************** */

/* -------------------------- types ----------------------------------------- */
/* ---------------------------- globals ------------------------------------- */
extern uint32_t g_is_post_launch;
/* ---------------------------- internal functions -------------------------- */
/* ---------------------------- APIs ------------------------------------- */

/*
 * init memory layout
 * This function should perform init of "memory layout object" and
 * init the primary guest memory layout.
 * In the case of no secondary guests, memory layout object is not required
 *
 * For primary guest:
 * - All memory upto 4G is mapped, except of MON and secondary guest areas
 * - Only specified memory above 4G is mapped. Mapping in the >4G region for
 * primary
 * guest should be added by demand
 *
 * For secondary guests:
 * - All secondary guests are loaded lower than 4G
 */
boolean_t init_memory_layout_from_mbr(
	const mon_memory_layout_t *mon_memory_layout,
	gpm_handle_t primary_guest_gpm,
	boolean_t are_secondary_guests_exist,
	const mon_application_params_struct_t *
	application_params)
{
	e820_abstraction_range_iterator_t e820_iter;
	const int15_e820_memory_map_entry_ext_t *e820_entry = NULL;
	boolean_t ok;
	uint64_t range_start;
	uint64_t range_end;
	int15_e820_range_type_t range_type;
	int15_e820_memory_map_ext_attributes_t range_attr;
	uint64_t page_index;
	uint64_t *entry_list;

	MON_ASSERT(e820_abstraction_is_initialized());

	if (global_policy_uses_vtlb()) {
		mam_rwx_attrs.uint32 = 0x5;
		mam_rw_attrs.uint32 = 0x1;
		mam_ro_attrs.uint32 = 0x0;
	}

	/* 1. first map 0-4G host region to primary guest */
	ok = mon_gpm_add_mapping(primary_guest_gpm, 0, 0,
		FOUR_GIGABYTE, mam_rwx_attrs);
	MON_LOG(mask_anonymous, level_trace,
		"Primary guest GPM: add 0-4G region\r\n");
	MON_ASSERT(ok == TRUE);

	/* 2. Add real memory to "memory layout object" and to the primary guest
	 * if this memory range is above 4G
	 * if in the post launch mode skip it */
	for (e820_iter = e820_abstraction_iterator_get_first(E820_ORIGINAL_MAP);
	     e820_iter != E820_ABSTRACTION_NULL_ITERATOR;
	     e820_iter =
		     e820_abstraction_iterator_get_next(E820_ORIGINAL_MAP,
			     e820_iter)) {
		e820_entry = e820_abstraction_iterator_get_range_details(
			e820_iter);

		range_start = e820_entry->basic_entry.base_address;
		range_end = range_start + e820_entry->basic_entry.length;
		range_type = e820_entry->basic_entry.address_range_type;
		range_attr = e820_entry->extended_attributes;

		/* align ranges and sizes on 4K boundaries */
		range_start = ALIGN_FORWARD(range_start, PAGE_4KB_SIZE);
		range_end = ALIGN_BACKWARD(range_end, PAGE_4KB_SIZE);

		MON_DEBUG_CODE({
				if (range_start !=
				    e820_entry->basic_entry.base_address) {
					MON_LOG(mask_anonymous, level_trace,
						"init_memory_layout_from_mbr WARNING: aligning"
						" E820 range start from %P to %P\n",
						e820_entry->basic_entry.
						base_address,
						range_start);
				}

				if (range_end !=
				    e820_entry->basic_entry.base_address +
				    e820_entry->basic_entry.length) {
					MON_LOG(mask_anonymous, level_trace,
						"init_memory_layout_from_mbr WARNING: aligning"
						" E820 range end from %P to %P\n",
						e820_entry->basic_entry.
						base_address +
						e820_entry->basic_entry.length,
						range_end);
				}
			}
			)

		if (range_end <= range_start) {
			/* after alignment the range became invalid */
			MON_LOG(mask_anonymous,
				level_trace,
				"init_memory_layout_from_mbr WARNING: skipping invalid"
				" E820 memory range FROM %P to %P\n",
				range_start,
				range_end);
			continue;
		}

		/* add memory to the "memory layout object" if this is a real memory
		 * lower 4G */
		if (are_secondary_guests_exist &&
		    (range_start < FOUR_GIGABYTE) &&
		    range_attr.bits.enabled &&
		    (!range_attr.bits.non_volatile)) {
			uint64_t top =
				(range_end <
				 FOUR_GIGABYTE) ? range_end : FOUR_GIGABYTE;

			if ((range_type ==
			     INT15_E820_ADDRESS_RANGE_TYPE_MEMORY) ||
			    (range_type ==
			     INT15_E820_ADDRESS_RANGE_TYPE_ACPI)) {
				/* here we need to all a call to the "memory layout object"
				 * to fill is with the range_start-top range */

				/* to make compiler happy */
				top = 0;
			}
		}

		/* add memory to the primary guest if this is a memory above 4G */
		if (range_end > FOUR_GIGABYTE) {
			uint64_t bottom =
				(range_start <
				 FOUR_GIGABYTE) ? FOUR_GIGABYTE : range_start;

			if (bottom < range_end) {
				MON_LOG(mask_anonymous,
					level_trace,
					"Primary guest GPM: add memory above 4GB base"
					" %p size %p\r\n",
					bottom,
					range_end - bottom);
				ok = mon_gpm_add_mapping(primary_guest_gpm,
					bottom,
					bottom,
					range_end - bottom,
					mam_rwx_attrs);
				MON_ASSERT(ok == TRUE);
			}
		}
	}

	/* now remove the MON area from the primary guest */
	ok = mon_gpm_remove_mapping(primary_guest_gpm,
		mon_memory_layout[mon_image].base_address,
		mon_memory_layout[mon_image].total_size);
	MON_LOG(mask_anonymous, level_trace,
		"Primary guest GPM: remove mon image base %p size 0x%x\r\n",
		mon_memory_layout[mon_image].base_address,
		mon_memory_layout[mon_image].total_size);

	/* and remove thunk area from the primary guest also
	 * if post launch skip it. */

	ok = mon_gpm_remove_mapping(primary_guest_gpm,
		mon_memory_layout[thunk_image].base_address,
		mon_memory_layout[thunk_image].total_size);
	MON_LOG(mask_anonymous, level_trace,
		"Primary guest GPM: remove thunk image base %p size 0x%x\r\n",
		mon_memory_layout[thunk_image].base_address,
		mon_memory_layout[thunk_image].total_size);

	if (g_is_post_launch) {
		MON_ASSERT(application_params != NULL);
		entry_list = (uint64_t *)application_params->address_entry_list;

		for (page_index = 0;
		     page_index < application_params->entry_number && ok;
		     page_index++) {
			ok = mon_gpm_remove_mapping(primary_guest_gpm,
				(gpa_t)entry_list[page_index],
				PAGE_4KB_SIZE);
			/* MON_LOG(mask_anonymous, level_trace,"Primary guest GPM: remove
			 * heap page base %p 4K size\r\n", (GPA)entry_list[page_index]); */
		}
	}

	MON_ASSERT(ok == TRUE);

	return TRUE;
}
