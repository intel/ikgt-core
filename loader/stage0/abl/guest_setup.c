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
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "abl_boot_param.h"

void g0_gcpu_setup(evmm_desc_t *evmm_desc, android_image_boot_params_t *android_boot_params)
{
	/* save multiboot initial state */
	/* hardcode GP register sequence here to align with ABL */
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RAX] = android_boot_params->CpuState.cpu_gp_register[0];
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RBX] = android_boot_params->CpuState.cpu_gp_register[1];
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RCX] = android_boot_params->CpuState.cpu_gp_register[2];
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RDX] = android_boot_params->CpuState.cpu_gp_register[3];
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RSI] = android_boot_params->CpuState.cpu_gp_register[4];
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RDI] = android_boot_params->CpuState.cpu_gp_register[5];
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RBP] = android_boot_params->CpuState.cpu_gp_register[6];
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RSP] = android_boot_params->CpuState.cpu_gp_register[7];

	evmm_desc->guest0_gcpu0_state.rip = android_boot_params->CpuState.rip;
	evmm_desc->guest0_gcpu0_state.rflags = android_boot_params->CpuState.rflags;

	evmm_desc->guest0_gcpu0_state.gdtr.base       = android_boot_params->CpuState.gdtr.base;
	evmm_desc->guest0_gcpu0_state.gdtr.limit      = android_boot_params->CpuState.gdtr.limit;

	evmm_desc->guest0_gcpu0_state.idtr.base       = android_boot_params->CpuState.idtr.base;
	evmm_desc->guest0_gcpu0_state.idtr.limit      = android_boot_params->CpuState.idtr.limit;

	evmm_desc->guest0_gcpu0_state.cr0 = android_boot_params->CpuState.cr0;
	evmm_desc->guest0_gcpu0_state.cr3 = android_boot_params->CpuState.cr3;
	evmm_desc->guest0_gcpu0_state.cr4 = android_boot_params->CpuState.cr4;

	evmm_desc->guest0_gcpu0_state.segment[SEG_CS].base       = android_boot_params->CpuState.cs.base;
	evmm_desc->guest0_gcpu0_state.segment[SEG_CS].limit      = android_boot_params->CpuState.cs.limit;
	evmm_desc->guest0_gcpu0_state.segment[SEG_CS].attributes = android_boot_params->CpuState.cs.attributes;
	evmm_desc->guest0_gcpu0_state.segment[SEG_CS].selector   = android_boot_params->CpuState.cs.selector;

	evmm_desc->guest0_gcpu0_state.segment[SEG_DS].base       = android_boot_params->CpuState.ds.base;
	evmm_desc->guest0_gcpu0_state.segment[SEG_DS].limit      = android_boot_params->CpuState.ds.limit;
	evmm_desc->guest0_gcpu0_state.segment[SEG_DS].attributes = android_boot_params->CpuState.ds.attributes;
	evmm_desc->guest0_gcpu0_state.segment[SEG_DS].selector   = android_boot_params->CpuState.ds.selector;

	evmm_desc->guest0_gcpu0_state.segment[SEG_SS].base       = android_boot_params->CpuState.ss.base;
	evmm_desc->guest0_gcpu0_state.segment[SEG_SS].limit      = android_boot_params->CpuState.ss.limit;
	evmm_desc->guest0_gcpu0_state.segment[SEG_SS].attributes = android_boot_params->CpuState.ss.attributes;
	evmm_desc->guest0_gcpu0_state.segment[SEG_SS].selector   = android_boot_params->CpuState.ss.selector;

	evmm_desc->guest0_gcpu0_state.segment[SEG_ES].base       = android_boot_params->CpuState.es.base;
	evmm_desc->guest0_gcpu0_state.segment[SEG_ES].limit      = android_boot_params->CpuState.es.limit;
	evmm_desc->guest0_gcpu0_state.segment[SEG_ES].attributes = android_boot_params->CpuState.es.attributes;
	evmm_desc->guest0_gcpu0_state.segment[SEG_ES].selector   = android_boot_params->CpuState.es.selector;

	evmm_desc->guest0_gcpu0_state.segment[SEG_FS].base       = android_boot_params->CpuState.fs.base;
	evmm_desc->guest0_gcpu0_state.segment[SEG_FS].limit      = android_boot_params->CpuState.fs.limit;
	evmm_desc->guest0_gcpu0_state.segment[SEG_FS].attributes = android_boot_params->CpuState.fs.attributes;
	evmm_desc->guest0_gcpu0_state.segment[SEG_FS].selector   = android_boot_params->CpuState.fs.selector;

	evmm_desc->guest0_gcpu0_state.segment[SEG_GS].base       = android_boot_params->CpuState.gs.base;
	evmm_desc->guest0_gcpu0_state.segment[SEG_GS].limit      = android_boot_params->CpuState.gs.limit;
	evmm_desc->guest0_gcpu0_state.segment[SEG_GS].attributes = android_boot_params->CpuState.gs.attributes;
	evmm_desc->guest0_gcpu0_state.segment[SEG_GS].selector   = android_boot_params->CpuState.gs.selector;

	evmm_desc->guest0_gcpu0_state.segment[SEG_LDTR].base       = android_boot_params->CpuState.ldtr.base;
	evmm_desc->guest0_gcpu0_state.segment[SEG_LDTR].limit      = android_boot_params->CpuState.ldtr.limit;
	evmm_desc->guest0_gcpu0_state.segment[SEG_LDTR].attributes = android_boot_params->CpuState.ldtr.attributes;
	evmm_desc->guest0_gcpu0_state.segment[SEG_LDTR].selector   = android_boot_params->CpuState.ldtr.selector;

	evmm_desc->guest0_gcpu0_state.segment[SEG_TR].base       = android_boot_params->CpuState.tr.base;
	evmm_desc->guest0_gcpu0_state.segment[SEG_TR].limit      = android_boot_params->CpuState.tr.limit;
	evmm_desc->guest0_gcpu0_state.segment[SEG_TR].attributes = android_boot_params->CpuState.tr.attributes;
	evmm_desc->guest0_gcpu0_state.segment[SEG_TR].selector   = android_boot_params->CpuState.tr.selector;

	evmm_desc->guest0_gcpu0_state.msr_efer = android_boot_params->CpuState.msr_efer;
}
