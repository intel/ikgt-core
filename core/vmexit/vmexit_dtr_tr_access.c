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

#include "mon_defs.h"
#include "guest_cpu.h"
#include "mon_dbg.h"
#include "vmexit_dtr_tr.h"
#include "ia32_defs.h"
#include "host_memory_manager_api.h"
#include "mon_callback.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_DTR_TR_ACCESS_C)
#define MON_ASSERT(__condition) \
	MON_ASSERT_LOG(VMEXIT_DTR_TR_ACCESS_C, __condition)

#define _MTF_SINGLE_STEP_

/*
 * Utils
 */

#ifdef DEBUG
void print_instruction_info(ia32_vmx_vmcs_vmexit_info_instruction_info_t *
			    instruction_info)
{
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits                      = %08X\n\n",
		instruction_info->bits);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.scaling              = %08X\n",
		instruction_info->bits.scaling);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.reserved_0           = %08X\n",
		instruction_info->bits.reserved_0);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.register1            = %08X\n",
		instruction_info->bits.register1);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.address_size          = %08X\n",
		instruction_info->bits.address_size);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.register_memory       = %08X\n",
		instruction_info->bits.register_memory);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.operand_size           = %08X\n",
		instruction_info->bits.operand_size);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.reserved_2           = %08X\n",
		instruction_info->bits.reserved_2);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.segment              = %08X\n",
		instruction_info->bits.segment);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.index_register        = %08X\n",
		instruction_info->bits.index_register);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.index_register_invalid = %08X\n",
		instruction_info->bits.index_register_invalid);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.base_register         = %08X\n",
		instruction_info->bits.base_register);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.base_register_invalid  = %08X\n",
		instruction_info->bits.base_register_invalid);
	MON_LOG(mask_anonymous, level_trace,
		"instruction_info.bits.register2            = %08X\n",
		instruction_info->bits.register2);
}

void print_guest_gprs(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_RCX = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_RCX));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_RDX = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_RDX));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_RBX = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_RBX));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_RBP = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_RBP));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_RSI = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_RSI));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_RDI = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_RDI));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_R8  = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_R8));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_R9  = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_R9));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_R10 = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_R10));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_R11 = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_R11));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_R12 = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_R12));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_R13 = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_R13));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_R14 = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_R14));
	MON_LOG(mask_anonymous, level_trace, "IA32_REG_R15 = %08X\n",
		gcpu_get_gp_reg(gcpu, IA32_REG_R15));
}

#endif

/*
 * VMEXIT Handlers
 */
vmexit_handling_status_t vmexit_dr_access(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	report_cr_dr_load_access_data_t dr_load_access_data;

	dr_load_access_data.qualification =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);

	if (!report_mon_event
		    (MON_EVENT_DR_LOAD_ACCESS, (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    (void *)&dr_load_access_data)) {
		MON_LOG(mask_anonymous,
			level_trace,
			"report_dr_load_access failed\n");
	}
	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_gdtr_idtr_access(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	report_dtr_access_data_t gdtr_idtr_access_data;

	gdtr_idtr_access_data.qualification =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);
	gdtr_idtr_access_data.instruction_info =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_INSTRUCTION_INFO);

	if (!report_mon_event
		    (MON_EVENT_GDTR_IDTR_ACCESS,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    (void *)&gdtr_idtr_access_data)) {
		MON_LOG(mask_anonymous, level_trace,
			"report_gdtr_idtr_access failed\n");
	}
	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_ldtr_tr_access(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	report_dtr_access_data_t ldtr_load_access_data;

	ldtr_load_access_data.qualification =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);
	ldtr_load_access_data.instruction_info =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_INSTRUCTION_INFO);

	if (!report_mon_event
		    (MON_EVENT_LDTR_LOAD_ACCESS,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    (void *)&ldtr_load_access_data)) {
		MON_LOG(mask_anonymous, level_trace,
			"report_ldtr_load_access failed\n");
	}

	return VMEXIT_HANDLED;
}
