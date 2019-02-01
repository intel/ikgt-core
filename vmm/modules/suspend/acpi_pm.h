/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _ACPI_PM_H_
#define _ACPI_PM_H_

#define ACPI_PM1_CNTRL_A        0
#define ACPI_PM1_CNTRL_B        1
#define ACPI_PM1_CNTRL_COUNT    2

typedef struct {
	uint32_t *p_waking_vector;
	uint16_t port_id[ACPI_PM1_CNTRL_COUNT];
	uint32_t pad;
} acpi_fadt_info_t;

boolean_t acpi_pm_is_s3(uint16_t port_id ,uint32_t port_size, uint32_t value);
void acpi_pm_init(acpi_fadt_info_t *p_acpi_fadt_info);

#endif
