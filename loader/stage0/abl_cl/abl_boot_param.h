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
	uint32_t cpu_num;
	uint32_t bzImage_base;
	uint32_t bzImage_size;
	uint32_t initrd_base;
	uint32_t initrd_size;
	uint32_t svnseed_addr;
	uint32_t rpmb_key_addr;
} cmdline_params_t;

#define ABL_SEED_LEN 32
#define ABL_SEED_LIST_MAX 4
typedef struct _abl_seed_info {
	uint8_t svn;
	uint8_t reserved[3];
	uint8_t seed[ABL_SEED_LEN];
} abl_seed_info_t;

typedef struct abl_svnseed {
	uint32_t size_of_this_struct;
	uint32_t version;
	uint32_t num_seeds;
	abl_seed_info_t seed_list[ABL_SEED_LIST_MAX];
} abl_svnseed_t;

#define ABL_RPMB_KEY_LEN 32U

boolean_t cmdline_parse(multiboot_info_t *mbi, cmdline_params_t *cmdline_param);
#endif
