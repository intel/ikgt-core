/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "gcpu.h"
#include "guest.h"
#include "scheduler.h"
#include "dbg.h"

void vmexit_triple_fault(guest_cpu_handle_t gcpu)
{

	print_trace("Triple Fault Occured \n");
	if (gcpu) {
		print_trace("Guest:%d,CPUID:%d,RIP:0x%llx\n",
			gcpu->guest->id,
			gcpu->id,
			vmcs_read(gcpu->vmcs, VMCS_GUEST_RIP));
	} else {
		 print_trace("gcpu is NULL\n");
	}

	VMM_DEADLOOP();
}
