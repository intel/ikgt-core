/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * Copyright (c) 2000-2007 R. Byron Moore.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
 *
 */

#include "hmm.h"
#include "guest.h"
#include "acpi_pm.h"
#include "dbg.h"
#include "heap.h"
#include "lib/util.h"

#include "modules/acpi.h"

/*------------------------------Types and Macros---------------------------*/
/* See "4.7.3.2.1 PM1 Control Registers" in ACPIspec30b */
#define SLP_EN(PM1x_CNT_BLK) (((PM1x_CNT_BLK) >> 13) & 0x1)
#define SLP_TYP(PM1x_CNT_BLK) (((PM1x_CNT_BLK) >> 10) & 0x7)

/* Power state values */
#define ACPI_STATE_S0                   ((uint8_t)0)
#define ACPI_STATE_S1                   ((uint8_t)1)
#define ACPI_STATE_S2                   ((uint8_t)2)
#define ACPI_STATE_S3                   ((uint8_t)3)
#define ACPI_STATE_S4                   ((uint8_t)4)
#define ACPI_STATE_S5                   ((uint8_t)5)
#define ACPI_S_STATE_COUNT              6

/* FADT Offset in bytes */
#define FADT_FACS_OFFSET                    36U
#define FADT_DSDT_OFFSET                    40U
#define FADT_PM1A_CNT_BLK_OFFSET            64U
#define FADT_PM1B_CNT_BLK_OFFSET            68U
#define FADT_PM1_CNT_LEN_OFFSET             89U
#define FADT_FLAGS                         112U
#define FADT_XFACS_OFFSET                  132U
#define FADT_XDSDT_OFFSET                  140U
#define FADT_X_PM1A_CNT_BLK_OFFSET         172U
#define FADT_X_PM1B_CNT_BLK_OFFSET         184U

/* FACS Offset in bytes */
#define FACS_FW_WAKING_VECTOR_OFFSET        12U
#define FACS_X_FW_WAKING_VECTOR_OFFSET      24U

typedef struct {
	acpi_generic_address_t *reg;
	uint8_t sleep_type[ACPI_S_STATE_COUNT];
} PACKED pm1_t;

typedef struct {
	pm1_t pm1[ACPI_PM1_CNT_NUM];
	uint32_t *p_waking_vector;
	uint64_t *p_x_waking_vector;
} PACKED acpi_fadt_data_t;

/*------------------------------Local Variables-------------------------------*/
static acpi_fadt_data_t acpi_fadt_data;
/*-----------------------------C-Code Starts Here--------------------------*/
#ifdef DEBUG
static void acpi_print_fadt(uint64_t fadt)
{
	vmm_printf(
		"===============FADT================\n");
	acpi_print_header((acpi_table_header_t *)fadt);
	vmm_printf("facs=0x%llX, dsdt=0x%llX, xfacs=0x%llX, xdsdt=0x%llX\n",
		*(uint32_t *)(fadt + FADT_FACS_OFFSET), *(uint32_t *)(fadt + FADT_DSDT_OFFSET),
		*(uint64_t *)(fadt + FADT_XFACS_OFFSET), *(uint64_t *)(fadt + FADT_XDSDT_OFFSET));
	vmm_printf("pm1a_control_block=0x%x, pm1b_control_block=0x%x, pm1_control_length=0x%x\n",
		*(uint32_t *)(fadt + FADT_PM1A_CNT_BLK_OFFSET),
		*(uint32_t *)(fadt + FADT_PM1B_CNT_BLK_OFFSET),
		*(uint8_t *)(fadt + FADT_PM1_CNT_LEN_OFFSET));
	vmm_printf("x_pm1a_control_block=0x%x, x_pm1b_control_block=0x%x\n",
		*(uint32_t *)(fadt + FADT_X_PM1A_CNT_BLK_OFFSET),
		*(uint32_t *)(fadt + FADT_X_PM1B_CNT_BLK_OFFSET))
	vmm_printf(
		"===================================\n");
}
#endif

boolean_t acpi_pm_is_s3(uint64_t addr, uint32_t size, uint32_t value)
{
	uint8_t sleep_type, sleep_en;
	uint32_t i;
	pm1_t *pm1x;

	sleep_en = (uint8_t)SLP_EN(value);
	sleep_type = (uint8_t)SLP_TYP(value);

	/* according to ACPI spec, the register is accessed through byte or word.  */
	if (size != 1 && size != 2)
		return FALSE;

	if (!sleep_en)
		return FALSE;

	for (i = 0; i < ACPI_PM1_CNT_NUM; i++) {
		pm1x = &acpi_fadt_data.pm1[i];
		if (!pm1x->reg)
			continue;
		if (addr == pm1x->reg->addr) {
			if (sleep_type == pm1x->sleep_type[ACPI_STATE_S3])
				return TRUE;
		}
	}

	return FALSE;
}

static void acpi_parse_pm1x_reg(uint8_t *fadt, uint32_t offset, uint32_t x_offset, acpi_generic_address_t **pm1x_cnt_reg)
{
	uint8_t port_len = 0U;
	acpi_generic_address_t *x_pm1x_cnt_blk;
	acpi_generic_address_t null_addr = { 0U };
	uint64_t addr = 0ULL;

	x_pm1x_cnt_blk = (void *)(fadt + x_offset);
	/*
	 * According to ACPI Spec Chapter 5.2.9:
	 *     If X_PM1a_CNT_BLK field contains a non zero value which can be used by the OSPM,
	 *     then PM1a_CNT_BLK must be ignored by the OSPM.
	 */
	if (memcmp(x_pm1x_cnt_blk, &null_addr, sizeof(acpi_generic_address_t)) != 0U) {
		(*pm1x_cnt_reg) = mem_alloc(sizeof(acpi_generic_address_t));
		memcpy(*pm1x_cnt_reg, x_pm1x_cnt_blk, sizeof(acpi_generic_address_t));
	} else {
		addr = *((uint16_t *)(void *)(fadt + offset));
		if (!addr)
			return;
		*pm1x_cnt_reg = mem_alloc(sizeof(acpi_generic_address_t));
		port_len = (*(fadt + FADT_PM1_CNT_LEN_OFFSET));
		VMM_ASSERT_EX(port_len >= 2U, "fadt pm1x port size is invalid\n");

		(*pm1x_cnt_reg)->as_id = ACPI_GAS_ID_IO;
		(*pm1x_cnt_reg)->reg_bit_width = port_len * 8U;
		(*pm1x_cnt_reg)->reg_bit_off = 0U;
		(*pm1x_cnt_reg)->access_size = ACPI_GAS_AS_WORD;
		(*pm1x_cnt_reg)->addr = *((uint16_t *)(void *)(fadt + offset));
	}
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
 *                           If 0, then only the first byte is used
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

static void acpi_parse_fadt_states(uint8_t *fadt)
{
	acpi_table_header_t *dsdt;
	uint8_t *aml_ptr;
	uint8_t *end;
	uint8_t sstate;

	/*get s3 sleep states*/
	VMM_ASSERT_EX(hmm_hpa_to_hva((uint64_t)*(uint32_t *)(void *)(fadt + FADT_DSDT_OFFSET), (uint64_t *)&dsdt),
		"[ACPI] DSDT not found\n");

	print_trace(
		"SleepState | SleepTypeA | SleepTypeB\n");
	print_trace(
		"------------------------------------\n");
	//optimize for s3
	//for (sstate = ACPI_STATE_S0; sstate < ACPI_STATE_COUNT; ++sstate) {
	for (sstate = ACPI_STATE_S3; sstate <= ACPI_STATE_S3; ++sstate) {
		aml_ptr = (uint8_t *)dsdt + sizeof(acpi_table_header_t);
		end = (uint8_t *)dsdt + dsdt->length;

		/* Search for '_SX_' string where 'X' is the sleep state e.g. '_S3_' */
		while (aml_ptr < (end - 8)) { // aml_ptr will move forward at least 8 bytes from here
			if (aml_ptr[0] == '_' && aml_ptr[1] == 'S' &&
				aml_ptr[2] == ('0' + sstate) && aml_ptr[3] == '_') {
				break;
			}
			aml_ptr++;
		}

		VMM_ASSERT_EX(aml_ptr < (end - 8), "Could not find the SLP_TYP value in DSDT table\n");

		/* Skip '_SX_' and Package Op */
		aml_ptr += 5;

		/* Skip 'Package length' bytes indicated by the 2 high bits of
		 * 'Package Lead' byte */
		aml_ptr += (*aml_ptr >> 6);
		/* aml_ptr will move forward at least 3 bytes from here */
		VMM_ASSERT_EX(aml_ptr < (end - 3),
			"search for SLP_TYP values is out of DSDT table\n");

		/* Skip 'Package Lead' byte */
		aml_ptr++;

		/* Skip 'Number of Elements' byte */
		aml_ptr++;

		/* Skip 'Byte Prefix' if found */
		if (*aml_ptr == 0x0a) {
			aml_ptr++;
		}

		/* This should be SLP_TYP value for PM1A_CNT_BLK */
		acpi_fadt_data.pm1[ACPI_PM1A_CNT].sleep_type[sstate] = *aml_ptr;
		aml_ptr++;

		/* Skip 'Byte Prefix' if found */
		if (*aml_ptr == 0x0a) {
			aml_ptr++;
		}

		/* This should be SLP_TYP value for PM1B_CNT_BLK */
		acpi_fadt_data.pm1[ACPI_PM1B_CNT].sleep_type[sstate] = *aml_ptr;

		print_trace("   %3d   |    %3d    |    %3d\n", sstate,
			acpi_fadt_data.pm1[ACPI_PM1A_CNT].sleep_type[sstate],
			acpi_fadt_data.pm1[ACPI_PM1B_CNT].sleep_type[sstate]);
	}
}

static void acpi_parse_fadt_waking_vector(uint8_t *fadt)
{
	uint8_t *facs;

	/* get XFACS/FACS address */
	facs = (uint8_t *)(*((uint64_t *)(void *)(fadt + FADT_XFACS_OFFSET)));
	if (facs == 0)
		facs = (uint8_t *)(uint64_t)(*((uint32_t *)(void *)(fadt + FADT_FACS_OFFSET)));

	VMM_ASSERT_EX(facs,
		"[ACPI] FACS is not detected. S3 is not supported by the platform\n");

	acpi_fadt_data.p_waking_vector = (uint32_t *)(void *)(uint64_t)(uint32_t)(facs + FACS_FW_WAKING_VECTOR_OFFSET);
	acpi_fadt_data.p_x_waking_vector = (uint64_t *)(void *)(uint64_t)(facs + FACS_X_FW_WAKING_VECTOR_OFFSET);
}

acpi_generic_address_t *get_pm1x_reg(uint32_t id)
{
	if (id != ACPI_PM1A_CNT && id != ACPI_PM1B_CNT)
		return NULL;

	return acpi_fadt_data.pm1[id].reg;
}

uint32_t *get_waking_vector(void)
{
	/*
	 * NOTE: Currently, we do not support setting waking vector through X_FW_WAKING_VECTOR.
	 *       We may need to revisit this in future and support 32/64 bit resume entry.
	 */
	VMM_ASSERT_EX(*acpi_fadt_data.p_x_waking_vector == 0ULL, "FATAL: X Waking Vector is not supported!\n");
	VMM_ASSERT_EX(*acpi_fadt_data.p_waking_vector != 0U, "FATAL: Waking Vector is NULL!\n");
	return acpi_fadt_data.p_waking_vector;
}

void acpi_pm_init(void)
{
	acpi_table_header_t *fadt;

	fadt = acpi_locate_table(ACPI_SIG_FADT);
	VMM_ASSERT_EX(fadt, "[ACPI] ERROR: No FADT table found\n");

#ifdef DEBUG
	acpi_print_fadt((uint64_t)fadt);
#endif

	acpi_parse_pm1x_reg((uint8_t *)fadt, FADT_PM1A_CNT_BLK_OFFSET, FADT_X_PM1A_CNT_BLK_OFFSET, &acpi_fadt_data.pm1[ACPI_PM1A_CNT].reg);
	VMM_ASSERT_EX(acpi_fadt_data.pm1[ACPI_PM1A_CNT].reg, "[SUSPEND] X_PM1A_CNT/PM1A_CNT block register is missing!");
	acpi_parse_pm1x_reg((uint8_t *)fadt, FADT_PM1B_CNT_BLK_OFFSET, FADT_X_PM1B_CNT_BLK_OFFSET, &acpi_fadt_data.pm1[ACPI_PM1B_CNT].reg);
	acpi_parse_fadt_states((uint8_t *)fadt);
	acpi_parse_fadt_waking_vector((uint8_t *)fadt);
}
