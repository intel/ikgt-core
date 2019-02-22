/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _ABL_BOOT_PARAM_H_
#define _ABL_BOOT_PARAM_H_

#include "vmm_base.h"
#include "stage0_lib.h"

/* arguments parsed from cmdline */
typedef struct cmdline_params {
	uint64_t cpu_num;
	uint64_t bzImage_base;
	uint64_t bzImage_size;
	uint64_t initrd_base;
	uint64_t initrd_size;
} cmdline_params_t;

boolean_t cmdline_parse(multiboot_info_t *mbi, cmdline_params_t *cmdline_param);
#endif
