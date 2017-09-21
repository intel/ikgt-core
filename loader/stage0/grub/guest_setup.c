/*******************************************************************************
* Copyright (c) 2017 Intel Corporation
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
#include "vmm_asm.h"
#include "vmm_base.h"
#include "vmm_arch.h"
#include "stage0_asm.h"
#include "evmm_desc.h"
#include "grub_boot_param.h"
#include "ldr_dbg.h"
#include "guest_setup.h"

static void save_other_cpu_state(gcpu_state_t *s)
{
	asm_sgdt(&(s->gdtr));
	asm_sidt(&(s->idtr));
	s->cr0 = asm_get_cr0();
	s->cr3 = asm_get_cr3();
	s->cr4 = asm_get_cr4();

	s->msr_efer = asm_rdmsr(MSR_EFER);

	/* The selector of LDTR in current environment is invalid which indicates
	 * the bootloader is not using LDTR. So set LDTR unusable here. In
	 * future, exception might occur if LDTR is used in bootloader. Then bootloader
	 * will find us since we changed LDTR to 0, and we can fix it for that bootloader. */
	s->segment[SEG_LDTR] = (segment_t){0, 0, 0x10000, 0, {0}};

	/* TSS is used for RING switch, which is usually not used in bootloader since
	 * bootloader always runs in RING0. So we hardcode TR here. In future, #TS
	 * might occur if TSS is used bootloader. Then bootlaoder will find us since we
	 * changed TR to 0, and we can fix it for that bootlaoder. */
	s->segment[SEG_TR] = (segment_t){0, 0xFFFFFFFF, 0x808B, 0, {0}};

	/* For segments: CS/DS/ES/FS/GS/SS, get selector from current environment,
	 * hardcode other fields to make guest launch successful. */
	s->segment[SEG_CS] = (segment_t){0, 0xFFFFFFFF, 0xAF9B, __BOOT_CS, {0}};
	s->segment[SEG_DS] = (segment_t){0, 0xFFFFFFFF, 0xCF93, __BOOT_DS, {0}};
	s->segment[SEG_ES] = (segment_t){0, 0xFFFFFFFF, 0xCF93, __BOOT_DS, {0}};
	s->segment[SEG_FS] = (segment_t){0, 0xFFFFFFFF, 0xCF93, __BOOT_DS, {0}};
	s->segment[SEG_GS] = (segment_t){0, 0xFFFFFFFF, 0xCF93, __BOOT_DS, {0}};
	s->segment[SEG_SS] = (segment_t){0, 0xFFFFFFFF, 0xCF93, __BOOT_DS, {0}};
}

boolean_t g0_gcpu_setup(evmm_desc_t *evmm_desc,
		uint64_t boot_param_addr, uint64_t kernel_entry_point)
{
	void *rsp = allocate_memory(PAGE_4K_SIZE);
	if (rsp == NULL) {
		print_panic("allocate stack mem failed!\n");
		return FALSE;
	}

	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RAX] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RBX] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RCX] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RDX] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RSI] = boot_param_addr;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RDI] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RBP] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RSP] = (uint64_t)rsp;
	evmm_desc->guest0_gcpu0_state.rip = kernel_entry_point;
	evmm_desc->guest0_gcpu0_state.rflags = asm_get_rflags();

	save_other_cpu_state(&evmm_desc->guest0_gcpu0_state);

	return TRUE;
}
