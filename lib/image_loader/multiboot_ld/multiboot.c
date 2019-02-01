/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <vmm_base.h>
#include <evmm_desc.h>
#include "lib/print.h"
#include "lib/util.h"
#include "multiboot.h"
#include "lib/image_loader.h"

#define local_print(fmt, ...)
//#define local_print(fmt, ...) printf(fmt, ##__VA_ARGS__)

#define MODULE_MAX_SIZE			0x1000000  /* Assume largest size 16 MB */

boolean_t parse_multiboot_module(void *addr,
		uint64_t *start,
		uint64_t *size,
		uint64_t index)
{
	multiboot_info_t *mbi = (multiboot_info_t *)addr;
	multiboot_module_t *mod_list = NULL;
	multiboot_module_t *mod = NULL;
	grub_module_index_t idx = index;

	if ((NULL == addr) || (NULL == start) || (NULL == size)) {
		local_print("Failed to parse module: Invalid input parameter!\n");
		return FALSE;
	}

	if (!(mbi->flags & MBI_MODULES)) {
		local_print("Multiboot info does not contain mods field!\n");
		return FALSE;
	}

	if (mbi->mods_count == 0) {
		local_print("No available module!\n");
		return FALSE;
	}

	if (idx > mbi->mods_count) {
		local_print("request index exceeds module count!\n");
		return FALSE;
	}

	mod_list = (multiboot_module_t *)((uint64_t)mbi->mods_addr);
	mod = &mod_list[idx];

	/* All modules does not accept cmd line for now */
	*size = mod->mod_end - mod->mod_start;
	if ((0 == *size) || (*size > MODULE_MAX_SIZE)) {
		local_print("Invalid size for module(%d)!\n", idx);
		*size = 0;
		return FALSE;
	}
	*start = mod->mod_start;

	return TRUE;
}


multiboot_header_t *locate_multiboot_header(void *addr)
{
	uint32_t count = 0;
	uint32_t *start = (uint32_t *)addr;

	for (count=0; count<2048; count++, start++) {
		if (MULTIBOOT_HEADER_MAGIC == *start)
			return (multiboot_header_t *)start;
	}

	return NULL;
}

static inline boolean_t is_multiboot_header_valid(multiboot_header_t *header)
{
	return !(header->magic + header->flags + header->checksum);
}

boolean_t relocate_multiboot_image(uint64_t *start, uint64_t size, uint64_t *entry_point)
{
	multiboot_header_t *mb_header = NULL;

	if ((NULL == start) || (NULL == entry_point)) {
		local_print("Failed to relocate multiboot image: Invalid input parameters!\n");
		return FALSE;
	}

	mb_header = locate_multiboot_header((void *)start);
	if (NULL == mb_header) {
		local_print("Failed to find multiboot header!\n");
		return FALSE;
	}

	if (!is_multiboot_header_valid(mb_header)) {
		local_print("Invalid multiboot header!\n");
		return FALSE;
	}

	memcpy((void *)(long)mb_header->load_addr, (void *)(long)start, size);
	*entry_point = mb_header->entry_addr;

	return TRUE;
}
