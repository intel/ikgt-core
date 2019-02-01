/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _EFI_SERVICES_H_
#define _EFI_SERVICES_H_

boolean_t init_efi_services(uint64_t sys_table);
void *efi_locate_acpi_table(void);

uint64_t efi_launch_aps(uint64_t c_entry);

#endif
