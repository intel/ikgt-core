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

/*
 * TODO:
 *    1. add code to maintain VMCS state transition (SDM: Chapter 24.1)
 *    2. add fault cases check
 */
void vmclear_vmexit(guest_cpu_handle_t gcpu)
{
	vmx_exit_instr_info_t info;
	uint64_t disp;
	uint64_t scaled_index = 0, base = 0;
	uint64_t gva;
	uint64_t vmcs_gpa;
	pf_info_t pfinfo;
	nestedvt_data_t *data = get_nestedvt_data(gcpu);

	disp = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
	info.uint32 = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_INFO);

	if (info.vmclear_instr.index_reg_valid == 0) {
		scaled_index = gcpu_get_gp_reg(gcpu, info.vmclear_instr.index_reg) << info.vmclear_instr.scaling;
	}

	if (info.vmclear_instr.base_reg_valid == 0) {
		base = gcpu_get_gp_reg(gcpu, info.vmclear_instr.base_reg);
	}

	gva = base + scaled_index + disp;
	VMM_ASSERT(gcpu_copy_from_gva(gcpu, gva, (uint64_t)&vmcs_gpa, 1 << (info.vmclear_instr.addr_size + 1), &pfinfo));

	if (data->gvmcs_gpa == vmcs_gpa) {
		data->gvmcs_gpa = 0xFFFFFFFFFFFFFFFF;
		data->gvmcs = NULL;
	}

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}
