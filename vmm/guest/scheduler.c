/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "scheduler.h"
#include "dbg.h"
#include "gcpu.h"
#include "gcpu_switch.h"
#include "heap.h"
#include "guest.h"

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

static guest_cpu_handle_t get_gcpu_from_guest(guest_handle_t guest, uint32_t host_cpu)
{
	guest_cpu_handle_t gcpu = g_current_gcpu[host_cpu];

	do {
		if (gcpu->guest == guest)
			return gcpu;
		gcpu = gcpu->next_same_host_cpu;
	} while (gcpu != g_current_gcpu[host_cpu]);

	return NULL;
}

/*
 * Set initial guest. Return the target gcpu on success, NULL on fail
 */
guest_cpu_handle_t set_initial_guest(guest_handle_t guest)
{
	uint16_t host_cpu = host_cpu_id();
	guest_cpu_handle_t gcpu;

	D(VMM_ASSERT(guest));
	D(VMM_ASSERT_EX((host_cpu < host_cpu_num),
		"host_cpu_id=%d is invalid \n", host_cpu_id));

	gcpu = get_gcpu_from_guest(guest, host_cpu);
	if (gcpu == NULL) {
		print_panic("failed to get gcpu from guest!\n");
		return NULL;
	}

	g_current_gcpu[host_cpu] = gcpu;

	return gcpu;
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
 * Schedule to guest. Return the target gcpu on success, NULL on fail
 */
guest_cpu_handle_t schedule_to_guest(guest_handle_t guest)
{
	uint16_t host_cpu = host_cpu_id();
	guest_cpu_handle_t next_gcpu;

	D(VMM_ASSERT(guest));
	D(VMM_ASSERT_EX((host_cpu < host_cpu_num),
		"host_cpu_id=%d is invalid \n", host_cpu_id));

	next_gcpu = get_gcpu_from_guest(guest, host_cpu);
	if (next_gcpu == NULL) {
		print_panic("failed to get gcpu from guest!\n");
		return NULL;
	}

	if (next_gcpu == g_current_gcpu[host_cpu])
		return g_current_gcpu[host_cpu];

	host_cpu_clear_pending_nmi();
	gcpu_swap_out(g_current_gcpu[host_cpu]);

	g_current_gcpu[host_cpu] = next_gcpu;

	gcpu_swap_in(next_gcpu);

	return next_gcpu;
}
