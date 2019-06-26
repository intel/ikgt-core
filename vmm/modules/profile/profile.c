/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "vmm_objects.h"
#include "vmexit.h"
#include "scheduler.h"
#include "guest.h"
#include "heap.h"
#include "gcpu.h"
#include "vmcs.h"
#include "vmx_cap.h"
#include "event.h"
#include "host_cpu.h"
#include "modules/vmcall.h"
#include "lib/util.h"
#include "dbg.h"
#include "stack_profile.h"
#include "time_profile.h"

void profile_init(void)
{
#ifdef TIME_PROFILE
	time_profile_init();
#endif

#ifdef STACK_PROFILE
	stack_profile_init();
#endif

	return;
}
