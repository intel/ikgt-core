/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "vmm_arch.h"
#include "abl_boot_param.h"
#include "stage0_lib.h"
#include "ldr_dbg.h"
#include "file_pack.h"

#include "lib/util.h"
#include "lib/string.h"

/*
 * The ABL.image is like: "0x600000@0xa000a000" which means size@addr with hexadecimal coding
 */
static boolean_t parse_region(const char *cmdline, uint32_t *base, uint32_t *size)
{
	const char *arg;

	*size = str2uint(cmdline, 10, &arg, 16);
	if ((*size == (uint32_t)-1) || (*size == 0)) {
		print_panic("Failed to parse image size!\n");
		return FALSE;
	}

	if (*arg != '@') {
		print_panic("image addr is not found!\n");
		return FALSE;
	}

	arg++;
	*base = str2uint(arg, 10, NULL, 16);
	if ((*base == (uint32_t)-1) || (*base == 0)) {
		print_panic("Failed to parse image addr!\n");
		return FALSE;
	}

	return TRUE;
}

/* The ABL.hwver is like:
 *	"51,4,1900,b00f,0,8192" which means as below:
 *      "cpu_stepping,number_of_cores,max_non_turbo_frequency,platform_id,sku,total_amount_of_memory_present"
 */
static uint32_t get_cpu_num(const char *cmdline)
{
	const char *arg;
	uint32_t cpu_num;

	/* skip cpu stepping and get cpu numbers
	 * assume cpu stepping will not longer than 5 */
	cmdline = strstr_s(cmdline, 5, ",", 1);
	if (!cmdline) {
		print_panic("CPU number not found in cmdline!\n");
		return 0;
	}

	/* now the param point to the 1st ','
	 * get cpu number from param+1 */
	cpu_num = str2uint(cmdline+1, 2, &arg, 10);
	if ((cpu_num > MAX_CPU_NUM) || (cpu_num == 0)) {
		print_panic("CPU number parse failed!\n");
		return 0;
	}

	return cpu_num;
}

/* The cmdline is present like:
 *       "ABL.image1=0x600000@0xa000a000 ABL.hwver=51,4,1900,b00f,0,8192"
 */
boolean_t cmdline_parse(multiboot_info_t *mbi, cmdline_params_t *cmdline_param)
{
	const char *cmdline;
	const char *arg;
	uint32_t addr = 0, size = 0;
	uint32_t cpu_num;
	boolean_t ret;

	if (!mbi || !cmdline_param) {
		print_panic("mbi or cmdline_params is NULL\n");
		return FALSE;
	}

	if (!CHECK_FLAG(mbi->flags, 2) || (mbi->cmdline == 0)) {
		print_panic("No cmdline info found\n");
		return FALSE;
	}

	cmdline = (const char *)(uint64_t)mbi->cmdline;

	/* Parse ABL.hwver */
	arg = strstr_s(cmdline, MAX_STR_LEN, "ABL.hwver=", sizeof("ABL.hwver=")-1);
	if (!arg) {
		print_panic("ABL.hwver not found in cmdline\n");
		return FALSE;
	}

	arg = arg + sizeof("ABL.hwver=") - 1;

	cpu_num = get_cpu_num(arg);
	if (cpu_num == 0) {
		print_panic("Failed to get cpu num!\n");
		return FALSE;
	}
	cmdline_param->cpu_num = (uint64_t)cpu_num;

	/* image1 (bzImage, must have) */
	arg = strstr_s(cmdline, MAX_STR_LEN, "ABL.image1=", sizeof("ABL.image1=")-1);
	if (!arg) {
		print_panic("ABL.image1 not found in cmdline!\n");
		return FALSE;
	}

	arg = arg + sizeof("ABL.image1=")-1;
	ret = parse_region(arg, &addr, &size);
	if (!ret) {
		print_panic("ABL.image1 params parse failed!\n");
		return FALSE;
	}
	cmdline_param->bzImage_size = (uint64_t)size;
	cmdline_param->bzImage_base = (uint64_t)addr;

	/* currently, clear linux doesn't need initrd */
	cmdline_param->initrd_size = 0;
	cmdline_param->initrd_base = 0;

	return TRUE;
}
