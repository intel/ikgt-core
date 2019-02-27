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
 * The argument is like: "image1=0x600000@0xa000a000" which means size@addr with hexadecimal coding
 */
static boolean_t parse_region(const char *cmdline, const char *str, uint32_t *base, uint32_t *size)
{
	const char *arg;
	uint32_t len = strnlen_s(str, MAX_STR_LEN);

	arg = strstr_s(cmdline, MAX_STR_LEN, str, len);
	if (!arg) {
		print_panic("%s: %s not found in cmdline\n", __func__, str);
		return FALSE;
	}

	arg += len;
	*size = str2uint(arg, 10, &arg, 16);
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

#define ABL_VER_STR     "ABL.hwver="
#define ABL_VER_STR_LEN (sizeof(ABL_VER_STR) - 1)
/* The ABL.hwver is like:
 *	"51,4,1900,b00f,0,8192" which means as below:
 *      "cpu_stepping,number_of_cores,max_non_turbo_frequency,platform_id,sku,total_amount_of_memory_present"
 */
static uint32_t get_cpu_num(const char *cmdline)
{
	const char *arg;
	uint32_t cpu_num;

	arg = strstr_s(cmdline, MAX_STR_LEN, ABL_VER_STR, ABL_VER_STR_LEN);
	if (!arg) {
		print_panic("ABL.hwver not found in cmdline\n");
		return FALSE;
	}

	arg += ABL_VER_STR_LEN;

	/* skip cpu stepping and get cpu numbers
	 * assume cpu stepping will not longer than 5 */
	arg = strstr_s(arg, 5, ",", 1);
	if (!arg) {
		print_panic("CPU number not found in cmdline!\n");
		return 0;
	}

	/* now the arg point to the 1st ','
	 * get cpu number from arg+1 */
	cpu_num = str2uint(arg + 1, 2, NULL, 10);
	if ((cpu_num > MAX_CPU_NUM) || (cpu_num == 0)) {
		print_panic("CPU number parse failed!\n");
		return 0;
	}

	return cpu_num;
}

/* Parse argument like: str=0xdeadbeef */
static boolean_t parse_integer_arg(const char *cmdline, const char *str, uint32_t base, boolean_t wipe_orig, uint32_t *value)
{
	const char *arg, *arg_end;
	uint32_t len;

	len = strnlen_s(str, MAX_STR_LEN);

	arg = strstr_s(cmdline, MAX_STR_LEN, str, len);
	if (!arg) {
		return FALSE;
	}

	*value = str2uint(arg + len, 10, &arg_end, base);
	if (*value == (uint32_t)-1) {
		return FALSE;
	}

	if (wipe_orig) {
		memset((void *)(uint64_t)arg, ' ', (uint32_t)(arg_end - arg));
	}

	return TRUE;
}

static boolean_t parse_addr(const char *cmdline, const char *str, uint32_t *addr)
{
	boolean_t ret;

	/* Parse ABL.svnseed (must have) */
	ret = parse_integer_arg(cmdline, str, 16, TRUE, addr);
	if (!ret || (*addr == 0)) {
		return FALSE;
	}

	return TRUE;
}

/* The cmdline is present like:
 *       "ABL.image1=0x600000@0xa000a000 ABL.hwver=51,4,1900,b00f,0,8192"
 */
boolean_t cmdline_parse(multiboot_info_t *mbi, cmdline_params_t *cmdline_param)
{
	const char *cmdline;
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

	/* Parse address of seed (must have)*/
	ret = parse_addr(cmdline, "ABL.svnseed=", &cmdline_param->svnseed_addr);
	if (!ret) {
		print_panic("Failed to parse address of svnseed!\n");
		return FALSE;
	}

	/* Parse address of rpmb key (optional) */
	parse_addr(cmdline, "ABL.rpmb=", &cmdline_param->rpmb_key_addr);

	/* Parse cpu num from cmdline (must have) */
	cpu_num = get_cpu_num(cmdline);
	if (cpu_num == 0) {
		print_panic("Failed to get cpu num!\n");
		return FALSE;
	}
	cmdline_param->cpu_num = (uint64_t)cpu_num;

	/* Parse image1 (bzImage, must have) */
	ret = parse_region(cmdline, "ABL.image1=", &cmdline_param->bzImage_base, &cmdline_param->bzImage_size);
	if (!ret) {
		print_panic("Failed to parse ABL.image1!\n");
		return FALSE;
	}

	/* currently, clear linux doesn't need initrd */
	cmdline_param->initrd_size = 0;
	cmdline_param->initrd_base = 0;

	return TRUE;
}
