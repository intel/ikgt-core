/*
 * Copyright (c) 2020 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */
#include "modules/spm_srv.h"
#include "shared-mem-smcall.h"

boolean_t is_spm_srv_call(guest_cpu_handle_t gcpu) {
	uint32_t id = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RDI);

	if (((id < SMC_FC32_FFA_MIN) || (id > SMC_FC32_FFA_MAX)) &&
		((id < SMC_FC64_FFA_MIN) || (id > SMC_FC64_FFA_MAX))) {
		return FALSE;
	}

	return TRUE;
}

boolean_t is_caller_secure(guest_cpu_handle_t gcpu) {
	return (GUEST_REE != gcpu->guest->id);
}

void smc_ret8(guest_cpu_handle_t gcpu, uint64_t r0, uint64_t r1,
	uint64_t r2, uint64_t r3, uint64_t r4,
	uint64_t r5, uint64_t r6, uint64_t r7) {
	gcpu_set_gp_reg(gcpu, REG_RDI, r0);
	gcpu_set_gp_reg(gcpu, REG_RSI, r1);
	gcpu_set_gp_reg(gcpu, REG_RDX, r2);
	gcpu_set_gp_reg(gcpu, REG_RCX, r3);
	gcpu_set_gp_reg(gcpu, REG_R8, r4);
	gcpu_set_gp_reg(gcpu, REG_R9, r5);
	gcpu_set_gp_reg(gcpu, REG_R10, r6);
	gcpu_set_gp_reg(gcpu, REG_R11, r7);
}

void smc_ret1(guest_cpu_handle_t gcpu, uint64_t r0) {
	smc_ret8(gcpu, r0, 0, 0, 0, 0, 0, 0, 0);
}
