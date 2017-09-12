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

static struct guest_descriptor_t *guests;

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
	uint32_t i;
	struct guest_descriptor_t *guest;

	guest = (struct guest_descriptor_t *)mem_alloc(sizeof(struct guest_descriptor_t));

	guest->id = guests ? (guests->id + 1) : 0;

	guest->gcpu_list = NULL;

	for (i=0; i<CR_HANDLER_NUM; i++)
	{
		guest->cr0_handlers[i] = NULL;
		guest->cr4_handlers[i] = NULL;
	}

	guest->cr0_mask = 0;
	guest->cr4_mask = 0;

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

/* there're many settings for a guest, such as number of gcpu, which gcpu register to which host cpu, ept policy, guest physical mapping.
** for this create_guest(), only number of gcpu is specified, other settings are:
** 1. gcpu register to the host cpu with same cpu id
** 2. guest physical mapping is 1:1 mapping for top_of_memory, and remove evmm's range
** 3. ept policy is EPT_POLICY, defined in .cfg
** In future, if there's new request to change these settings, please implement other versions of create_guest() api */
guest_handle_t create_guest(uint32_t gcpu_count, const module_file_info_t *evmm_file)
{
	guest_handle_t guest;
	guest_cpu_handle_t gcpu;
	uint32_t i;

	VMM_ASSERT_EX((gcpu_count <= host_cpu_num),
		"gcpu_count=%d is invalid\n", gcpu_count);
	VMM_ASSERT_EX((evmm_file), "evmm_file is NULL\n");

	/* create guest */
	guest = guest_register();

	gpm_set_mapping(guest, 0, 0, top_of_memory, 0x7);

	/* remove eVMM area from guest */
	gpm_remove_mapping(guest, evmm_file->runtime_addr, evmm_file->runtime_total_size);

	cr_write_guest_init(guest->id);

	ept_guest_init(guest);

	event_raise(NULL, EVENT_GUEST_MODULE_INIT, (void *)guest);

	/* special case - run on all existing CPUs */
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
