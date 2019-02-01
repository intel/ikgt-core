/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "vmm_asm.h"
#include "lib/print.h"
#include "lib/util.h"

void __stack_chk_fail(void)
{
	printf("stack check fail in loader\n");
	__STOP_HERE__
}
