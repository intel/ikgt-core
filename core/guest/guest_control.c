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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(GUEST_CONTROL_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(GUEST_CONTROL_C, __condition)
#include "guest_internal.h"
#include "guest_cpu.h"
#include "mon_dbg.h"
#include "mon_globals.h"
#include "ipc.h"
#include "scheduler.h"

/******************************************************************************
*
* Main implementatuion idea:
*    at boot stage just iterate through all gcpus and immediately apply
*    at run stage use IPC to apply
*
******************************************************************************/

/* -------------------------- types ----------------------------------------- */
typedef struct {
	guest_handle_t		guest;
	volatile uint32_t	executed;
	uint8_t			pad1[4];
} ipc_comm_guest_struct_t;

/* ---------------------------- globals ------------------------------------- */
/* ---------------------------- internal funcs ------------------------------ */


/* apply vmexit config to the gcpu that are allocated
 * for the current host cpu */
static
void apply_vmexit_config(cpu_id_t from UNUSED, void *arg)
{
	guest_gcpu_econtext_t ctx;
	guest_cpu_handle_t gcpu;
	cpu_id_t this_hcpu_id = hw_cpu_id();

	ipc_comm_guest_struct_t *ipc = (ipc_comm_guest_struct_t *)arg;
	guest_handle_t guest = ipc->guest;
	volatile uint32_t *p_executed_count = &(ipc->executed);

	MON_ASSERT(guest);

	for (gcpu = mon_guest_gcpu_first(guest, &ctx); gcpu;
	     gcpu = mon_guest_gcpu_next(&ctx)) {
		if (this_hcpu_id == scheduler_get_host_cpu_id(gcpu)) {
			gcpu_control_apply_only(gcpu);
		}
	}

	/* mark as done */
	hw_interlocked_increment((int32_t *)p_executed_count);
}


/* ---------------------------- APIs --------------------------------------- */
void guest_control_setup(guest_handle_t guest, const vmexit_control_t *request)
{
	guest_gcpu_econtext_t ctx;
	guest_cpu_handle_t gcpu;
	mon_state_t mon_state;
	cpu_id_t this_hcpu_id = hw_cpu_id();

	MON_ASSERT(guest);

	/* setup vmexit requests without applying */
	for (gcpu = mon_guest_gcpu_first(guest, &ctx); gcpu;
	     gcpu = mon_guest_gcpu_next(&ctx))
		gcpu_control_setup_only(gcpu, request);

	/* now apply */
	mon_state = mon_get_state();

	if (MON_STATE_BOOT == mon_state) {
		/* may be run on BSP only */
		MON_ASSERT(0 == this_hcpu_id);

		/* single thread mode with all APs yet not init */
		for (gcpu = mon_guest_gcpu_first(guest, &ctx); gcpu;
		     gcpu = mon_guest_gcpu_next(&ctx))
			gcpu_control_apply_only(gcpu);
	} else if (MON_STATE_RUN == mon_state) {
		ipc_comm_guest_struct_t ipc;
		uint32_t wait_for_ipc_count = 0;
		ipc_destination_t ipc_dst;

		mon_memset(&ipc, 0, sizeof(ipc));
		mon_memset(&ipc_dst, 0, sizeof(ipc_dst));

		/* multi-thread mode with all APs ready and running
		 * or in Wait-For-SIPI state on behalf of guest */

		ipc.guest = guest;

		/* first apply for gcpus allocated for this hw cpu */
		apply_vmexit_config(this_hcpu_id, &ipc);

		/* reset executed counter and flush memory */
		hw_assign_as_barrier(&(ipc.executed), 0);

		/* send for execution */
		ipc_dst.addr_shorthand = IPI_DST_ALL_EXCLUDING_SELF;
		wait_for_ipc_count =
			ipc_execute_handler(ipc_dst, apply_vmexit_config, &ipc);

		/* wait for execution finish */
		while (wait_for_ipc_count != ipc.executed) {
			/* avoid deadlock - process one IPC if exist */
			ipc_process_one_ipc();
			hw_pause();
		}
	} else {
		/* not supported mode */
		MON_LOG(mask_anonymous, level_trace,
			"Unsupported global mon_state=%d in"
			" guest_request_vmexit_on()\n",
			mon_state);
		MON_DEADLOOP();
	}
}
