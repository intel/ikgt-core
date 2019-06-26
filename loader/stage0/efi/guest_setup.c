/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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
