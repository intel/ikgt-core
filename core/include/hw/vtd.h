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

#ifndef _VTD_H
#define _VTD_H

#include "mon_defs.h"
#include "vtd_domain.h"
#include "lock.h"
#include "vtd_hw_layer.h"
#include "mon_startup.h"

typedef enum {
	DMA_BLOCK_WRITE,        /* clear W bit only */
	DMA_UNBLOCK_WRITE,      /* set W bit only */

	DMA_REMAPPING,          /* redirect both DMA read and write to a dummy page. */
	DMA_RESTORE_MAPPING,    /* restore to previous mapping */

	DMA_BLOCK_READ,         /* clear R bit only */
	DMA_UNBLOCK_READ,       /* set R bit only */

	DMA_BLOCK_READ_WRITE,   /* block both DMA read and write access (NOT-present) */
	DMA_UNBLOCK_READ_WRITE  /* restore both DMA read and write access (NOT-present) */
} dma_block_type_t;

/* ------------------------------------------------------------------------
 * Function: vtd_is_vtd_available
 * Description: This function should be called after vtd_initialize, it returns
 *              whether vtd is available.
 * Input: void
 * Return value: TRUE - VT-d hardware exists and initialized successfully.
 *               FALSE - VT-d is not available.
 * ------------------------------------------------------------------------*/
boolean_t vtd_is_vtd_available(void);

/* ------------------------------------------------------------------------
 * Function: vtd_inv_iotlb_global
 * Description: This function flushes all iotlb
 * Input: void * Return value: void
 * ------------------------------------------------------------------------*/
void vtd_inv_iotlb_global(void);

/* ------------------------------------------------------------------------
 * Function: vtd_set_dma_blocking
 * Description: This function enables modifying VT-d mappings to avoid DMA
 *              attacking. for different dma_block_type_t, it will update the
 *              permission of an existing mapping or remap some dva to a dummy
 *              page.
 *              Notice: whenever this function is called, please call
 *              vtd_inv_iotlb_global to flush TLB, otherwise, the system may
 *              use the stale mappings.
 * Input: type - currently only block and unblock write are using.
 *        gpa - the DMA target address , gpa is the same as dva from the
 *              perspective of DMA devices. gpa must be 4KB alignment.
 *        size - size of contigous DMA region. size must be an integer multiple
 *               of 4KB. *
 * Return value: TRUE - successfully modified the VT-d mapping.
 *               FALSE - parameters assertion failed or fail to modify the
 *                       mapping.
 * ------------------------------------------------------------------------*/
boolean_t vtd_set_dma_blocking(dma_block_type_t type,
			       uint64_t gpa,
			       uint32_t size);

uint32_t vtd_num_supported_domains(vtd_dma_remapping_hw_uint_t *dmar);

#endif
