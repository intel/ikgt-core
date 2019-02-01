/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "gcpu.h"
#include "gcpu_state.h"
#include "guest.h"
#include "dbg.h"

/* Propagate INIT signal from primary guest to CPU */
static void vmexit_init_event(guest_cpu_handle_t gcpu)
{
	uint16_t cpu_id = host_cpu_id();

	/* comment out all the debug logs in this handler, otherwise it
	 * causes the subsequent SIPI signal loss.
	 */
	/*
	print_trace(
		"INIT signal in Guest#%d GuestCPU#%d HostCPU#%d\n",
		gcpu->guest->id, gcpu->id,
		host_cpu_id());
	*/

	VMM_ASSERT_EX(gcpu->guest->id == 0,
		"Unacceptable INIT signal from Guest[%d], GCPU[%d]", gcpu->guest->id, gcpu->id);

	VMM_ASSERT_EX(cpu_id != 0, "GCPU[%d] BSP must not be disabled!\n", cpu_id);

	/* print_trace("[%d] Switch to Wait for SIPI mode\n", cpu_id); */

	/* Reset CPU state following INIT signal */
	gcpu_set_reset_state(gcpu);
}

void vmexit_register_init_event(void)
{
	vmexit_install_handler(vmexit_init_event, REASON_03_INIT_EVENT);
}
