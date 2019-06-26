/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef HOST_MEMORY_MANAGER_H
#define HOST_MEMORY_MANAGER_H

#include "mam.h"
#include <evmm_desc.h>

typedef union {
	struct {
		uint32_t p:1; // bit[0] present
		uint32_t w:1; // bit[1] writable
		uint32_t x:1; // bit[2] execute
		uint32_t pi:2; // bit[4:3] pat index
			// "wb" is used for all page structures. assume it <=3 (usually it is 0)
			// so, we don't need the "pat" bit
		uint32_t rsvd:27;
	} bits;
	uint32_t uint32;
} cr3_attr_t;

extern uint64_t top_of_memory;

/*-----------------------------------------------------------------------
 * Function: hmm_setup
 *  Description: This function should be called in order to
 *               initialize Host Memory Manager. This function must be called
 *               first.
 *  Input: startup_struct - pointer to startup data structure
 *------------------------------------------------------------------------- */
void hmm_setup(evmm_desc_t *evmm_desc);


/*-------------------------------------------------------------------------
 * Function: hmm_enable
 *  Description: This function will set new CR3
 *------------------------------------------------------------------------- */
void hmm_enable(void);

/*-------------------------------------------------------------------------
 * Function: hmm_hva_to_hpa
 *  Description: This function is used in order to convert Host Virtual Address
 *               to Host Physical Address (HVA-->HPA).
 *  Input: hva - host virtual address.
 *  Output: hpa - host physical address.
 *          p_attr - page table entry attribute, can be NULL.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t hmm_hva_to_hpa(IN uint64_t hva, OUT uint64_t *p_hpa, OUT cr3_attr_t *p_attr);

/*-------------------------------------------------------------------------
 * Function: hmm_hpa_to_hva
 *  Description: This function is used in order to convert Host Physical Address
 *               to Host Virtual Address (HPA-->HVA), i.e. converting physical
 *               address to pointer.
 *  Input: hpa - host physical address.
 *  Output: p_hva - host virtual address.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t hmm_hpa_to_hva(IN uint64_t hpa, OUT uint64_t *p_hva);

/*-------------------------------------------------------------------------
 * Function: hmm_unmap_hpa
 *  Description: This function is used in order to unmap HVA -> HPA references
 *               to physical address
 *  Input: hpa - host physical address - must be aligned on page
 *         size - size in bytes, but must be alinged on page size (4K, 8K, ...)
 *         flush_tlbs_on_all_cpus - TRUE in case when flush TLB on all cpus is
 *                                  required
 *------------------------------------------------------------------------- */
void hmm_unmap_hpa(IN uint64_t hpa, uint64_t size);

#endif
