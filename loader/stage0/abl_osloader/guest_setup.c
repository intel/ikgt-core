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
#include "guest_setup.h"
#include "stage0_lib.h"
#include "ldr_dbg.h"
#include "evmm_desc.h"
#include "vmm_arch.h"
#include "lib/util.h"

boolean_t g0_gcpu_setup(evmm_desc_t *evmm_desc, uint64_t rsp, uint64_t rip)
{
	if (!evmm_desc) {
		print_panic("evmm_desc is NULL\n");
		return FALSE;
	}

	/* [RAX] is the return value when resume back to kernelflinger,
	 * set it to 0 to inform kernelflinger trusty boot successfully. */
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RAX] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RBX] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RCX] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RDX] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RSI] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RDI] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RBP] = 0;
	evmm_desc->guest0_gcpu0_state.gp_reg[REG_RSP] = rsp;
	evmm_desc->guest0_gcpu0_state.rip = rip;
	evmm_desc->guest0_gcpu0_state.rflags = asm_get_rflags();

	save_current_cpu_state(&evmm_desc->guest0_gcpu0_state);

	return TRUE;
}
