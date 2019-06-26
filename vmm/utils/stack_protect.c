/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "dbg.h"
#include <lib/print.h>

void __stack_chk_fail(void)
{
	printf("stack check fail in core\n");
	VMM_DEADLOOP();
}
