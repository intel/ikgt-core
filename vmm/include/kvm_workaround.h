/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef __KVM_WORKAROUND_H__
#define __KVM_WORKAROUND_H__

#include "vmm_base.h"
#include "vmm_asm.h"

#include "lib/util.h"

#define KVM_CPUID_SIGNATURE 0x40000000U

/*
 * Note: This function is to check whether we are running on top of KVM.
 *
 *    1. When enable SMP, KVM(L0) does not support IA32_VMX_MISC.wait_for_sipi,
 *       KVM will set L2's APs' state to active when it receieves SIPI signal
 *       from L2.
 *       So eVMM will:
 *             1.) not assert IA32_VMX_MISC.wait_for_sipi when running on KVM.
 *             2.) set L2's AP's initial state to HLT state.
 *    2. When enable SMP, eVMM will receieve a VMEXIT which to set L2's
 *       VMCS_GUEST_CR4.vmxe on AP. To workaround this issue, eVMM will not assert
 *       when receieve this vmexit if running on KVM.
 */
static inline boolean_t running_on_kvm(void)
{
	cpuid_params_t cpuid_param = { KVM_CPUID_SIGNATURE, 0U, 0U, 0U };

	asm_cpuid(&cpuid_param);
	if (!memcmp("KVMKVMKVM\0\0\0", &cpuid_param.ebx, 12U)) {
		return TRUE;
	}

	return FALSE;
}

#endif /* __KVM_WORKAROUND_H__ */
