/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _STAGE0_MMAP_H_
#define _STAGE0_MMAP_H_

#include "vmm_base.h"
#include "stage0_lib.h"

boolean_t init_stage0_mmap(multiboot_info_t *mbi, uint32_t *tmp_type);
boolean_t insert_stage0_mmap(uint64_t base, uint64_t len, uint32_t type);
boolean_t get_max_stage0_mmap(uint64_t *base, uint64_t *len);
uint64_t get_sipi_page(void);
boolean_t stage0_mmap_to_e820(boot_params_t *bp);
#endif
