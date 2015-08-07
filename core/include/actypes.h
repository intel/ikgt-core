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

#ifndef __ACTYPES_H__
#define __ACTYPES_H__

/* Names within the namespace are 4 bytes long */
#define ACPI_NAME_SIZE                  4
/* Sizes for ACPI table headers */
#define ACPI_OEM_ID_SIZE                6
#define ACPI_OEM_TABLE_ID_SIZE          8

/*
 * Power state values
 */
#define ACPI_STATE_UNKNOWN              ((uint8_t)0xFF)

#define ACPI_STATE_S0                   ((uint8_t)0)
#define ACPI_STATE_S1                   ((uint8_t)1)
#define ACPI_STATE_S2                   ((uint8_t)2)
#define ACPI_STATE_S3                   ((uint8_t)3)
#define ACPI_STATE_S4                   ((uint8_t)4)
#define ACPI_STATE_S5                   ((uint8_t)5)
#define ACPI_S_STATES_MAX               ACPI_STATE_S5
#define ACPI_S_STATE_COUNT              6

#endif                          /* __ACTYPES_H__ */
