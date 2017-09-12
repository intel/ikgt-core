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
