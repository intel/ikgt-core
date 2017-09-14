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
#include "gpm.h"
#include "hmm.h"
#include "event.h"
#include "heap.h"
#include "gcpu.h"
#include "vmm_objects.h"
#include "modules/vmcall.h"
#include "modules/ipc.h"
#include "vmm_asm.h"
#include "ept.h"
#include "vmm_arch.h"
#include "vmx_cap.h"

#define VMCALL_EPT_UPDATE  0x65707501 //0x657075='ept'

static void flush_ept(UNUSED guest_cpu_handle_t gcpu, void *arg)
{
	uint64_t eptp = (uint64_t)arg;

	asm_invept(eptp);
}

static void assert_mapping_status(guest_cpu_handle_t gcpu, uint64_t start, uint64_t size, boolean_t mapped)
{
	uint64_t tgt_addr;
	ept_attr_t attr;
	boolean_t present;
	uint64_t end = start + size;

	while (start < end) {
		present = gpm_gpa_to_hpa(gcpu->guest, start, &tgt_addr, &attr);

		VMM_ASSERT_EX((mapped == present),
			"ept mapping check fail at 0x%llx, mapped:%d, present:%d\n", start, mapped, present);

		start += 4096;
	}
}

static void trusty_vmcall_ept_update(guest_cpu_handle_t gcpu)
{
	enum {
		ADD,
		REMOVE,
	};

	uint64_t start = gcpu_get_gp_reg(gcpu, REG_RDI);
	uint64_t size = gcpu_get_gp_reg(gcpu, REG_RSI);
	uint32_t action = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RDX);
	uint32_t flush_all_cpus = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RCX);
	ept_attr_t attr;

	switch (action) {
	case ADD:
		assert_mapping_status(gcpu, start, size, FALSE);

		attr.uint32 = 0x7;
		attr.bits.emt = CACHE_TYPE_WB;
		gpm_set_mapping(gcpu->guest, start, start, size, attr.uint32);
		break;
	case REMOVE:
		assert_mapping_status(gcpu, start, size, TRUE);

		gpm_remove_mapping(gcpu->guest, start, size);

		/* first flush ept TLB on local cpu */
		asm_invept(gcpu->guest->eptp);

		/* if with smp, flush ept TLB on remote cpus */
		if (flush_all_cpus)
			ipc_exec_on_all_other_cpus(flush_ept, (void*)gcpu->guest->eptp);
		break;
	default:
		print_panic("unknown action of EPT update\n");
		break;
	}

	return;
}

void ept_update_install(uint32_t guest_id)
{
	vmx_ept_vpid_cap_t ept_vpid;

	ept_vpid.uint64 = get_ept_vpid_cap();
	VMM_ASSERT_EX(ept_vpid.bits.invept,
			"invept is not support.\n");
	VMM_ASSERT_EX(ept_vpid.bits.invept_single_ctx,
			"single context invept type is not supported.\n");

	vmcall_register(guest_id, VMCALL_EPT_UPDATE, trusty_vmcall_ept_update);
}
