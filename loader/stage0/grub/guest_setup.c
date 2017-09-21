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
#include "stage0_lib.h"

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

	save_current_cpu_state(&evmm_desc->guest0_gcpu0_state);

	return TRUE;
}
