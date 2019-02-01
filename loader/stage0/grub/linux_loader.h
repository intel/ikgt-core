/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _LINUX_LOADER_H_
#define _LINUX_LOADER_H_

#include "stage0_lib.h"

boolean_t linux_kernel_parse(multiboot_info_t *mbi,
		uint64_t *boot_param_addr, uint64_t *entry_point);

#endif

