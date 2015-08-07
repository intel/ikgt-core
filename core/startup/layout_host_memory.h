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

#ifndef _LAYOUT_HOST_MEMORY_H_
#define _LAYOUT_HOST_MEMORY_H_

#include "mon_startup.h"
#include "mon_objects.h"

/**************************************************************************
 *
 * Perform host memory layout for multiguest environment
 *
 * Different algorithms should be used for MBR-based abd loader-based
 * MON load scenarios
 *
 ************************************************************************** */

/*-------------------------------------------------------------------------
 *
 * init memory layout
 * This function should perform init of "memory layout object" and
 * init the primary guest memory layout.
 *
 *------------------------------------------------------------------------- */
boolean_t init_memory_layout_from_mbr(
	const mon_memory_layout_t *mon_memory_layout,
	gpm_handle_t primary_guest_gpm,
	boolean_t are_secondary_guests_exist,
	const mon_application_params_struct_t *
	application_params);

/*-------------------------------------------------------------------------
 *
 * Allocate memory for seondary guest, remove it from primary guest and
 * return allocated address
 *
 *------------------------------------------------------------------------- */
uint64_t allocate_memory_for_secondary_guest_from_mbr(gpm_handle_t
						      primary_guest_gpm,
						      gpm_handle_t
						      secondary_guest_gpm,
						      uint32_t required_size);

/* wrappers */
INLINE
boolean_t init_memory_layout(const mon_memory_layout_t *mon_memory_layout,
			     gpm_handle_t primary_guest_gpm,
			     boolean_t are_secondary_guests_exist,
			     const mon_application_params_struct_t *
			     application_params)
{
	return init_memory_layout_from_mbr(mon_memory_layout,
		primary_guest_gpm,
		are_secondary_guests_exist,
		application_params);
}

#endif                          /* _LAYOUT_HOST_MEMORY_H_ */
