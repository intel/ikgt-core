/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "vmm_base.h"
#include "vmm_arch.h"
#include "stage0_asm.h"
#include "evmm_desc.h"
#include "grub_boot_param.h"
#include "ldr_dbg.h"
#include "guest_setup.h"
#include "stage0_lib.h"

boolean_t g0_gcpu_setup(evmm_desc_t *desc, uint64_t entry)
{
	void *rsp = allocate_memory(PAGE_4K_SIZE);
	if (rsp == NULL) {
		print_panic("allocate stack mem failed!\n");
		return FALSE;
	}

	desc->guest0_gcpu0_state.gp_reg[REG_RAX] = 0;
	desc->guest0_gcpu0_state.gp_reg[REG_RBX] = 0;
	desc->guest0_gcpu0_state.gp_reg[REG_RCX] = 0;
	desc->guest0_gcpu0_state.gp_reg[REG_RDX] = 0;
	desc->guest0_gcpu0_state.gp_reg[REG_RSI] = 0;
	desc->guest0_gcpu0_state.gp_reg[REG_RDI] = 0;
	desc->guest0_gcpu0_state.gp_reg[REG_RBP] = 0;
	desc->guest0_gcpu0_state.gp_reg[REG_RSP] = (uint64_t)rsp;
	desc->guest0_gcpu0_state.rip             = entry;
	desc->guest0_gcpu0_state.rflags          = asm_get_rflags();

	save_current_cpu_state(&desc->guest0_gcpu0_state);

	return TRUE;
}

void trusty_gcpu0_setup(evmm_desc_t *desc)
{
#if TRUSTY_64BIT_ENTRY
	/* EAX: multiboot magic */
	desc->trusty_desc.gcpu0_state.gp_reg[REG_RAX] = 0x2BADB002;
	/* EBX: Trusty memory region size (16MB) in KB */
	desc->trusty_desc.gcpu0_state.gp_reg[REG_RBX] = 0x4000;
	/* RFLAGS: reuse loader's RFLAGS */
	desc->trusty_desc.gcpu0_state.rflags          = asm_get_rflags();

	save_current_cpu_state(&desc->trusty_desc.gcpu0_state);
#else
	setup_32bit_env(&(evmm_desc->trusty_desc.gcpu0_state));
#endif
}
