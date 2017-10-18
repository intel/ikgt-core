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

#include "scheduler.h"
#include "dbg.h"
#include "gcpu.h"
#include "gcpu_switch.h"
#include "heap.h"

#include "lib/util.h"

static guest_cpu_handle_t g_current_gcpu[MAX_CPU_NUM];

/*
 * Register guest cpu for given host_cpu_id.
 *
 * Note: no lock used currently because all gcpus registered in vmm_bsp_proc_main()
 */
void register_gcpu(guest_cpu_handle_t gcpu_handle, uint16_t host_cpu_id)
{
	D(VMM_ASSERT(gcpu_handle));
	D(VMM_ASSERT_EX((host_cpu_id < host_cpu_num),
		"host_cpu_id=%d is invalid \n", host_cpu_id));

	if (g_current_gcpu[host_cpu_id] == NULL) {
		g_current_gcpu[host_cpu_id] = gcpu_handle;
		g_current_gcpu[host_cpu_id]->next_same_host_cpu = gcpu_handle;
	}

	gcpu_handle->next_same_host_cpu = g_current_gcpu[host_cpu_id]->next_same_host_cpu;
	g_current_gcpu[host_cpu_id]->next_same_host_cpu = gcpu_handle;
	g_current_gcpu[host_cpu_id] = gcpu_handle;
}

/*
 * Get current guest_cpu_handle on current host cpu
 */
guest_cpu_handle_t get_current_gcpu()
{
	uint16_t host_cpu;

	host_cpu = host_cpu_id();

	return g_current_gcpu[host_cpu];
}

/*
 * Schedule to next guest cpu on current host cpu
 */
guest_cpu_handle_t schedule_next_gcpu()
{
	uint16_t host_cpu = host_cpu_id();
	guest_cpu_handle_t next_gcpu;

	VMM_ASSERT_EX(g_current_gcpu[host_cpu], "%s: no gcpu registered on host[%d]!\n", __func__, host_cpu);

	next_gcpu = g_current_gcpu[host_cpu]->next_same_host_cpu;

	if (next_gcpu == g_current_gcpu[host_cpu])
		return g_current_gcpu[host_cpu];

	host_cpu_clear_pending_nmi();
	gcpu_swap_out(g_current_gcpu[host_cpu]);

	g_current_gcpu[host_cpu] = next_gcpu;
	gcpu_swap_in(next_gcpu);

	return next_gcpu;
}

/*
 * Schedule to initial guest cpu on current host cpu
 */
guest_cpu_handle_t schedule_initial_gcpu()
{
	uint16_t host_cpu = host_cpu_id();

	VMM_ASSERT_EX(g_current_gcpu[host_cpu], "%s: no gcpu registered on host[%d]!\n", __func__, host_cpu);

	gcpu_swap_in(g_current_gcpu[host_cpu]);

	return g_current_gcpu[host_cpu];
}

/*
 * Schedule to next gcpu on same host as the initial gcpu
 */
void schedule_next_gcpu_as_init(uint16_t host_cpu_id)
{
	VMM_ASSERT_EX(host_cpu_id < host_cpu_num, "%s: Wrong host_cpu_id(%d), host_cpu_num=%d\n",
					__func__, host_cpu_id, host_cpu_num);

	g_current_gcpu[host_cpu_id] =
		g_current_gcpu[host_cpu_id]->next_same_host_cpu;
}
