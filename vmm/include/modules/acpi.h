/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
* Copyright (C) 2000 - 2008, Intel Corp.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions, and the following disclaimer,
*    without modification.
* 2. Redistributions in binary form must reproduce at minimum a disclaimer
*    substantially similar to the "NO WARRANTY" disclaimer below
*    ("Disclaimer") and any redistribution must be conditioned upon
*    including a substantially similar Disclaimer requirement for further
*    binary redistribution.
* 3. Neither the names of the above-listed copyright holders nor the names
*    of any contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*
* NO WARRANTY
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
* A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
* HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
* IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGES.
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

#ifndef _ACPI_H_
#define _ACPI_H_

#ifndef MODULE_ACPI
#error "MODULE_ACPI is not defined"
#endif

/* Names within the namespace are 4 bytes long */
#define ACPI_NAME_SIZE                  4
/* Sizes for ACPI table headers */
#define ACPI_OEM_ID_SIZE                6
#define ACPI_OEM_TABLE_ID_SIZE          8

/*
 * Values for description table header signatures for tables defined in this
 * file. Useful because they make it more difficult to inadvertently type in
 * the wrong signature.
 */
#define ACPI_SIG_FADT           0x50434146      /* the ASCII vaule for FACP: Fixed ACPI Description Table */

/*******************************************************************************
 *
 * Master ACPI Table header. This common header is used by all ACPI tables
 * except the RSDP and FACS.
 *
 ******************************************************************************/

typedef struct {
	uint32_t	signature;					/* ASCII table signature */
	uint32_t	length;						/* length of table in bytes, including this header */
	uint8_t		revision;					/* ACPI Specification minor version # */
	uint8_t		check_sum;					/* To make sum of entire table == 0 */
	char		oem_id[ACPI_OEM_ID_SIZE];			/* ASCII OEM identification */
	char		oem_table_id[ACPI_OEM_TABLE_ID_SIZE];		/* ASCII OEM table identification */
	uint32_t	oem_revision;					/* OEM revision number */
	char		asl_compiler_id[ACPI_NAME_SIZE];		/* ASCII ASL compiler vendor ID */
	uint32_t	asl_compiler_revision;				/* ASL compiler version */
} acpi_table_header_t;

#ifdef DEBUG
void acpi_print_header(acpi_table_header_t *table_header);
#endif
acpi_table_header_t *acpi_locate_table(uint32_t sig);

#endif
