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

#ifndef PAGE_WALKER_H

#include <vmm_base.h>
#include <gcpu.h>
#include <gpm.h>

void page_walk_init(void);

/*-------------------------------------------------------------------------
 * Function: gcpu_gva_to_gpa
 *  Description: This function is used in order to convert Guest Virtual Address
 *               to Guest physical Address (GVA-->GPA).
 *  Input:  gcpu - guest cpu handle.
 *	    gva - guest virtual address.
 *          access - access rights, can be read, write, or read and write
 *  Output: p_gpa - guest physical address, it is valid when return true.
 *          p_pfec - page fault error code, it is valid when return false.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t gcpu_gva_to_gpa(IN guest_cpu_handle_t gcpu,
				IN uint64_t gva,
				IN uint32_t access,
				OUT uint64_t *p_gpa,
				OUT pf_ec_t *p_pfec);
#endif

