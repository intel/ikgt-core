/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "guest.h"
#include "gcpu.h"
#include "vmexit.h"
#include "event.h"
#include "gcpu_inject_event.h"

void vmexit_msr_read(guest_cpu_handle_t gcpu)
{
	event_msr_vmexit_t msr_vmexit = {FALSE, FALSE};

	D(VMM_ASSERT(gcpu));

	event_raise(gcpu, EVENT_MSR_ACCESS, &msr_vmexit);

	if (!msr_vmexit.handled) {
		gcpu_inject_gp0(gcpu);
	}
}

void vmexit_msr_write(guest_cpu_handle_t gcpu)
{
	event_msr_vmexit_t msr_vmexit = {TRUE, FALSE};

	D(VMM_ASSERT(gcpu));

	event_raise(gcpu, EVENT_MSR_ACCESS, &msr_vmexit);

	if (!msr_vmexit.handled) {
		gcpu_inject_gp0(gcpu);
	}
}
