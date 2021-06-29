/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _ACPI_PM_H_
#define _ACPI_PM_H_

#define ACPI_PM1A_CNT    0U
#define ACPI_PM1B_CNT    1U
#define ACPI_PM1_CNT_NUM 2U

#include "modules/acpi.h"

boolean_t acpi_pm_is_s3(uint64_t addr, uint32_t size, uint32_t value);
acpi_generic_address_t *get_pm1x_reg(uint32_t id);
uint32_t *get_waking_vector(void);
void acpi_pm_init(void);

#endif
