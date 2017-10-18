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
#include "heap.h"
#include "vmm_util.h"
#include "dbg.h"
#include "hmm.h"
#include "host_cpu.h"
#include "event.h"

#include "lib/lapic_ipi.h"

#include "modules/lapic_id.h"

static uint32_t g_lapic_id[MAX_CPU_NUM];

static void lapic_id_reinit_from_s3(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	uint16_t hcpu_id = host_cpu_id();
	VMM_ASSERT_EX(lapic_get_id(&g_lapic_id[hcpu_id]), "%s(): get lapic id failed\n", __FUNCTION__);
	print_trace("cpu%d, %s(): apic_id=0x%x\n",
		hcpu_id, __FUNCTION__, g_lapic_id[hcpu_id]);
}

/* need to be called for all cpus */
void lapic_id_init(void)
{
	uint16_t hcpu_id;

	// CPUID[EAX=1].EDX[9] indicates whether APIC is available
	// but, the presence of IA32_APIC_BASE_MSR doesn't depends on CPUID
	// so, there's no need to assert CPUID here

	hcpu_id = host_cpu_id();

	// BSP is called before AP
	if (hcpu_id == 0) {
		event_register(EVENT_RESUME_FROM_S3, lapic_id_reinit_from_s3);
	}

	VMM_ASSERT_EX(lapic_get_id(&g_lapic_id[hcpu_id]), "%s(): get lapic id failed\n", __FUNCTION__);
	print_trace("cpu%d, %s(): apic_id=0x%x\n",
		hcpu_id, __FUNCTION__, g_lapic_id[hcpu_id]);
}

/*IA32 spec, volume3, chapter 10 APIC->Local APIC->Local APIC ID
 *Local APIC ID usually not be changed*/
uint32_t get_lapic_id(uint16_t hcpu_id)
{
	VMM_ASSERT_EX(hcpu_id < host_cpu_num, "hcpu_id is invalid\n");
	return g_lapic_id[hcpu_id];
}
