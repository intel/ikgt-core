/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "guest.h"
#include "gcpu.h"
#include "hmm.h"
#include "vmm_asm.h"
#include "event.h"
#include "heap.h"
#include "vmx_cap.h"

#include "lib/util.h"

#include "modules/msr_isolation.h"

typedef struct {
	msr_list_t msr_list[MAX_ISOLATED_MSR_COUNT];
	uint32_t msr_list_count;
	uint32_t pad;
} msr_isolation_t;

static msr_isolation_t host_msr_isolation;
static msr_isolation_t guest_msr_isolation;

static uint64_t alloc_and_copy_msr_list(msr_isolation_t *copy_from)
{
	msr_list_t *new_msr_list = NULL;
	uint64_t new_msr_list_hpa;
	new_msr_list = mem_alloc(copy_from->msr_list_count * sizeof(msr_list_t));

	memcpy(new_msr_list, &copy_from->msr_list,
			copy_from->msr_list_count * sizeof (msr_list_t));

	if (!hmm_hva_to_hpa((uint64_t)new_msr_list, &new_msr_list_hpa, NULL)) {
			print_trace(
				"%s: Failed to retrieve uint64_t of MSR list\n",
				__FUNCTION__);
			VMM_DEADLOOP();
	}
	return new_msr_list_hpa;
}

static void add_to_msr_list(msr_isolation_t *msr_isolation, uint32_t msr_index, uint64_t msr_value)
{
	uint32_t i;

#ifdef DEBUG
	/* Check if MSR already in the list */
	for ( i = 0; i < msr_isolation->msr_list_count; i++) {
		if ( msr_isolation->msr_list[i].msr_index == msr_index ) {
			VMM_DEADLOOP();
		}
	}
#endif

	VMM_ASSERT_EX((msr_isolation->msr_list_count < MAX_ISOLATED_MSR_COUNT),
		"too many msrs in msr isolation list\n");
	i = msr_isolation->msr_list_count;
	msr_isolation->msr_list[i].msr_index = msr_index;
	msr_isolation->msr_list[i].msr_data = msr_value;
	msr_isolation->msr_list_count ++;
}

/* Assume msr lists are the same for all guests, all gcpu. In future, if there's a request
 * to isolate a MSR which has different initial value for different guests or different
 * host/guest cpu, this function needs to be expanded.
 */
void add_to_msr_isolation_list(uint32_t msr_index, uint64_t msr_value, msr_policy_t msr_policy)
{

	add_to_msr_list(&guest_msr_isolation, msr_index, msr_value);

	if ( msr_policy == GUEST_HOST_ISOLATION )
		add_to_msr_list(&host_msr_isolation, msr_index, asm_rdmsr(msr_index));
}

/* Update msr list to VMCS field.
 * This event should be called only once for each gcpu.
 */
static void gcpu_update_msr_list(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	uint64_t msr_list_addr_hpa;

	if ( host_msr_isolation.msr_list_count != 0 ) {
#ifdef DEBUG
		uint64_t vmexit_msr_load_addr = vmcs_read(gcpu->vmcs, VMCS_EXIT_MSR_LOAD_ADDR);
		VMM_ASSERT(vmexit_msr_load_addr == 0);
#endif

		msr_list_addr_hpa = alloc_and_copy_msr_list(&host_msr_isolation);

		vmcs_write(gcpu->vmcs, VMCS_EXIT_MSR_LOAD_ADDR, msr_list_addr_hpa);
		vmcs_write(gcpu->vmcs, VMCS_EXIT_MSR_LOAD_COUNT, host_msr_isolation.msr_list_count);
	}

	if ( guest_msr_isolation.msr_list_count != 0 ) {
#ifdef DEBUG
		uint64_t vmexit_msr_store_addr = vmcs_read(gcpu->vmcs, VMCS_EXIT_MSR_STORE_ADDR);
		uint64_t vmenter_msr_load_addr = vmcs_read(gcpu->vmcs, VMCS_ENTRY_MSR_LOAD_ADDR);
		VMM_ASSERT(vmexit_msr_store_addr == vmenter_msr_load_addr);
		VMM_ASSERT(vmexit_msr_store_addr == 0);
#endif

		msr_list_addr_hpa = alloc_and_copy_msr_list(&guest_msr_isolation);

		vmcs_write(gcpu->vmcs, VMCS_EXIT_MSR_STORE_ADDR, msr_list_addr_hpa);
		vmcs_write(gcpu->vmcs, VMCS_ENTRY_MSR_LOAD_ADDR, msr_list_addr_hpa);
		vmcs_write(gcpu->vmcs, VMCS_EXIT_MSR_STORE_COUNT, guest_msr_isolation.msr_list_count);
		vmcs_write(gcpu->vmcs, VMCS_ENTRY_MSR_LOAD_COUNT, guest_msr_isolation.msr_list_count);
	}
}

static void print_msr_list(uint64_t msr_list_hpa, uint32_t count)
{
	UNUSED uint16_t hcpu_id = host_cpu_id();
	uint32_t i;
	msr_list_t * msr_list;

	print_info("msr_list_hpa=0x%llX, count=%d\n", msr_list_hpa, count);

	if (msr_list_hpa == 0)
		return;

	if (!hmm_hpa_to_hva(msr_list_hpa, (uint64_t *)&msr_list))
	{
		print_panic("%s: Host[%d] failed to get hva.\n",
				__FUNCTION__, hcpu_id);
		return;
	}

	for (i=0; i<count; i++)
	{
		print_info("msr_id=0x%x, value=0x%llx\n",
				msr_list[i].msr_index, msr_list[i].msr_data);
	}
}

static void vmenter_failed_msr_loading(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;
	UNUSED uint16_t hcpu_id = host_cpu_id();

	print_panic("%s: Host[%d], Guest[%d], VMENTER failed load MSR list.\n",
				__FUNCTION__, hcpu_id, gcpu->guest->id);

	vmcs = gcpu->vmcs;

	print_msr_list(vmcs_read(vmcs, VMCS_ENTRY_MSR_LOAD_ADDR),
			(uint32_t)vmcs_read(vmcs, VMCS_ENTRY_MSR_LOAD_COUNT));

	VMM_DEADLOOP();

}

void msr_isolation_init()
{
	msr_misc_data_t misc_data;

	misc_data.uint64 = get_misc_data_cap();
	VMM_ASSERT_EX((((uint32_t)misc_data.bits.max_msr_list_size + 1)*512 >= MAX_ISOLATED_MSR_COUNT),
			"MAX_ISOLATED_MSR_COUNT is exceed the limited.\n");

	vmexit_install_handler(vmenter_failed_msr_loading, REASON_34_ENTRY_FAIL_MSR);
	event_register(EVENT_GCPU_MODULE_INIT, gcpu_update_msr_list);
}
