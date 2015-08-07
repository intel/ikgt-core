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

#include "mon_defs.h"
#include "mon_dbg.h"
#include "libc.h"
#include "host_memory_manager_api.h"
#include "file_codes.h"
#include "mon_acpi.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(MON_ACPI_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(MON_ACPI_C, __condition)

static acpi_table_fadt_t fadt;    /* locally stored FADT */
static char
	sleep_conversion_table[ACPI_PM1_CNTRL_REG_COUNT][ACPI_S_STATE_COUNT] = { 0 };


void mon_acpi_print_header(acpi_table_header_t *table_header)
{
	MON_LOG(mask_anonymous, level_trace,
		"==============header===============\n");
	MON_LOG(mask_anonymous, level_trace, "signature     = %c%c%c%c\n",
		table_header->signature[0], table_header->signature[1],
		table_header->signature[2], table_header->signature[3]);
	MON_LOG(mask_anonymous, level_trace, "length        = 0x%x\n",
		table_header->length);
	MON_LOG(mask_anonymous, level_trace, "revision      = %d\n",
		table_header->revision);
	MON_LOG(mask_anonymous, level_trace, "check_sum      = 0x%x\n",
		table_header->check_sum);
	MON_LOG(mask_anonymous, level_trace, "oem_id         = %c%c%c%c%c%c\n",
		table_header->oem_id[0], table_header->oem_id[1],
		table_header->oem_id[2], table_header->oem_id[3],
		table_header->oem_id[4], table_header->oem_id[5]);
	MON_LOG(mask_anonymous,
		level_trace,
		"oem_table_id    = %c%c%c%c%c%c%c%c\n",
		table_header->oem_table_id[0],
		table_header->oem_table_id[1],
		table_header->oem_table_id[2],
		table_header->oem_table_id[3],
		table_header->oem_table_id[4],
		table_header->oem_table_id[5],
		table_header->oem_table_id[6],
		table_header->oem_table_id[7]);
	MON_LOG(mask_anonymous, level_trace, "oem_revision   = %d\n",
		table_header->oem_revision);
	MON_LOG(mask_anonymous, level_trace, "asl_compiler_id = %c%c%c%c\n",
		table_header->asl_compiler_id[0],
		table_header->asl_compiler_id[1],
		table_header->asl_compiler_id[2],
		table_header->asl_compiler_id[3]);
	MON_LOG(mask_anonymous, level_trace, "asl_compiler_revision= %d\n",
		table_header->asl_compiler_revision);
	MON_LOG(mask_anonymous, level_trace,
		"-----------------------------------\n");
}

void mon_acpi_print_fadt(acpi_table_fadt_t *fadt)
{
	MON_LOG(mask_anonymous, level_trace,
		"===============FADT================\n");
	mon_acpi_print_header(&fadt->header);
	MON_LOG(mask_anonymous, level_trace, "facs              : %p\n",
		fadt->facs);
	MON_LOG(mask_anonymous, level_trace, "dsdt              : %p\n",
		fadt->dsdt);
	MON_LOG(mask_anonymous, level_trace, "model             : %d\n",
		fadt->model);
	MON_LOG(mask_anonymous, level_trace, "preferred_profile  : %d\n",
		fadt->preferred_profile);
	MON_LOG(mask_anonymous, level_trace, "sci_interrupt      : 0x%x\n",
		fadt->sci_interrupt);
	MON_LOG(mask_anonymous, level_trace, "smi_command        : 0x%x\n",
		fadt->smi_command);
	MON_LOG(mask_anonymous, level_trace, "acpi_enable        : 0x%x\n",
		fadt->acpi_enable);
	MON_LOG(mask_anonymous, level_trace, "acpi_disable       : 0x%x\n",
		fadt->acpi_disable);
	MON_LOG(mask_anonymous, level_trace, "s4_bios_request     : 0x%x\n",
		fadt->s4_bios_request);
	MON_LOG(mask_anonymous, level_trace, "pstate_control     : 0x%x\n",
		fadt->pstate_control);
	MON_LOG(mask_anonymous, level_trace, "pm1a_event_block    : 0x%x\n",
		fadt->pm1a_event_block);
	MON_LOG(mask_anonymous, level_trace, "pm1a_event_block    : 0x%x\n",
		fadt->pm1a_event_block);
	MON_LOG(mask_anonymous, level_trace, "pm1b_event_block    : 0x%x\n",
		fadt->pm1b_event_block);
	MON_LOG(mask_anonymous, level_trace, "pm1a_control_block  : 0x%x\n",
		fadt->pm1a_control_block);
	MON_LOG(mask_anonymous, level_trace, "pm1b_control_block  : 0x%x\n",
		fadt->pm1b_control_block);
	MON_LOG(mask_anonymous, level_trace, "pm2_control_block   : 0x%x\n",
		fadt->pm2_control_block);
	MON_LOG(mask_anonymous, level_trace, "pm_timer_block      : 0x%x\n",
		fadt->pm_timer_block);
	MON_LOG(mask_anonymous, level_trace, "gpe0_block         : 0x%x\n",
		fadt->gpe0_block);
	MON_LOG(mask_anonymous, level_trace, "gpe1_block         : 0x%x\n",
		fadt->gpe1_block);
	MON_LOG(mask_anonymous, level_trace, "pm1_event_length    : 0x%x\n",
		fadt->pm1_event_length);
	MON_LOG(mask_anonymous, level_trace, "pm1_control_length  : 0x%x\n",
		fadt->pm1_control_length);
	MON_LOG(mask_anonymous, level_trace, "pm2_control_length  : 0x%x\n",
		fadt->pm2_control_length);
	MON_LOG(mask_anonymous, level_trace, "pm_timer_length     : 0x%x\n",
		fadt->pm_timer_length);
	MON_LOG(mask_anonymous, level_trace, "gpe0_block_length   : 0x%x\n",
		fadt->gpe0_block_length);
	MON_LOG(mask_anonymous, level_trace, "gpe1_block_length   : 0x%x\n",
		fadt->gpe1_block_length);
	MON_LOG(mask_anonymous, level_trace, "gpe1_base          : 0x%x\n",
		fadt->gpe1_base);
	MON_LOG(mask_anonymous, level_trace, "cst_control        : 0x%x\n",
		fadt->cst_control);
	MON_LOG(mask_anonymous, level_trace, "c2_latency         : 0x%x\n",
		fadt->c2_latency);
	MON_LOG(mask_anonymous, level_trace, "c3_latency         : 0x%x\n",
		fadt->c3_latency);
	MON_LOG(mask_anonymous, level_trace, "flush_size         : 0x%x\n",
		fadt->flush_size);
	MON_LOG(mask_anonymous, level_trace, "flush_stride       : 0x%x\n",
		fadt->flush_stride);
	MON_LOG(mask_anonymous, level_trace, "duty_offset        : 0x%x\n",
		fadt->duty_offset);
	MON_LOG(mask_anonymous, level_trace, "duty_width         : 0x%x\n",
		fadt->duty_width);
	MON_LOG(mask_anonymous, level_trace, "day_alarm          : 0x%x\n",
		fadt->day_alarm);
	MON_LOG(mask_anonymous, level_trace, "month_alarm        : 0x%x\n",
		fadt->month_alarm);
	MON_LOG(mask_anonymous, level_trace, "century           : 0x%x\n",
		fadt->century);
	MON_LOG(mask_anonymous, level_trace, "boot_flags         : 0x%x\n",
		fadt->boot_flags);
	MON_LOG(mask_anonymous, level_trace, "flags             : 0x%x\n",
		fadt->flags);
	MON_LOG(mask_anonymous, level_trace, "reset_register     : 0x%x\n",
		fadt->reset_register);
	MON_LOG(mask_anonymous, level_trace, "reset_value        : 0x%x\n",
		fadt->reset_value);
	MON_LOG(mask_anonymous, level_trace, "xfacs             : 0x%x\n",
		fadt->xfacs);
	MON_LOG(mask_anonymous, level_trace, "xdsdt             : 0x%x\n",
		fadt->xdsdt);
	MON_LOG(mask_anonymous, level_trace, "xpm1a_event_block   : 0x%x\n",
		fadt->xpm1a_event_block);
	MON_LOG(mask_anonymous, level_trace, "xpm1b_event_block   : 0x%x\n",
		fadt->xpm1b_event_block);
	MON_LOG(mask_anonymous, level_trace, "xpm1a_control_block : 0x%x\n",
		fadt->xpm1a_control_block);
	MON_LOG(mask_anonymous, level_trace, "xpm1b_control_block : 0x%x\n",
		fadt->xpm1b_control_block);
	MON_LOG(mask_anonymous, level_trace, "xpm2c_control_block  : 0x%x\n",
		fadt->xpm2c_control_block);
	MON_LOG(mask_anonymous, level_trace, "xpm2c_control_block  : 0x%x\n",
		fadt->xpm2c_control_block);
	MON_LOG(mask_anonymous, level_trace, "xpm_timer_block     : 0x%x\n",
		fadt->xpm_timer_block);
	MON_LOG(mask_anonymous, level_trace, "xgpe0_block        : 0x%x\n",
		fadt->xgpe0_block);
	MON_LOG(mask_anonymous, level_trace, "xgpe1_block        : 0x%x\n",
		fadt->xgpe1_block);
	MON_LOG(mask_anonymous, level_trace,
		"===================================\n");
}

void mon_acpi_print_facs(acpi_table_facs_t *facs)
{
	MON_LOG(mask_anonymous, level_trace,
		"===============FACS================\n");
	MON_LOG(mask_anonymous, level_trace, "signature         : %c%c%c%c\n",
		facs->signature[0], facs->signature[1], facs->signature[2],
		facs->signature[3]);
	MON_LOG(mask_anonymous, level_trace, "length                : %d\n",
		facs->length);
	MON_LOG(mask_anonymous, level_trace, "hardware_signature     : 0x%x\n",
		facs->hardware_signature);
	MON_LOG(mask_anonymous, level_trace, "firmware_waking_vector  : 0x%x\n",
		facs->firmware_waking_vector);
	MON_LOG(mask_anonymous, level_trace, "flags                 : 0x%x\n",
		facs->flags);
	MON_LOG(mask_anonymous, level_trace, "xfirmware_waking_vector : 0x%x\n",
		facs->xfirmware_waking_vector);
	MON_LOG(mask_anonymous, level_trace, "version               : %d\n",
		facs->version);
}


INLINE void_t *acpi_map_memory(uint64_t where)
{
	hva_t hva;

	mon_hmm_hpa_to_hva((hpa_t)where, &hva);

	return (void *)hva;
}

/* Calculate acpi table checksum */
INLINE uint8_t checksum(uint8_t *buffer, uint32_t length)
{
	int sum = 0;
	uint8_t *i = buffer;

	buffer += length;
	for (; i < buffer; sum += *(i++))
		;
	return (char)sum;
}

/* Scan for RSDP table and return mapped address of rsdp, if found */
INLINE acpi_table_rsdp_t *scan_for_rsdp(void *addr, uint32_t length)
{
	acpi_table_rsdp_t *rsdp, *result = NULL;
	unsigned char *begin;
	unsigned char *i, *end;

	begin = addr;
	end = begin + length;

	/* Search from given start address for the requested length */
	for (i = begin; i < end; i += ACPI_RSDP_SCAN_STEP) {
		/* The signature and checksum must both be correct */
		if (mon_memcmp((char *)i, "RSD PTR ", 8)) {
			continue;
		}

		MON_LOG(mask_anonymous, level_trace,
			"Got the rsdp header, now check the checksum\n");
		rsdp = (acpi_table_rsdp_t *)i;

		/* signature matches, check the appropriate checksum */
		if (!checksum((unsigned char *)rsdp, (rsdp->revision < 2) ?
			    ACPI_RSDP_CHECKSUM_LENGTH :
			    ACPI_RSDP_XCHECKSUM_LENGTH)) {
			/* check_sum valid, we have found a valid RSDP */
			MON_LOG(mask_anonymous,
				level_trace,
				"Found acpi rsdp table\n");
			result = rsdp;
			break;
		}
	}

	return result;
}

/* Find an acpi table with specified signature and return mapped address */
INLINE acpi_table_header_t *get_acpi_table_from_rsdp(acpi_table_rsdp_t *rsdp,
						     char *sig)
{
	acpi_table_header_t *sdt = NULL;
	acpi_table_header_t *tbl = NULL;
	int xsdt = 1;
	int i;
	int num;
	char *offset;

	/* Get xsdt pointer */
	if (rsdp->revision > 1 && rsdp->xsdt_physical_address) {
		MON_LOG(mask_anonymous,
			level_trace,
			"rsdp->xsdt_physical_address %lx\n",
			rsdp->xsdt_physical_address);
		sdt = acpi_map_memory(rsdp->xsdt_physical_address);
	}

	/* Or get rsdt */
	if (!sdt && rsdp->rsdt_physical_address) {
		xsdt = 0;
		MON_LOG(mask_anonymous, level_trace,
			"rsdp->rsdt_physical_address  = %x\n",
			rsdp->rsdt_physical_address);
		sdt = acpi_map_memory(rsdp->rsdt_physical_address);
	}

	/* Check if the rsdt/xsdt table pointer is NULL */
	if (NULL == sdt) {
		MON_LOG(mask_anonymous, level_error, "map rsdt/xsdt error\n");
		return NULL;
	}

	/* Make sure the table checksum is correct */
	if (checksum((unsigned char *)sdt, sdt->length)) {
		MON_LOG(mask_anonymous, level_error, "Wrong checksum in %s!\n",
			(xsdt) ? "XSDT" : "RSDT");
		return NULL;
	}

	MON_LOG(mask_anonymous, level_trace, "xsdt/rsdt checksum verified!\n");

	/* Calculate the number of table pointers in the xsdt or rsdt table */
	num = (sdt->length - sizeof(acpi_table_header_t)) /
	      ((xsdt) ? sizeof(uint64_t) : sizeof(uint32_t));

	MON_LOG(mask_anonymous, level_trace,
		"The number of table pointers in xsdt/rsdt = %d\n", num);

	/* Get to the table pointer area */
	offset = (char *)sdt + sizeof(acpi_table_header_t);

	/* Traverse the pointer list to get the desired acpi table */
	for (i = 0; i < num; ++i,
	     offset += ((xsdt) ? sizeof(uint64_t) : sizeof(uint32_t))) {
		/* Get the address from the pointer entry */
		tbl = acpi_map_memory((uint64_t)
			((xsdt) ? (*(uint64_t *)offset)
			 : (*(uint32_t *)offset)));

		/* Make sure address is valid */
		if (!tbl) {
			continue;
		}

		MON_LOG(mask_anonymous,
			level_trace,
			"Mapped ACPI table addr = %p, ",
			tbl);
		MON_LOG(mask_anonymous, level_trace, "signature = %c%c%c%c\n",
			tbl->signature[0], tbl->signature[1], tbl->signature[2],
			tbl->signature[3]);

		/* Verify table signature & table checksum */
		if ((0 == mon_memcmp(tbl->signature, sig, 4)) &&
		    !checksum((unsigned char *)tbl, tbl->length)) {
			/* Found the table with matched signature */
			MON_LOG(mask_anonymous,
				level_trace,
				"Found the table %s address = %p length = %x\n",
				sig,
				tbl,
				tbl->length);

			return tbl;
		}
	}

	MON_LOG(mask_anonymous, level_error,
		"Could not find %s table in XSDT/RSDT\n", sig);

	return NULL;
}

acpi_table_header_t *mon_acpi_locate_table(char *sig)
{
	acpi_table_rsdp_t *rsdp = NULL;
	void *table = NULL;

	/* Try 0x0 first for getting rsdp table */
	rsdp = scan_for_rsdp(acpi_map_memory(0), 0x400);
	if (NULL == rsdp) {
		/* Try 0xE0000 */
		MON_LOG(mask_anonymous, level_trace,
			"Try 0xE0000 for ACPI RSDP table\n");
		rsdp = scan_for_rsdp(acpi_map_memory(0xE0000), 0x1FFFF);
	}

	if (NULL == rsdp) {
		MON_LOG(mask_anonymous,
			level_error,
			"Could not find the rsdp table\n");
		return NULL;
	}

	MON_LOG(mask_anonymous, level_trace, "rsdp address %p\n", rsdp);

	/* Get the specified table from rsdp */
	table = get_acpi_table_from_rsdp(rsdp, sig);

	return table;
}

/*
 * SLP_TYP values are programmed in PM1A and PM1B control block registers
 * to initiate power transition.  Each Sx state has a corresponding SLP_TYP
 * value. SLP_TYP values are stored in DSDT area of ACPI tables as AML packages
 * Following code searches for these packages to retreive the SLP_TYPs
 *
 * Search for '_SX_' to get to start of package.  'X' stands for sleep state
 * e.g. '_S3_'. If '_SX_' is not found then it means the system does not support
 * that sleep state.
 *
 * _SX_packages are in the following format
 *
 * 1 byte : Package Op (0x12)
 *
 * 1 byte
 * + 'Package length' size : 'Package length' field.  Refer ACPI spec for
 *                           'Package length Encoding' High 2 bits of first
 *                           byte indicates how many bytes are used by
 *                           'Package length'
 * If 0, then only the first byte is used
 *                           If > 0 then following bytes (max 3) will be also
 *                           used
 *
 * 1 byte : 'Number of Elements'
 *
 * 1 byte optional         : There may be an optional 'Byte Prefix' (0x0A)
 *                           present.
 *
 * 1 byte SLP_TYP_A : SLP_TYP value for PM1A control block
 *
 * 1 byte optional         : There may be an optional 'Byte Prefix' (0x0A)
 *                           present.
 *
 * 1 byte SLP_TYP_B : SLP_TYP value for PM1B control block
 *
 * Remaining bytes are ignored. */
void mon_acpi_retrieve_sleep_states(void)
{
	acpi_table_header_t *dsdt;
	char *aml_ptr;
	uint8_t sstate;
	uint32_t i;

	dsdt = acpi_map_memory((uint64_t)fadt.dsdt);
	if (!dsdt) {
		MON_LOG(mask_anonymous, level_error, "[ACPI] DSDT not found\n");
		return;
	}

	MON_LOG(mask_anonymous, level_trace,
		"SleepState | SleepTypeA | SleepTypeB\n");
	MON_LOG(mask_anonymous, level_trace,
		"------------------------------------\n");

	for (sstate = ACPI_STATE_S0; sstate < ACPI_S_STATE_COUNT; ++sstate) {
		aml_ptr = (char *)(dsdt + sizeof(acpi_table_header_t));

		sleep_conversion_table[ACPI_PM1_CNTRL_REG_A][sstate] = 0xff;
		sleep_conversion_table[ACPI_PM1_CNTRL_REG_B][sstate] = 0xff;

		/* Search for '_SX_' string where 'X' is the sleep state e.g. '_S3_' */
		for (i = 0; i < dsdt->length - 8; i++) {
			if (aml_ptr[0] == '_' && aml_ptr[1] == 'S'
			    && aml_ptr[2] == ('0' + sstate) && aml_ptr[3] ==
			    '_') {
				break;
			}
			aml_ptr++;
		}
		if (i < dsdt->length - 8) {
			/* Skip '_SX_' and Package Op */
			aml_ptr += 5;

			/* Skip 'Package length' bytes indicated by the 2 high bits of
			 * 'Package Lead' byte */
			aml_ptr += (*aml_ptr >> 6);

			/* Skip 'Package Lead' byte */
			aml_ptr++;

			/* Skip 'Number of Elements' byte */
			aml_ptr++;

			/* Skip 'Byte Prefix' if found */
			if (*aml_ptr == 0x0a) {
				aml_ptr++;
			}

			/* This should be SLP_TYP value for PM1A_CNT_BLK */
			sleep_conversion_table[ACPI_PM1_CNTRL_REG_A][sstate] =
				*aml_ptr;
			aml_ptr++;

			/* Skip 'Byte Prefix' if found */
			if (*aml_ptr == 0x0a) {
				aml_ptr++;
			}

			/* This should be SLP_TYP value for PM1B_CNT_BLK */
			sleep_conversion_table[ACPI_PM1_CNTRL_REG_B][sstate] =
				*aml_ptr;
		}

		MON_LOG(mask_anonymous, level_trace,
			"    %3d    |    %3d     |    %3d\n", sstate,
			sleep_conversion_table[ACPI_PM1_CNTRL_REG_A][sstate],
			sleep_conversion_table[ACPI_PM1_CNTRL_REG_B][sstate]);
	}
}

boolean_t mon_acpi_init(hva_t fadt_hva)
{
	acpi_table_header_t *table;

	if (fadt_hva) {
		table = (acpi_table_header_t *)fadt_hva;
	} else {
		table = mon_acpi_locate_table(ACPI_SIG_FADT);
	}

	if (NULL != table) {
		/* Keep a local copy of fadt to avoid losing the tables if OS
		 * re-uses acpi memory */
		fadt = *(acpi_table_fadt_t *)table;
		mon_acpi_print_fadt((acpi_table_fadt_t *)table);
		/* Get Sleep Data */
		mon_acpi_retrieve_sleep_states();

		return TRUE;
	} else {
		MON_LOG(mask_anonymous,
			level_trace,
			"ERROR: No FADT table found\n");
		return FALSE;
	}
}

uint16_t mon_acpi_smi_cmd_port(void)
{
	return (uint16_t)fadt.smi_command;
}

uint8_t mon_acpi_pm_port_size(void)
{
	return fadt.pm1_control_length;
}

uint32_t mon_acpi_pm_port_a(void)
{
	return fadt.pm1a_control_block;
}

uint32_t mon_acpi_pm_port_b(void)
{
	return fadt.pm1b_control_block;
}

unsigned mon_acpi_sleep_type_to_state(unsigned pm_reg_id, unsigned sleep_type)
{
	int sstate;

	if (pm_reg_id >= ACPI_PM1_CNTRL_REG_COUNT) {
		MON_LOG(mask_anonymous, level_error,
			"[ACPI] got bad input. pm_reg_id(%d) sleep_type(%d)\n",
			pm_reg_id, sleep_type);
		return 0;
	}

	for (sstate = ACPI_STATE_S0; sstate < ACPI_S_STATE_COUNT; ++sstate) {
		if (sleep_conversion_table[pm_reg_id][sstate] ==
		    (char)sleep_type) {
			/* found */
			return sstate;
		}
	}

	MON_LOG(mask_anonymous,
		level_error,
		"[ACPI] got bad input. pm_reg_id(%d) sleep_type(%d)\n",
		pm_reg_id,
		sleep_type);
	/* sleep_type not recognized */
	return 0;
}

static acpi_table_facs_t *mon_acpi_facs_table(acpi_table_fadt_t *fadt_table)
{
	acpi_table_facs_t *p_facs;

	p_facs = (acpi_table_facs_t *)(size_t)fadt_table->facs;
	if (NULL == p_facs) {
		p_facs = (acpi_table_facs_t *)fadt_table->xfacs;
	}

	return p_facs;
}

int mon_acpi_waking_vector(uint32_t *p_waking_vector,
			   uint64_t *p_extended_waking_vector)
{
	acpi_table_facs_t *p_facs;

	p_facs = mon_acpi_facs_table(&fadt);

	if (NULL == p_facs) {
		MON_LOG(mask_anonymous,
			level_error,
			"[acpi] FACS is not detected. S3 is not supported by the platform\n");
		return -1;
	}

	MON_LOG(mask_anonymous,
		level_trace,
		"[acpi] firmware_waking_vector=%P  xfirmware_waking_vector=%P\n",
		p_facs->firmware_waking_vector,
		p_facs->xfirmware_waking_vector);

	*p_waking_vector = p_facs->firmware_waking_vector;
	*p_extended_waking_vector = p_facs->xfirmware_waking_vector;
	/* OK */
	return 0;
}

int mon_acpi_facs_flag(uint32_t *flags, uint32_t *ospm_flags)
{
	acpi_table_facs_t *p_facs;

	p_facs = mon_acpi_facs_table(&fadt);

	if (NULL == p_facs) {
		MON_LOG(mask_anonymous,
			level_error,
			"[acpi] FACS is not detected. S3 is not supported by the platform\n");
		return -1;
	}

	MON_LOG(mask_anonymous, level_trace,
		"[acpi] p_facs->flags=%P  p_facs->ospm_flags=%P\n",
		p_facs->flags, p_facs->ospm_flags);

	*flags = p_facs->flags;
	*ospm_flags = p_facs->ospm_flags;
	/* OK */
	return 0;
}
