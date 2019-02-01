/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
 *
 */

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
void make_fake_acpi(acpi_table_header_t *header);

#endif
