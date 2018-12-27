/*
 * Copyright (c) 2018 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "efi_types.h"
#include "efi_api.h"
#include "lib/util.h"

extern efi_system_table_t *g_sys_table;

/* ACPI 2.0 or newer tables */
#define EFI_ACPI_TABLE_GUID \
{\
	0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81} \
}

/* ACPI 1.0 */
#define EFI_ACPI_10_TABLE_GUID \
{\
	0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} \
}

void *efi_locate_acpi_table(void)
{
	uintn_t i;
	efi_guid_t acpi_table_guid = EFI_ACPI_TABLE_GUID;
	efi_guid_t acpi_10_table_guid = EFI_ACPI_10_TABLE_GUID;
	efi_configuration_table_t *conf_table;
	void *acpi_ptr = NULL, *acpi_10_ptr = NULL;

	if (!g_sys_table) {
		return NULL;
	}

	if ((!g_sys_table->num_of_table_entries) ||
		(!g_sys_table->configuration_table)) {
		return NULL;
	}

	conf_table = g_sys_table->configuration_table;
	for (i = 0; i < g_sys_table->num_of_table_entries; i++) {
		if (0 == memcmp(&acpi_table_guid, &conf_table[i].vendor_guid, sizeof(efi_guid_t))) {
			acpi_ptr = conf_table[i].vendor_table;
			break; // End loop if ACPI 2.0 or newer table found
		}

		if (0 == memcmp(&acpi_10_table_guid, &conf_table[i].vendor_guid, sizeof(efi_guid_t))) {
			acpi_10_ptr = conf_table[i].vendor_table;
		}
	}

	if (acpi_ptr) {
		return acpi_ptr;
	} else if (acpi_10_ptr) {
		return acpi_10_ptr;
	} else {
		return NULL;
	}
}
