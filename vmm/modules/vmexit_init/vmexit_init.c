/*******************************************************************************
* Copyright (c) 2017 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

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
