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
#include "vmm_base.h"
#include "vmm_objects.h"
#include "vmexit.h"
#include "scheduler.h"
#include "guest.h"
#include "heap.h"
#include "gcpu.h"
#include "vmcs.h"
#include "vmx_cap.h"
#include "event.h"
#include "host_cpu.h"
#include "modules/vmcall.h"
#include "lib/util.h"
#include "dbg.h"
#include "stack_profile.h"

#define VMCALL_TIME_PROFILE  0x6C696E01
#define PROFILE_BUF_SIZE (4*4096)
//#define LOCAL_DEBUG 1

/* Sample of the profiling data */
typedef struct {
	uint32_t index;             // range 0~(2<<32-1)
	uint8_t  vmexit_guest_id;   // current guest id will exit
	uint8_t  vmexit_gcpu_id;    // current gcpu id will exit
	uint16_t vmexit_reason;     // current vm VMEXIT reason
	uint64_t vmexit_tsc;        // Start TSC of current VMEXIT
	uint64_t vmenter_tsc;       // Start TSC of next VMRESUME
	uint8_t  vmenter_guest_id;  // Resume guest id
	uint8_t  vmenter_gcpu_id;   // Resume gcpu id
	uint16_t reserve_1;
	uint32_t avail;             // Verify sample available
} profile_data_t;

/* Head of the profiling data buffer */
typedef struct {
	uint32_t put;
	uint32_t max_count;
	uint32_t tsc_per_ms;
	uint32_t pad;
	profile_data_t data[0];
} module_profile_t;

static module_profile_t *pmod_profile;

static void trusty_vmcall_dump_profile_data(guest_cpu_handle_t gcpu)
{
	uint64_t dump_gva;
	int ret = -1;
	pf_info_t pf_info;

	D(VMM_ASSERT(gcpu->guest->id == 0));

	dump_gva = gcpu_get_gp_reg(gcpu, REG_RDI);

	if (gcpu_copy_to_gva(gcpu, dump_gva, (uint64_t)pmod_profile, PROFILE_BUF_SIZE, &pf_info)) {
		ret = 0;
	}

	/* set the return value */
	gcpu_set_gp_reg(gcpu, REG_RAX, ret);

	return;
}

static void module_profile(guest_cpu_handle_t gcpu, void *p)
{
	event_profile_t *ptmp = (event_profile_t *)p;
	profile_data_t *pdata_put;
	vmcs_obj_t vmcs;
	vmx_exit_reason_t reason;
	uint32_t put;

	put = lock_inc32(&pmod_profile->put);
	pdata_put = pmod_profile->data + put % pmod_profile->max_count;
	vmcs = gcpu->vmcs;
	reason.uint32 = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_REASON);

	pdata_put->index = put;
	pdata_put->vmexit_guest_id = gcpu->guest->id;
	pdata_put->vmexit_gcpu_id= gcpu->id;
	pdata_put->vmexit_tsc = ptmp->vmexit_tsc;
	pdata_put->vmexit_reason = reason.bits.basic_reason;
	pdata_put->vmenter_guest_id = ptmp->next_gcpu->guest->id;
	pdata_put->vmenter_gcpu_id= ptmp->next_gcpu->id;
	pdata_put->vmenter_tsc = asm_rdtsc();
	pdata_put->avail = put;
#ifdef LOCAL_DEBUG
	print_info("put = %d exit_guest_id = %u, exit_gcpu_id = %u, exit_tsc: 0x%016llX,  resume_tsc: 0x%016llx, reason = %u resume_guestid = %d, resume_gcpuid = u\n",
	pdata_put->index,
	pdata_put->vmexit_guest_id,
	pdata_put->vmexit_gcpu_id,
	pdata_put->vmexit_tsc,
	pdata_put->vmenter_tsc,
	pdata_put->vmexit_reason,
	pdata_put->vmenter_guest_id,
	pdata_put->vmenter_gcpu_id);
#endif

	return;
}

void time_profile_init(void)
{
	print_info("%s start\n", __func__);

	pmod_profile = (module_profile_t *)mem_alloc(PROFILE_BUF_SIZE);

	memset((void *)pmod_profile, 0, PROFILE_BUF_SIZE);

	pmod_profile->put = 0xffffffff;
	pmod_profile->tsc_per_ms = tsc_per_ms;
	pmod_profile->max_count = (PROFILE_BUF_SIZE - sizeof(module_profile_t))/sizeof(profile_data_t);

	vmcall_register(0, VMCALL_TIME_PROFILE, trusty_vmcall_dump_profile_data);
	event_register(EVENT_MODULE_PROFILE, module_profile);

	return;
}
