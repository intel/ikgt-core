/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "guest.h"
#include "gcpu.h"
#include "gpm.h"
#include "heap.h"
#include "vmexit.h"
#include "dbg.h"
#include "hmm.h"
#include "scheduler.h"
#include "host_cpu.h"
#include "event.h"
#include "vmexit_cr_access.h"

#include "lib/util.h"

static struct guest_descriptor_t *guests;
static uint64_t g_evmm_rt_base;
static uint64_t g_evmm_rt_size;

guest_handle_t guest_handle(uint16_t guest_id)
{
	struct guest_descriptor_t *guest;

	if (!guests) {
		goto fail;
	}

	if (guest_id > guests->id) {
		goto fail;
	}

	for (guest = guests; guest != NULL; guest = guest->next_guest) {
		if (guest->id == guest_id) {
			return guest;
		}
	}

fail:
	print_panic("%s(): failed, guest_id(%d)\n", __FUNCTION__, guest_id);
	VMM_DEADLOOP();
	return NULL;
}

static guest_handle_t guest_register(void)
{
	struct guest_descriptor_t *guest;

	guest = (struct guest_descriptor_t *)mem_alloc(sizeof(struct guest_descriptor_t));

	memset(guest, 0, sizeof(struct guest_descriptor_t));

	guest->id = guests ? (guests->id + 1) : 0;

	guest->ept_policy.uint32 = EPT_POLICY;

	gpm_create_mapping(guest);

	guest->next_guest = guests;
	guests = guest;

	return guest;
}

static void add_cpu_to_guest(guest_cpu_handle_t gcpu, guest_handle_t guest)
{
	gcpu->guest = guest;
	gcpu->id = guest->gcpu_list ? guest->gcpu_list->id + 1 : 0;
	gcpu->next_same_guest = guest->gcpu_list;
	guest->gcpu_list = gcpu;
}

void guest_save_evmm_range(uint64_t evmm_rt_base, uint64_t evmm_rt_size)
{
	g_evmm_rt_base = evmm_rt_base;
	g_evmm_rt_size = evmm_rt_size;
}

#define MAX_GUEST_PHY_ADDR (1ULL << 48ULL)
/* there're many settings for a guest, such as number of gcpu, which gcpu register to which host cpu, ept policy, guest physical mapping.
** for this create_guest(), only number of gcpu is specified, other settings are:
** 1. gcpu register to the host cpu with same cpu id
** 2. guest physical mapping is 1:1 mapping for top_of_memory, and remove evmm's range
** 3. ept policy is EPT_POLICY, defined in .cfg
** In future, if there's new request to change these settings, please implement other versions of create_guest() api */
guest_handle_t create_guest(uint32_t gcpu_count, uint32_t attr)
{
	guest_handle_t guest;
	guest_cpu_handle_t gcpu;
	uint32_t i;

	VMM_ASSERT_EX((gcpu_count <= host_cpu_num),
		"gcpu_count=%d is invalid\n", gcpu_count);

	/* create guest */
	guest = guest_register();

	if (attr) {
		gpm_set_mapping(guest, 0, 0, MAX_PHYS_ADDR, attr);

		/* remove eVMM area from guest */
		gpm_remove_mapping(guest, g_evmm_rt_base, g_evmm_rt_size);
	}

	cr_write_guest_init(guest);

	ept_guest_init(guest);

	event_raise(NULL, EVENT_GUEST_MODULE_INIT, (void *)guest);

	for (i = 0; i < gcpu_count; i++) {
		gcpu = gcpu_allocate();
		add_cpu_to_guest(gcpu, guest);
		register_gcpu(gcpu, (uint16_t)i);
		print_trace(
			"CPU #%d added successfully to the current guest[%d]\n",
			i, guest->id);
	}

	return guest;
}
