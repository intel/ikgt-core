/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "efi_types.h"
#include "efi_api.h"
#include "efi_error.h"

efi_system_table_t *g_sys_table;
efi_boot_services_t *g_bs;

boolean_t init_efi_services(uint64_t sys_table)
{
	g_sys_table = (efi_system_table_t *)sys_table;
	if (!g_sys_table) {
		return FALSE;
	}

	if (g_sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE) {
		return FALSE;
	}

	g_bs = g_sys_table->boot_services;
	if (!g_bs) {
		return FALSE;
	}

	if (g_bs->hdr.signature != EFI_BOOT_SERVICES_SIGNATURE) {
		return FALSE;
	}

	return TRUE;
}

uint64_t efi_get_tom(void)
{
	uintn_t mmap_size, map_key, desc_size;
	uint32_t desc_ver;
	uint64_t end;
	uint64_t tom = 0;
	efi_memory_descriptor_t temp_mmap[1];
	efi_memory_descriptor_t *mmap_desc;
	efi_memory_descriptor_t *mmap_desc_ptr;
	efi_status_t status;
	uintn_t i;

	/* get mmap_size */
	mmap_size = sizeof(temp_mmap);
	status = g_bs->get_memory_map(&mmap_size, &temp_mmap[0], &map_key, &desc_size, &desc_ver);

	/* allocate space for mmap */
	mmap_size += PAGE_4K_SIZE;
	status = g_bs->allocate_pool(EfiLoaderData, mmap_size, (void **)&mmap_desc);
	if (status != EFI_SUCCESS) {
		return 0;
	}

	/* get mmap */
	status = g_bs->get_memory_map(&mmap_size, mmap_desc, &map_key, &desc_size, &desc_ver);
	if (status != EFI_SUCCESS) {
		return 0;
	}

	/* caculate top of memory */
	mmap_desc_ptr = mmap_desc;
	for (i = 0; i < mmap_size/desc_size; i++) {
		end = mmap_desc_ptr->physical_start + mmap_desc_ptr->number_of_pages * PAGE_4K_SIZE;
		if (tom < end) {
			tom = end;
		}

		mmap_desc_ptr = (efi_memory_descriptor_t *)(((uintn_t)mmap_desc_ptr) + desc_size);
	}

	/*
	 * When calling EFI API, the BIOS may enable interrupts for some services.
	 * But these interrupts cannot be handled in eVMM. So close interrupts after return back
	 * from efi services.
	 */
	asm volatile("cli;");

	return tom;
}
