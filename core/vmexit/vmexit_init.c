/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
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

#include "file_codes.h"
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_INIT_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMEXIT_INIT_C, __condition)
#include "mon_defs.h"
#include "guest_cpu.h"
#include "guest.h"
#include "local_apic.h"
#include "mon_dbg.h"
#include "hw_utils.h"
#include "vmcs_init.h"
#include "mon_events_data.h"

/*-------------------------------------------------------------------------*
*  FUNCTION : vmexit_init_event()
*  PURPOSE  : reset CPU
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : void
*  NOTE     : Propagate INIT signal from primary guest to CPU
*-------------------------------------------------------------------------*/
vmexit_handling_status_t vmexit_init_event(guest_cpu_handle_t gcpu)
{
	cpu_id_t cpu_id = hw_cpu_id();

	MON_LOG(mask_anonymous, level_trace,
		"INIT signal in Guest#%d GuestCPU#%d HostCPU#%d\n",
		mon_guest_vcpu(gcpu)->guest_id, mon_guest_vcpu(
			gcpu)->guest_cpu_id,
		hw_cpu_id());

	MON_ASSERT(guest_is_primary(mon_gcpu_guest_handle(gcpu)));

	if (cpu_id == 0) {      /* If cpu is BSP */
		MON_LOG(mask_anonymous,
			level_trace,
			"[%d] Perform global reset\n",
			cpu_id);
		hw_reset_platform(); /* then preform cold reset. */
		MON_DEADLOOP();
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"[%d] Switch to Wait for SIPI mode\n", cpu_id);

		/* Switch to Wait for SIPI state. */
		gcpu_set_activity_state(gcpu,
			IA32_VMX_VMCS_GUEST_SLEEP_STATE_WAIT_FOR_SIPI);
	}

	return VMEXIT_HANDLED;
}
