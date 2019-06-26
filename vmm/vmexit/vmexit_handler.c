/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "vmx_asm.h"
#include "vmcs.h"
#include "guest.h"
#include "dbg.h"
#include "host_cpu.h"
#include "gcpu.h"
#include "vmexit.h"
#include "gcpu_inject_event.h"

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_invalid_instruction()
*  PURPOSE	: Handler for invalid instruction
*  ARGUMENTS: gcpu
*  RETURNS	: void
*--------------------------------------------------------------------------*/
void vmexit_invalid_instruction(guest_cpu_handle_t gcpu)
{
	D(VMM_ASSERT(gcpu));
	gcpu_inject_ud(gcpu);
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_xsetbv()
*  PURPOSE	: Handler for xsetbv instruction
*  ARGUMENTS: gcpu
*  RETURNS	: void
*--------------------------------------------------------------------------*/
void vmexit_xsetbv(guest_cpu_handle_t gcpu)
{
	uint32_t xcr0_mask_low, xcr0_mask_high;
	cpuid_params_t cpuid_params = {0xd, 0, 0, 0};

	D(VMM_ASSERT(gcpu));
	asm_cpuid(&cpuid_params);

	xcr0_mask_low = asm_xgetbv_hl(0, &xcr0_mask_high);
	/*
	 * let's check three things first before executing the instruction to make
	 * sure everything is correct, otherwise, inject GP0 to guest instead of
	 * failing in host since guest is responsible for the failure if any
	 * 1. Guest ECX must have a value of zero since only one XCR which is
	 *	  XCR0 is supported by HW currently
	 * 2. The reserved bits in XCR0 are not being changed
	 * 3. Bit 0 of XCR0 is not being changed to zero since it must be one.
	 * 4. No attempt to write 0 to bit 1 and 1 to bit 2, i.e. XCR0[2:1]=10.
	 */
	if (((gcpu_get_gp_reg(gcpu, REG_RCX) << 32) > 0) ||
		(((~((uint32_t)cpuid_params.eax)) & xcr0_mask_low) !=
		 (uint32_t)(~cpuid_params.eax &
			gcpu_get_gp_reg(gcpu, REG_RAX)))
		|| (((~((uint32_t)cpuid_params.edx)) & xcr0_mask_high) !=
		(uint32_t)(~cpuid_params.edx & gcpu_get_gp_reg(gcpu, REG_RDX)))
		|| ((gcpu_get_gp_reg(gcpu, REG_RAX) & 1) == 0)
		|| ((gcpu_get_gp_reg(gcpu, REG_RAX) & 0x6) == 0x4)) {
		gcpu_inject_gp0(gcpu);
		return;
	}

	asm_xsetbv(gcpu_get_gp_reg(gcpu, REG_RCX),
		MAKE64(gcpu_get_gp_reg(gcpu, REG_RDX),
		gcpu_get_gp_reg(gcpu, REG_RAX)));

	gcpu_skip_instruction(gcpu);

}
