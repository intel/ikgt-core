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

void vmread_vmexit(guest_cpu_handle_t gcpu)
{
	vmx_exit_instr_info_t info;
	uint64_t disp;
	uint64_t scaled_index = 0, base = 0;
	uint64_t gva;
	pf_info_t pfinfo;
	uint32_t vmcs_encode;
	vmcs_field_t field_id;
	nestedvt_data_t *data = get_nestedvt_data(gcpu);

	info.uint32 = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_INFO);

	vmcs_encode = gcpu_get_gp_reg(gcpu, info.vmread_instr.reg2);
	field_id = enc2id(vmcs_encode);

	if (info.vmread_instr.mem_reg == 0) {
		if (info.vmread_instr.index_reg_valid == 0) {
			scaled_index = gcpu_get_gp_reg(gcpu, info.vmread_instr.index_reg) << info.vmread_instr.scaling;
		}

		if (info.vmread_instr.base_reg_valid == 0) {
			base = gcpu_get_gp_reg(gcpu, info.vmread_instr.base_reg);
		}

		disp = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);

		gva = base + scaled_index + disp;
		VMM_ASSERT(gcpu_copy_to_gva(gcpu, gva, (uint64_t)&data->gvmcs[field_id], 1 << (info.vmread_instr.addr_size + 1), &pfinfo));
	} else {
		gcpu_set_gp_reg(gcpu, info.vmread_instr.reg1, data->gvmcs[field_id]);
	}

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}

void vmwrite_vmexit(guest_cpu_handle_t gcpu)
{
	vmx_exit_instr_info_t info;
	uint64_t disp;
	uint64_t scaled_index = 0, base = 0;
	uint64_t gva;
	pf_info_t pfinfo;
	uint32_t vmcs_encode;
	vmcs_field_t field_id;
	nestedvt_data_t *data = get_nestedvt_data(gcpu);

	info.uint32 = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_INFO);

	vmcs_encode = gcpu_get_gp_reg(gcpu, info.vmwrite_instr.reg2);
	field_id = enc2id(vmcs_encode);

	if (info.vmwrite_instr.mem_reg == 0) {
		if (info.vmwrite_instr.index_reg_valid == 0) {
			scaled_index = gcpu_get_gp_reg(gcpu, info.vmwrite_instr.index_reg) << info.vmwrite_instr.scaling;
		}

		if (info.vmwrite_instr.base_reg_valid == 0) {
			base = gcpu_get_gp_reg(gcpu, info.vmwrite_instr.base_reg);
		}

		disp = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);

		gva = base + scaled_index + disp;
		VMM_ASSERT(gcpu_copy_from_gva(gcpu, gva, (uint64_t)&data->gvmcs[field_id], 1 << (info.vmwrite_instr.addr_size + 1), &pfinfo));
	} else {
		data->gvmcs[field_id] = gcpu_get_gp_reg(gcpu, info.vmwrite_instr.reg1);
	}

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}
