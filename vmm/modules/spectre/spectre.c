/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "guest.h"
#include "gcpu.h"
#include "event.h"
#include "lib/util.h"

#include "modules/spectre.h"
#include "modules/msr_isolation.h"

/* Overwrite RSB(Return Stack Buffer) by 32 CALLs */
static void rsb_overwrite(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	__asm__ __volatile__ (
		"			mov %%rsp, %%rax;"
		"			call 2f;"
		"1:			call 3f;"
		"2:			call 4f;"
		"3:			call 5f;"
		"4:			call 6f;"
		"5:			call 7f;"
		"6:			call 8f;"
		"7:			call 9f;"
		"8:			call 10f;"
		"9:			call 11f;"
		"10:			call 12f;"
		"11:			call 13f;"
		"12:			call 14f;"
		"13:			call 15f;"
		"14:			call 16f;"
		"15:			call 17f;"
		"16:			call 18f;"
		"17:			call 19f;"
		"18:			call 20f;"
		"19:			call 21f;"
		"20:			call 22f;"
		"21:			call 23f;"
		"22:			call 24f;"
		"23:			call 25f;"
		"24:			call 26f;"
		"25:			call 27f;"
		"26:			call 28f;"
		"27:			call 29f;"
		"28:			call 30f;"
		"29:			call 31f;"
		"30:			call 1b;"
		"31:			call end_rsb_overwrite; pause;"
		"end_rsb_overwrite:	mov %%rax, %%rsp;"
		: : : "rax"
	);

}

static void trigger_ibpb(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	asm_wrmsr(MSR_PRED_CMD, PRED_CMD_IBPB);
}

void spectre_init(void)
{
	uint64_t spec_ctrl_msr;
	cpuid_params_t cpuid_param = {7, 0, 0, 0}; //eax=7, ecx=0

	asm_cpuid(&cpuid_param);

	if (cpuid_param.edx & CPUID_EDX_IBRS_IBPB) {
		spec_ctrl_msr = asm_rdmsr(MSR_SPEC_CTRL);
		asm_wrmsr(MSR_SPEC_CTRL, SPEC_CTRL_IBRS);
		add_to_msr_isolation_list(MSR_SPEC_CTRL, spec_ctrl_msr, GUEST_HOST_ISOLATION);

		event_register(EVENT_RSB_OVERWRITE, rsb_overwrite);
		event_register(EVENT_GCPU_SWAPIN, trigger_ibpb);
	} else {
		print_warn("IA32_SPEC_CTRL and IA32_PRED_CMD are not supported!");
	}
}
