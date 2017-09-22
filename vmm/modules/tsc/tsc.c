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
#include "guest.h"
#include "gcpu.h"
#include "vmm_util.h"
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

	offset_value = (asm_rdmsr(MSR_TIME_STAMP_COUNTER) - msr_value);
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

	asm_wrmsr(MSR_TIME_STAMP_COUNTER, msr_value);
	gcpu_skip_instruction(gcpu);
}

static void tsc_swap_in(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	uint64_t offset_value;
	vmcs_obj_t vmcs = gcpu->vmcs;

	if(gcpu->guest->id != 0)
	{
		/* In other guests, we adjust the VMCS_TSC_OFFSET to
		 * keep the TSC unaffected from guest 0.*/
		offset_value = -asm_rdmsr(MSR_TSC_ADJUST);
		vmcs_write(vmcs, VMCS_TSC_OFFSET, offset_value);
	}
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

void tsc_init(void)
{
	VMM_ASSERT_EX((get_proctl1_cap(NULL) & PROC_TSC_OFFSET),
		"tsc offset in proctl1 is not supported by vmx proctl1_may1\n");

	event_register(EVENT_GUEST_MODULE_INIT, tsc_setup);
	event_register(EVENT_GCPU_MODULE_INIT, tsc_gcpu_init);

	if(is_tsc_adjust_supported())
	{
		event_register(EVENT_GCPU_SWAPIN, tsc_swap_in);
	}else{
		/* since tsc adjust is not supported, we need to trap wrmsr
		 * to tsc to get the tsc delta.*/
		monitor_msr_write(0, MSR_TIME_STAMP_COUNTER, tsc_write);
	}
}
