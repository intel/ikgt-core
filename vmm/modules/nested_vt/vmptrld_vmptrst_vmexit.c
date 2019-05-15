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

void vmptrld_vmexit(guest_cpu_handle_t gcpu)
{
	vmx_exit_instr_info_t info;
	uint64_t disp;
	uint64_t scaled_index = 0, base = 0;
	uint64_t gva, hva;
	pf_info_t pfinfo;
	nestedvt_data_t *data = get_nestedvt_data(gcpu);

	disp = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
	info.uint32 = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_INFO);

	if (info.vmptrld_instr.index_reg_valid == 0) {
		scaled_index = gcpu_get_gp_reg(gcpu, info.vmptrld_instr.index_reg) << info.vmptrld_instr.scaling;
	}

	if (info.vmptrld_instr.base_reg_valid == 0) {
		base = gcpu_get_gp_reg(gcpu, info.vmptrld_instr.base_reg);
	}

	gva = base + scaled_index + disp;
	VMM_ASSERT(gcpu_copy_from_gva(gcpu, gva, (uint64_t)&data->gvmcs_gpa, 1 << (info.vmptrld_instr.addr_size + 1), &pfinfo));

	VMM_ASSERT(gpm_gpa_to_hva(gcpu->guest, data->gvmcs_gpa, GUEST_CAN_READ | GUEST_CAN_WRITE, &hva));

	/*
	 * According to IA-SDM Chapter 24.2 Format of the VMCS Region:
	 * 	skip [VMCS revision identifer, shadow-VMCS indicator] and [VMX-abort indicator]
	 */
	data->gvmcs = (uint64_t *)(hva + 8);

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}

void vmptrst_vmexit(guest_cpu_handle_t gcpu)
{
	vmx_exit_instr_info_t info;
	uint64_t disp;
	uint64_t scaled_index = 0, base = 0;
	uint64_t gva;
	pf_info_t pfinfo;
	nestedvt_data_t *data = get_nestedvt_data(gcpu);

	disp = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
	info.uint32 = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_INFO);

	if (info.vmptrst_instr.index_reg_valid == 0) {
		scaled_index = gcpu_get_gp_reg(gcpu, info.vmptrst_instr.index_reg) << info.vmptrst_instr.scaling;
	}

	if (info.vmptrst_instr.base_reg_valid == 0) {
		base = gcpu_get_gp_reg(gcpu, info.vmptrst_instr.base_reg);
	}

	gva = base + scaled_index + disp;
	VMM_ASSERT(gcpu_copy_to_gva(gcpu, gva, (uint64_t)(&data->gvmcs_gpa), 1 << (info.vmptrst_instr.addr_size + 1), &pfinfo));

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}
