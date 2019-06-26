/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "gcpu.h"
#include "vmexit.h"

void vmexit_invd(guest_cpu_handle_t gcpu)
{
	D(VMM_ASSERT(gcpu));

	/* We can't invalidate caches without writing them to memory */
	asm_wbinvd();
	gcpu_skip_instruction(gcpu);
}
