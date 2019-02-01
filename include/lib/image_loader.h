/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _IMAGE_LOADER_H_
#define _IMAGE_LOADER_H_

#include "vmm_base.h"
#include "evmm_desc.h"

/**************************************************************************
 *
 * Get info about the VMM memory image itself
 *
 ************************************************************************** */
typedef struct {
	char        *start;
	uint32_t    size;
	boolean_t   readable;
	boolean_t   writable;
	boolean_t   executable;
	boolean_t   valid;
	uint32_t    pad;
} image_section_info_t;


boolean_t get_image_section(void *image_base, uint16_t index, image_section_info_t *info);

/*----------------------------------------------------------------------
 *
 * relocate image in memory
 *
 * Input:
 * module_file_info_t* file_info - file info
 * uint64_t* p_entry - address of the uint64_t that will be filled
 * with the address of image entry point if
 * all is ok
 *
 * Output:
 * Return value - FALSE on any error
 *---------------------------------------------------------------------- */
boolean_t relocate_elf_image(module_file_info_t *file_info, uint64_t *p_entry);

/*----------------------------------------------------------------------
 * Parse multiboot module by index
 *
 *  INPUT        void *addr      -- multiboot info address
 *  INPUT/OUTPUT uint64_t *start -- start address of this module
 *  INPUT/OUTPUT uint64_t *size  -- size of this module
 *  INPUT        uint64_t index  -- module index (support Trusty/Testrunner only)
 *---------------------------------------------------------------------- */
boolean_t parse_multiboot_module(void *addr,
		uint64_t *start,
		uint64_t *size,
		uint64_t index);

/*----------------------------------------------------------------------
 * Relocate multiboot image
 *
 *  INPUT        uint64_t *start       -- start address of this image
 *  INPUT        uint64_t size         -- size of this image
 *  INPUT/OUTPUT uint64_t *entry_point -- entry point after relocation
 *---------------------------------------------------------------------- */
boolean_t relocate_multiboot_image(uint64_t *start,
				uint64_t size,
				uint64_t *entry_point);

#endif     /* _IMAGE_LOADER_H_ */
