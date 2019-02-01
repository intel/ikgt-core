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
