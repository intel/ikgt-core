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

#ifndef _MON_ACPI_H_
#define _MON_ACPI_H_

#define ACPI_PM1_CNTRL_REG_A        0
#define ACPI_PM1_CNTRL_REG_B        1
#define ACPI_PM1_CNTRL_REG_COUNT    2

#include "actypes.h"
#include "actbl.h"
#include "actbl2.h"
#include "acconfig.h"

int mon_acpi_init(hva_t address);
acpi_table_header_t *mon_acpi_locate_table(char *sig);
uint16_t mon_acpi_smi_cmd_port(void);
uint8_t mon_acpi_pm_port_size(void);
uint32_t mon_acpi_pm_port_a(void);
uint32_t mon_acpi_pm_port_b(void);
unsigned mon_acpi_sleep_type_to_state(unsigned pm_reg_id, unsigned sleep_type);
int mon_acpi_waking_vector(uint32_t *p_waking_vector,
			   uint64_t *p_extended_waking_vector);
int mon_acpi_facs_flag(uint32_t *flags, uint32_t *ospm_flags);

typedef void (*mon_acpi_callback_t) (void);
boolean_t mon_acpi_register_platform_suspend_callback(mon_acpi_callback_t
						      suspend_cb);
boolean_t mon_acpi_register_platform_resume_callback(
	mon_acpi_callback_t resume_cb);

#endif                          /* _MON_ACPI_H_ */
