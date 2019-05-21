/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "gcpu.h"
#include "gpm.h"
#include "nested_vt_internal.h"

/* TODO: Add error check for invvpid vmexit */

void invvpid_vmexit(guest_cpu_handle_t gcpu)
{
	vmx_exit_instr_info_t info;
	uint64_t disp;
	uint64_t scaled_index = 0, base = 0;
	uint64_t gva;
	pf_info_t pfinfo;
	uint64_t invvpid_type;
	struct {
		uint16_t vpid;
		uint16_t rsvd[3];
		uint64_t linear_addr;
	} invvpid_desc;

	info.uint32 = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_INFO);

	invvpid_type = gcpu_get_gp_reg(gcpu, info.invvpid_instr.reg2);

	if (info.invvpid_instr.index_reg_valid == 0) {
		scaled_index = gcpu_get_gp_reg(gcpu, info.invvpid_instr.index_reg) << info.invvpid_instr.scaling;
	}

	if (info.invvpid_instr.base_reg_valid == 0) {
		base = gcpu_get_gp_reg(gcpu, info.invvpid_instr.base_reg);
	}

	disp = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);

	gva = base + scaled_index + disp;
	VMM_ASSERT(gcpu_copy_from_gva(gcpu, gva, (uint64_t)&invvpid_desc, 16, &pfinfo));

	/* TODO: refactor cache invalidation for invvpid */
	asm_invvpid(invvpid_desc.vpid, invvpid_desc.linear_addr, invvpid_type);

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}
