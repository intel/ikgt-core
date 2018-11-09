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

#ifndef _FILE_PACK_H_
#define _FILE_PACK_H_

#include "vmm_base.h"
#include "vmm_arch.h"
#include "file_pack_asm.h"

/* The final package(evmm_pkg.bin) is packed with stage0.bin(.text section),
 * stage1.bin, and evmm.bin */

/* file layout in this order */
enum {
	STAGE0_BIN_INDEX,
	STAGE1_BIN_INDEX,
	EVMM_BIN_INDEX,

#if defined (MODULE_TRUSTY_GUEST) && defined (PACK_LK)
	LK_BIN_INDEX,
#endif

#if defined (MODULE_OPTEE_GUEST) && defined (PACK_OPTEE)
	OPTEE_BIN_INDEX,
#endif

	/* keep this as the last one */
	PACK_BIN_COUNT
} file_pack_index_t;

typedef struct {
	/* must be EVMM_BINARY_FILE_MAPPING_HEADER_MAGIC0/1*/
	uint32_t magic0;
	uint32_t magic1;

	/* valid only if the corresponding flag bit-map is set */
	uint32_t file_size[PACK_BIN_COUNT];
} file_offset_header_t;

/* the func is used in different binary: pack and stage0 */
static inline file_offset_header_t* get_file_offsets_header(uint64_t start_addr, uint64_t size)
{
	/* search the magic file offset header */
	uint64_t tmpbuf;
	file_offset_header_t *tmp_hdr;
	for (tmpbuf = start_addr; tmpbuf < (start_addr + size - 4); tmpbuf++) {
		/* 4 byte aligned searching */
		tmp_hdr = (file_offset_header_t *)tmpbuf;

		if ((tmp_hdr->magic0 == FILE_OFFSET_MAGIC0) &&
			(tmp_hdr->magic1 == FILE_OFFSET_MAGIC1)) {
			return (file_offset_header_t *) tmp_hdr;
		}
	}
	return NULL;
}
#endif
