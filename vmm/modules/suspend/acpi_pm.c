/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
* Copyright (C) 2000 - 2007, R. Byron Moore
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

#include "hmm.h"
#include "guest.h"
#include "acpi_pm.h"
#include "dbg.h"

#include "modules/acpi.h"

/*------------------------------Types and Macros---------------------------*/
/* See "4.7.3.2.1 PM1 Control Registers" in ACPIspec30b */
#define SLP_EN(PM1x_CNT_BLK) (((PM1x_CNT_BLK) >> 13) & 0x1)
#define SLP_TYP(PM1x_CNT_BLK) (((PM1x_CNT_BLK) >> 10) & 0x7)

/* Power state values */
#define ACPI_STATE_UNKNOWN              ((uint8_t)0xFF)
#define ACPI_STATE_S0                   ((uint8_t)0)
#define ACPI_STATE_S1                   ((uint8_t)1)
#define ACPI_STATE_S2                   ((uint8_t)2)
#define ACPI_STATE_S3                   ((uint8_t)3)
#define ACPI_STATE_S4                   ((uint8_t)4)
#define ACPI_STATE_S5                   ((uint8_t)5)
#define ACPI_S_STATE_COUNT              6

#define FADT_FACS_OFFSET 36
#define FADT_DSDT_OFFSET 40
#define FADT_PM1A_CNT_BLK_OFFSET 64
#define FADT_PM1B_CNT_BLK_OFFSET 68
#define FADT_PM1_CNT_LEN_OFFSET 89
#define FADT_XFACS_OFFSET 132
#define FADT_XDSDT_OFFSET 140

#define FACS_FW_WAKING_VECTOR_OFFSET 12

typedef struct {
	uint32_t port_size;
	uint16_t port_id[ACPI_PM1_CNTRL_COUNT];
	uint8_t  sleep_type[ACPI_PM1_CNTRL_COUNT][ACPI_S_STATE_COUNT];
	uint32_t pad;
	uint32_t *p_waking_vector;
} acpi_fadt_data_t;
/*------------------------------Local Variables-------------------------------*/
acpi_fadt_data_t acpi_fadt_data;
/*------------------Forward Declarations for Local Functions---------------*/
/*-----------------------------C-Code Starts Here--------------------------*/
#ifdef DEBUG
static void acpi_print_fadt(uint8_t *fadt)
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
		*(fadt + FADT_PM1_CNT_LEN_OFFSET));
	vmm_printf(
		"===================================\n");
}
#endif

boolean_t acpi_pm_is_s3(uint16_t port_id ,uint32_t port_size,  uint32_t value)
{
	uint8_t sleep_type;
	uint32_t pm_reg_id;

	if (acpi_fadt_data.port_size != port_size) {
		return FALSE;
	}

	if (port_id == acpi_fadt_data.port_id[ACPI_PM1_CNTRL_A]) {
		pm_reg_id = ACPI_PM1_CNTRL_A;
	} else if (port_id == acpi_fadt_data.port_id[ACPI_PM1_CNTRL_B]) {
		pm_reg_id = ACPI_PM1_CNTRL_B;
	} else {
		return FALSE;
	}

	sleep_type = (uint8_t)SLP_TYP(value);

	if ((acpi_fadt_data.sleep_type[pm_reg_id][ACPI_STATE_S3] == sleep_type)
		&& (SLP_EN(value))) {
		return TRUE;
	}

	return FALSE;
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

static void acpi_parse_fadt_states(uint8_t *fadt)
{
	acpi_table_header_t *dsdt;
	uint8_t *facs;
	uint8_t *aml_ptr;
	uint8_t sstate;
	uint32_t i;

	/*get pm1x port & port szie*/
	acpi_fadt_data.port_size = *(fadt + FADT_PM1_CNT_LEN_OFFSET);
	VMM_ASSERT_EX((2 == acpi_fadt_data.port_size || 4 == acpi_fadt_data.port_size),
		"fadt pm1x port size is invalid\n");

	acpi_fadt_data.port_id[ACPI_PM1_CNTRL_A] = *((uint16_t *)(fadt + FADT_PM1A_CNT_BLK_OFFSET));
	acpi_fadt_data.port_id[ACPI_PM1_CNTRL_B] = *((uint16_t *)(fadt + FADT_PM1B_CNT_BLK_OFFSET));

	/*get waking vector address*/
	facs = (uint8_t *)(uint64_t)(*((uint32_t *)(fadt + FADT_FACS_OFFSET)));
	if (!facs) {
		facs = (uint8_t *)(*((uint64_t *)(fadt + FADT_XFACS_OFFSET)));
	}

	VMM_ASSERT_EX(facs,
		"[ACPI] FACS is not detected. S3 is not supported by the platform\n");

	acpi_fadt_data.p_waking_vector = (uint32_t *)(facs + FACS_FW_WAKING_VECTOR_OFFSET);

	/*get s3 sleep states*/
	VMM_ASSERT_EX(hmm_hpa_to_hva((uint64_t)*(uint32_t *)(fadt + FADT_DSDT_OFFSET), (uint64_t *)&dsdt),
		"[ACPI] DSDT not found\n");

	print_trace(
		"SleepState | SleepTypeA | SleepTypeB\n");
	print_trace(
		"------------------------------------\n");
	//optimize for s3
	//for (sstate = ACPI_STATE_S0; sstate < ACPI_STATE_COUNT; ++sstate) {
	for (sstate = ACPI_STATE_S3; sstate <= ACPI_STATE_S3; ++sstate) {
		aml_ptr = (uint8_t *)dsdt + sizeof(acpi_table_header_t);

		acpi_fadt_data.sleep_type[ACPI_PM1_CNTRL_A][sstate] = ACPI_STATE_UNKNOWN;
		acpi_fadt_data.sleep_type[ACPI_PM1_CNTRL_B][sstate] = ACPI_STATE_UNKNOWN;

		/* Search for '_SX_' string where 'X' is the sleep state e.g. '_S3_' */
		for (i = 0; i < dsdt->length - 8; i++) {
			if (aml_ptr[0] == '_' && aml_ptr[1] == 'S' &&
				aml_ptr[2] == ('0' + sstate) && aml_ptr[3] == '_') {
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
			acpi_fadt_data.sleep_type[ACPI_PM1_CNTRL_A][sstate] = *aml_ptr;
			aml_ptr++;

			/* Skip 'Byte Prefix' if found */
			if (*aml_ptr == 0x0a) {
				aml_ptr++;
			}

			/* This should be SLP_TYP value for PM1B_CNT_BLK */
			acpi_fadt_data.sleep_type[ACPI_PM1_CNTRL_B][sstate] = *aml_ptr;
		}

		print_trace(
			"    %3d    |    %3d     |    %3d\n", sstate,
			acpi_fadt_data.sleep_type[ACPI_PM1_CNTRL_A][sstate],
			acpi_fadt_data.sleep_type[ACPI_PM1_CNTRL_B][sstate]);
	}
}

void acpi_pm_init(acpi_fadt_info_t *p_acpi_fadt_info)
{
	uint8_t *fadt;

	D(VMM_ASSERT_EX(p_acpi_fadt_info, "[ACPI] p_acpi_fadt_info is NULL\n"));

	fadt = (uint8_t *)acpi_locate_table(ACPI_SIG_FADT);
	VMM_ASSERT_EX(fadt, "[ACPI] ERROR: No FADT table found\n");

	#ifdef DEBUG
	acpi_print_fadt(fadt);
	#endif
	acpi_parse_fadt_states(fadt);

	p_acpi_fadt_info->port_id[ACPI_PM1_CNTRL_A] = acpi_fadt_data.port_id[ACPI_PM1_CNTRL_A];
	p_acpi_fadt_info->port_id[ACPI_PM1_CNTRL_B] = acpi_fadt_data.port_id[ACPI_PM1_CNTRL_B];
	p_acpi_fadt_info->p_waking_vector = acpi_fadt_data.p_waking_vector;

}
