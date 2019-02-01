/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "vmm_arch.h"
#include "sbl_boot_param.h"
#include "stage0_lib.h"
#include "ldr_dbg.h"

#include "lib/util.h"
#include "lib/string.h"

#define CHECK_FLAG(flag, bit)   ((flag) & (1 << (bit)))

/* The cmdline is present like:
 *       "ImageBootParamsAddr=0x12345"
 */
image_boot_params_t *cmdline_parse(multiboot_info_t *mbi)
{
	const char *cmdline;
	const char *arg;
	const char *param;
	uint32_t addr;
	image_boot_params_t *image_boot;

	if (!mbi) {
		print_panic("Multiboot info is NULL!\n");
		return NULL;
	}

	if (!CHECK_FLAG(mbi->flags, 2)) {
		print_panic("Multiboot info does not contain cmdline field!\n");
		return NULL;
	}

	cmdline = (const char *)(uint64_t)mbi->cmdline;

	/* Parse ImageBootParamsAddr */
	print_trace("cmdline form SBL: %s\n", cmdline);
	arg = strstr_s(cmdline, MAX_STR_LEN, "ImageBootParamsAddr=",
				sizeof("ImageBootParamsAddr=")-1);

	if (!arg) {
		print_panic("ImageBootParamsAddr not found in cmdline!\n");
		return NULL;
	}

	param = arg + sizeof("ImageBootParamsAddr=") - 1;

	addr = str2uint(param, 18, NULL, 16);
	if ((addr == (uint32_t)-1) || (addr == 0)) {
		print_panic("Failed to parse image_boot_param_addr!\n");
		return NULL;
	}

	image_boot = (image_boot_params_t *)(uint64_t)addr;
	if (!image_boot) {
		print_panic("image boot is NULL!\n");
		return NULL;
	}

	if (image_boot->size_of_this_struct != sizeof(image_boot_params_t)) {
		print_panic("size of image_boot is not match!\n");
		return NULL;
	}

	return image_boot;
}
