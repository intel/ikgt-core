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

#include "vmm_base.h"
#include "dbg.h"
#include "hmm.h"

#include "modules/acpi.h"

/*------------------------------Types and Macros---------------------------*/
/* Constants used in searching for the RSDP in low memory */
#define ACPI_RSDP_SCAN_STEP 16

/* RSDP checksums */
#define ACPI_V1_RSDP_LENGTH       20

#define ACPI_SIG_RSD_PTR 0x2052545020445352ull /* "RSD PTR " */

/*******************************************************************************
 *
 * RSDP - Root System Description Pointer (signature is "RSD PTR ")
 *        Version 2
 *
 ******************************************************************************/

typedef struct {
	uint64_t        signature;                              /* ACPI signature, contains "RSD PTR " */
	uint8_t         check_sum;                              /* ACPI 1.0 checksum */
	char            oem_id[ACPI_OEM_ID_SIZE];               /* OEM identification */
	uint8_t         revision;                               /* Must be (0) for ACPI 1.0 or (2) for ACPI 2.0+ */
	uint32_t        rsdt_physical_address;                  /* 32-bit physical address of the RSDT */
	uint32_t        length;                                 /* Table length in bytes, including header (ACPI 2.0+) */
	uint64_t        xsdt_physical_address;                  /* 64-bit physical address of the XSDT (ACPI 2.0+) */
	uint8_t         extended_checksum;                      /* checksum of entire table (ACPI 2.0+) */
	uint8_t         reserved[3];                            /* reserved, must be zero */
} PACKED acpi_table_rsdp_t;

/*-----------------------------C-Code Starts Here--------------------------*/
#ifdef DEBUG
void acpi_print_header(acpi_table_header_t *table_header)
{
	VMM_ASSERT_EX(table_header, "acpi table header is NULL\n");

	vmm_printf(
		"==============header===============\n");
	vmm_printf("signature     = 0x%x\n",
		table_header->signature);
	vmm_printf("length        = 0x%x\n",
		table_header->length);
	vmm_printf("revision      = %d\n",
		table_header->revision);
	vmm_printf("check_sum      = 0x%x\n",
		table_header->check_sum);
	vmm_printf("oem_id         = %c%c%c%c%c%c\n",
		table_header->oem_id[0], table_header->oem_id[1],
		table_header->oem_id[2], table_header->oem_id[3],
		table_header->oem_id[4], table_header->oem_id[5]);
	vmm_printf(
		"oem_table_id    = %c%c%c%c%c%c%c%c\n",
		table_header->oem_table_id[0],
		table_header->oem_table_id[1],
		table_header->oem_table_id[2],
		table_header->oem_table_id[3],
		table_header->oem_table_id[4],
		table_header->oem_table_id[5],
		table_header->oem_table_id[6],
		table_header->oem_table_id[7]);
	vmm_printf("oem_revision   = %d\n",
		table_header->oem_revision);
	vmm_printf("asl_compiler_id = %c%c%c%c\n",
		table_header->asl_compiler_id[0],
		table_header->asl_compiler_id[1],
		table_header->asl_compiler_id[2],
		table_header->asl_compiler_id[3]);
	vmm_printf("asl_compiler_revision= %d\n",
		table_header->asl_compiler_revision);
	vmm_printf(
		"-----------------------------------\n");
}
#endif

/* Calculate acpi table checksum */
static uint8_t checksum(uint8_t *buffer, uint32_t length)
{
	uint8_t sum = 0;
	uint8_t *i = buffer;

	buffer += length;
	for (; i < buffer; sum += *(i++))
		;
	return sum;
}

/* Scan for RSDP table and return mapped address of rsdp, if found */
static acpi_table_rsdp_t *scan_for_rsdp(void *addr, uint32_t length)
{
	acpi_table_rsdp_t *rsdp;
	unsigned char *begin;
	unsigned char *i, *end;

	begin = addr;
	end = begin + length;

	/* Search from given start address for the requested length */
	for (i = begin; i < end; i += ACPI_RSDP_SCAN_STEP) {
		/* The signature and checksum must both be correct */
		if (*(uint64_t *)i != ACPI_SIG_RSD_PTR) {
			continue;
		}

		print_trace("Got the rsdp header, now check the checksum\n");
		rsdp = (acpi_table_rsdp_t *)i;

		/* signature matches, check the appropriate checksum */
		if (!checksum((unsigned char *)rsdp,
			/* rsdp->length is only valid when the revision is 2 or above */
			(rsdp->revision < 2) ? ACPI_V1_RSDP_LENGTH : rsdp->length)) {
			/* check_sum valid, we have found a valid RSDP */
			print_trace("Found acpi rsdp table\n");
			return rsdp;
		}
	}

	return NULL;
}

/* Find an acpi table with specified signature and return mapped address */
static acpi_table_header_t *get_acpi_table_from_rsdp(acpi_table_rsdp_t *rsdp,
						     uint32_t sig)
{
	acpi_table_header_t *sdt = NULL;
	acpi_table_header_t *tbl = NULL;
	uint32_t xsdt = 1;
	uint32_t i;
	uint32_t num;
	char *offset;

	/* Get xsdt pointer */
	if (rsdp->revision > 1 && rsdp->xsdt_physical_address) {
		print_trace("rsdp->xsdt_physical_address 0x%llX\n",
			rsdp->xsdt_physical_address);
		VMM_ASSERT_EX(hmm_hpa_to_hva(rsdp->xsdt_physical_address, (uint64_t *)&sdt),
			"fail to convert hpa 0x%llX to hva", rsdp->xsdt_physical_address);
	}

	/* Or get rsdt */
	if (!sdt && rsdp->rsdt_physical_address) {
		xsdt = 0;
		print_trace("rsdp->rsdt_physical_address = 0x%X\n",
			rsdp->rsdt_physical_address);
		VMM_ASSERT_EX(hmm_hpa_to_hva(rsdp->rsdt_physical_address, (uint64_t *)&sdt),
			"fail to convert hpa 0x%X to hva", rsdp->rsdt_physical_address);
	}

	/* Check if the rsdt/xsdt table pointer is NULL */
	if (NULL == sdt) {
		print_panic("map rsdt/xsdt error\n");
		return NULL;
	}

	/* Make sure the table checksum is correct */
	if (checksum((unsigned char *)sdt, sdt->length)) {
		print_panic("Wrong checksum in %s!\n", (xsdt) ? "XSDT" : "RSDT");
		return NULL;
	}

	print_trace("xsdt/rsdt checksum verified!\n");

	/* Calculate the number of table pointers in the xsdt or rsdt table */
	num = (sdt->length - sizeof(acpi_table_header_t)) /
			((xsdt) ? sizeof(uint64_t) : sizeof(uint32_t));

	print_trace("The number of table pointers in xsdt/rsdt = %d\n", num);

	/* Get to the table pointer area */
	offset = (char *)sdt + sizeof(acpi_table_header_t);

	/* Traverse the pointer list to get the desired acpi table */
	for (i = 0; i < num; ++i, offset += ((xsdt) ? sizeof(uint64_t) : sizeof(uint32_t))) {
		/* Get the address from the pointer entry */
		VMM_ASSERT_EX(hmm_hpa_to_hva((uint64_t)((xsdt) ?
			(*(uint64_t *)offset) : (*(uint32_t *)offset)), (uint64_t *)&tbl),
				"fail to convert hpa 0x%llX to hva",
				(uint64_t)((xsdt) ? (*(uint64_t *)offset): (*(uint32_t *)offset)));

		/* Make sure address is valid */
		if (!tbl) {
			continue;
		}

		print_trace("Mapped ACPI table addr = 0x%llX, ", tbl);
		print_trace("signature = 0x%X\n", tbl->signature);

		/* Verify table signature & table checksum */
		if ((tbl->signature == sig) &&
			!checksum((unsigned char *)tbl, tbl->length)) {
			/* Found the table with matched signature */
			print_trace("Found the table %s address = 0x%llX length = 0x%X\n",
				sig, tbl, tbl->length);
			return tbl;
		}
	}

	print_panic("Could not find %s table in XSDT/RSDT\n", sig);

	return NULL;
}

static acpi_table_rsdp_t *acpi_locate_rsdp(void)
{
	acpi_table_rsdp_t *rsdp;
	uint64_t hva;

	/* Try 0x0 first for getting rsdp table */
	VMM_ASSERT_EX(hmm_hpa_to_hva((uint64_t)0, &hva),
			"fail to convert hap 0 to hva\n");
	rsdp = scan_for_rsdp((void *)hva, 0x400);
	if (NULL == rsdp) {
		/* Try 0xE0000 */
		print_trace("Try 0xE0000 for ACPI RSDP table\n");
		 VMM_ASSERT_EX(hmm_hpa_to_hva((uint64_t)0xE0000, &hva),
				"fail to convert hap 0xE0000 to hva\n");
		rsdp = scan_for_rsdp((void *)hva, 0x1FFFF);
	}

	VMM_ASSERT_EX(rsdp, "Could not find the rsdp table\n");

	print_trace("rsdp address 0x%llx\n", rsdp);

	return rsdp;
}

acpi_table_header_t *acpi_locate_table(uint32_t sig)
{
	static acpi_table_rsdp_t *rsdp = NULL;
	void *table;

	if (rsdp == NULL) {
#ifdef ACPI_RSDP
#ifdef DEBUG
		rsdp = acpi_locate_rsdp();
		VMM_ASSERT_EX(rsdp == (acpi_table_rsdp_t *)ACPI_RSDP,
			"rsdp is not equal to ACPI_RSDP\n");
#else
		rsdp = (acpi_table_rsdp_t *)ACPI_RSDP;
		if (rsdp->signature != ACPI_SIG_RSD_PTR) {
			print_warn("the signature in ACPI_RSDP is not correct\n");
			rsdp = acpi_locate_rsdp();
		}
#endif
#else
		rsdp = acpi_locate_rsdp();
#endif
	}
	/* Get the specified table from rsdp */
	table = get_acpi_table_from_rsdp(rsdp, sig);

	return table;
}
