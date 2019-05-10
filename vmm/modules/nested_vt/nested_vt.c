/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "lib/util.h"
#include "heap.h"
#include "gcpu.h"
#include "lock.h"
#include "gpm.h"

#include "modules/nested_vt.h"

#define GUEST_L1 0
#define GUEST_L2 1

#define VMX_ON 1
#define VMX_OFF 0

typedef struct nestedvt_data {
	guest_cpu_handle_t gcpu;
	uint64_t gvmcs_gpa;
	uint64_t *gvmcs;
	uint8_t guest_layer;
	uint8_t vmx_on_status;
	uint8_t pad[6];
	struct nestedvt_data *next;
} nestedvt_data_t;

static nestedvt_data_t *g_nestedvt_data = NULL;
static vmm_lock_t nestedvt_lock = {0};

nestedvt_data_t *get_nestedvt_data(guest_cpu_handle_t gcpu)
{
	nestedvt_data_t *p;
	nestedvt_data_t *new_data;

	D(VMM_ASSERT(gcpu));

	p = g_nestedvt_data;

	/* No need to add read lock because different physical cpu points to different gcpu.
           Data for same gcpu will not be created twice */
	while (p) {
		if (gcpu == p->gcpu) {
			return p;
		}
		p = p->next;
	}

	new_data = (nestedvt_data_t *)mem_alloc(sizeof(nestedvt_data_t));

	new_data->gcpu = gcpu;
	/* It stores the value FFFFFFFF_FFFFFFFF if there is no current VMCS according to IA spec*/
	new_data->gvmcs_gpa = 0xFFFFFFFFFFFFFFFF;
	new_data->gvmcs = NULL;
	new_data->guest_layer = GUEST_L1;
	new_data->vmx_on_status = VMX_OFF;

	lock_acquire_write(&nestedvt_lock);
	new_data->next = g_nestedvt_data;
	g_nestedvt_data = new_data;
	lock_release(&nestedvt_lock);

	return new_data;
}

static void vm_succeed(guest_cpu_handle_t gcpu)
{
	uint64_t old_rflags = vmcs_read(gcpu->vmcs, VMCS_GUEST_RFLAGS);
	vmcs_write(gcpu->vmcs, VMCS_GUEST_RFLAGS, old_rflags & (~RFLAGS_CF) & (~RFLAGS_PF) & (~RFLAGS_AF) & (~RFLAGS_ZF) & (~RFLAGS_SF) & (~RFLAGS_OF));
}

/*
 * TODO: Currently, the false cases of vmx instruction vmexit are not checked/handled,
 *       will add them later.
 */

static void vmxoff_vmexit(guest_cpu_handle_t gcpu)
{
	nestedvt_data_t *data = get_nestedvt_data(gcpu);

	data->vmx_on_status = VMX_OFF;

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}

static void vmxon_vmexit(guest_cpu_handle_t gcpu)
{
	nestedvt_data_t *data = get_nestedvt_data(gcpu);

	data->vmx_on_status = VMX_ON;

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}

static void vmptrld_vmexit(guest_cpu_handle_t gcpu)
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

static void vmptrst_vmexit(guest_cpu_handle_t gcpu)
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

static void vmread_vmexit(guest_cpu_handle_t gcpu)
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

static void vmwrite_vmexit(guest_cpu_handle_t gcpu)
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

void nested_vt_init(void)
{
	lock_init(&nestedvt_lock, "nestedvt_lock");

	vmexit_install_handler(vmptrld_vmexit, REASON_21_VMPTRLD_INSTR);
	vmexit_install_handler(vmptrst_vmexit, REASON_22_VMPTRST_INSTR);
	vmexit_install_handler(vmread_vmexit,  REASON_23_VMREAD_INSTR);
	vmexit_install_handler(vmwrite_vmexit, REASON_25_VMWRITE_INSTR);
	vmexit_install_handler(vmxoff_vmexit,  REASON_26_VMXOFF_INSTR);
	vmexit_install_handler(vmxon_vmexit,   REASON_27_VMXON_INSTR);
}
