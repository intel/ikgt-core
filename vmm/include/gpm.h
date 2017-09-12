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

#include <vmm_base.h>
#include <vmm_objects.h>
#include "mam.h"
#include "ept.h"

#define GUEST_CAN_READ 0x1
#define GUEST_CAN_WRITE 0x2

void gpm_create_mapping(guest_handle_t guest);

//the cache in attr[5:3] is valid
void gpm_set_mapping_with_cache(IN guest_handle_t guest,
				IN uint64_t gpa,
				IN uint64_t hpa,
				IN uint64_t size,
				IN uint32_t attr);

//the cache in attr[5:3] is not used. cache will be set automatically
void gpm_set_mapping(IN guest_handle_t guest,
			IN uint64_t gpa,
			IN uint64_t hpa,
			IN uint64_t size,
			IN uint32_t attr);

#define gpm_remove_mapping(guest, gpa, size) gpm_set_mapping_with_cache(guest, gpa, 0, size, 0)

/*-------------------------------------------------------------------------
 * Function: gpm_gpa_to_hpa
 *  Description: This function is used in order to convert Guest Physical Address
 *               to Host Physical Address (GPA-->HPA).
 *  Input:  guest - guest handle.
 *	    gpa - guest physical address.
 *  Output: p_hpa - host physical address.
 *          p_attr - ept page table entry attribute, can be NULL.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t gpm_gpa_to_hpa(IN guest_handle_t guest,
			     IN uint64_t gpa,
			     OUT uint64_t *p_hpa,
			     OUT ept_attr_t *p_attr);

/*-------------------------------------------------------------------------
 * Function: gpm_gpa_to_hva
 *  Description: This function is used in order to convert Guest Physical Address
 *               to Host Virtual Address (GPA-->HVA).
 *  Input:  guest - guest handle.
 *          gpa - guest physical address.
 *          access - access rights, can be read, write, or read and write.
 *  Output: p_hva - host virtual address.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t gpm_gpa_to_hva(IN guest_handle_t guest,
			 IN uint64_t gpa,
			 IN uint32_t access,
			 OUT uint64_t *p_hva);
#endif
