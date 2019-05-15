/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "gcpu.h"
#include "nested_vt_internal.h"

/*
 * TODO: Currently, the false cases of vmx instruction vmexit are not checked/handled,
 *       will add them later.
 */

void vm_succeed(guest_cpu_handle_t gcpu)
{
	uint64_t old_rflags = vmcs_read(gcpu->vmcs, VMCS_GUEST_RFLAGS);
	vmcs_write(gcpu->vmcs, VMCS_GUEST_RFLAGS, old_rflags & (~RFLAGS_CF) & (~RFLAGS_PF) & (~RFLAGS_AF) & (~RFLAGS_ZF) & (~RFLAGS_SF) & (~RFLAGS_OF));
}
