/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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

