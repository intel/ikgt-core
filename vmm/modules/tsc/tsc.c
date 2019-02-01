/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "guest.h"
#include "gcpu.h"
#include "host_cpu.h"
#include "dbg.h"
#include "event.h"
#include "vmm_asm.h"
#include "vmcs.h"
#include "vmm_objects.h"
#include "vmx_cap.h"
#include "gcpu_inject_event.h"
#include "scheduler.h"

#include "modules/msr_monitor.h"
#include "modules/tsc.h"

static boolean_t is_tsc_adjust_supported(void)
{
	cpuid_params_t cpuid_params = {0x7, 0, 0, 0};

	asm_cpuid(&cpuid_params);

	return (cpuid_params.ebx &
		CPUID_EBX_TSC_ADJUST) ? TRUE : FALSE;
}

static void tsc_write(guest_cpu_handle_t gcpu, UNUSED uint32_t msr_id)
{
	uint64_t offset_value;
	guest_cpu_handle_t next_gcpu = gcpu->next_same_host_cpu;
	uint64_t msr_value = get_val_for_wrmsr(gcpu);

	D(print_warn("%s() is called from gcpu(%d), msr value: 0x%llx -> 0x%llx\n", __FUNCTION__,
				next_gcpu->id, asm_rdmsr(msr_id), msr_value));

	offset_value = (asm_rdmsr(msr_id) - msr_value);
	while (gcpu != next_gcpu)
	{
		if(next_gcpu->guest->id != 0)
		{
			/* In other guests, we adjust the VMCS_TSC_OFFSET to
			 * keep the TSC unaffected from guest 0.*/
			vmcs_write(next_gcpu->vmcs, VMCS_TSC_OFFSET,
				(offset_value + vmcs_read(next_gcpu->vmcs, VMCS_TSC_OFFSET)));
		}
		next_gcpu = next_gcpu->next_same_host_cpu;
	}

	asm_wrmsr(msr_id, msr_value);
	gcpu_skip_instruction(gcpu);
}

static void tsc_setup(UNUSED guest_cpu_handle_t gcpu, void *pv)
{
	guest_handle_t guest = (guest_handle_t)pv;

	if(guest->id != 0)
	{
		block_msr_access(guest->id, MSR_TIME_STAMP_COUNTER);
		block_msr_access(guest->id, MSR_TSC_ADJUST);
		block_msr_access(guest->id, MSR_TSC_DEADLINE);
	}
}

static void tsc_gcpu_init(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	uint32_t ctrl;
	vmcs_obj_t vmcs = gcpu->vmcs;

	if(gcpu->guest->id != 0)
	{
		ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL1);
		ctrl |= PROC_TSC_OFFSET;
		vmcs_write(vmcs, VMCS_TSC_OFFSET, 0);
		vmcs_write(vmcs, VMCS_PROC_CTRL1, ctrl);
	}
}

#ifdef MODULE_SUSPEND

/* for all non-guest0's gcpu, save its tsc (= hw tsc+VMCS_TSC_OFFSET) to VMCS_TSC_OFFSET */
static void save_tsc_before_S3(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	guest_cpu_handle_t next_gcpu = gcpu->next_same_host_cpu;

	while (gcpu != next_gcpu)
	{
		if(next_gcpu->guest->id != 0)
		{
			vmcs_write(next_gcpu->vmcs, VMCS_TSC_OFFSET,
				(asm_rdtsc() + vmcs_read(next_gcpu->vmcs, VMCS_TSC_OFFSET)));
			D(print_info("%s(): guest(%d) gcpu(%d) TSC_OFFSET=0x%llx\n", __FUNCTION__,
				next_gcpu->guest->id, next_gcpu->id, vmcs_read(next_gcpu->vmcs, VMCS_TSC_OFFSET)));
		}
		next_gcpu = next_gcpu->next_same_host_cpu;
	}
}

/* for all non-guest0's gcpu, restore its tsc
 * before S3, guest_tsc = old_hw_tsc + VMCS_TSC_OFFSET. the guest_tsc is saved to VMCS_TSC_OFFSET
 * in save_tsc_before_S3()
 * So, the value in VMCS_TSC_OFFSET is the tsc expected in guest's view */
static void restore_tsc_after_S3(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	guest_cpu_handle_t next_gcpu = gcpu->next_same_host_cpu;

	while (gcpu != next_gcpu)
	{
		if(next_gcpu->guest->id != 0)
		{
			vmcs_write(next_gcpu->vmcs, VMCS_TSC_OFFSET,
				(vmcs_read(next_gcpu->vmcs, VMCS_TSC_OFFSET)) - asm_rdtsc());
			D(print_info("%s(): guest(%d) gcpu(%d) TSC_OFFSET=0x%llx\n", __FUNCTION__,
				next_gcpu->guest->id, next_gcpu->id, vmcs_read(next_gcpu->vmcs, VMCS_TSC_OFFSET)));
		}
		next_gcpu = next_gcpu->next_same_host_cpu;
	}
}

#endif

void tsc_init(void)
{
	VMM_ASSERT_EX((get_proctl1_cap(NULL) & PROC_TSC_OFFSET),
		"tsc offset in proctl1 is not supported by vmx proctl1_may1\n");

	event_register(EVENT_GUEST_MODULE_INIT, tsc_setup);
	event_register(EVENT_GCPU_MODULE_INIT, tsc_gcpu_init);

	if(is_tsc_adjust_supported())
	{
		monitor_msr_write(0, MSR_TSC_ADJUST, tsc_write);
	}
	monitor_msr_write(0, MSR_TIME_STAMP_COUNTER, tsc_write);

#ifdef MODULE_SUSPEND
	event_register(EVENT_SUSPEND_TO_S3, save_tsc_before_S3);
	event_register(EVENT_RESUME_FROM_S3, restore_tsc_after_S3);
#endif
}
