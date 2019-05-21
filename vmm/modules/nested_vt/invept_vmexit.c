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

/* TODO: Add error check for invept vmexit */

void invept_vmexit(guest_cpu_handle_t gcpu)
{
	vmx_exit_instr_info_t info;
	uint64_t disp;
	uint64_t scaled_index = 0, base = 0;
	uint64_t gva;
	pf_info_t pfinfo;
	uint64_t invept_type;
	uint128_t invept_desc;
	uint64_t eptp_hpa;

	info.uint32 = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_INFO);

	invept_type = gcpu_get_gp_reg(gcpu, info.invept_instr.reg2);

	if (info.invept_instr.index_reg_valid == 0) {
		scaled_index = gcpu_get_gp_reg(gcpu, info.invept_instr.index_reg) << info.invept_instr.scaling;
	}

	if (info.invept_instr.base_reg_valid == 0) {
		base = gcpu_get_gp_reg(gcpu, info.invept_instr.base_reg);
	}

	disp = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);

	gva = base + scaled_index + disp;
	VMM_ASSERT(gcpu_copy_from_gva(gcpu, gva, (uint64_t)&invept_desc, 16, &pfinfo));
	VMM_ASSERT(gpm_gpa_to_hpa(gcpu->guest, invept_desc.uint64[0], &eptp_hpa, NULL));

	/* TODO: refactor cache invalidation for invept */
	asm_invept(eptp_hpa, invept_type);

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}
