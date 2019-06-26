/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "gcpu.h"
#include "nested_vt_internal.h"

void vmxoff_vmexit(guest_cpu_handle_t gcpu)
{
	nestedvt_data_t *data = get_nestedvt_data(gcpu);

	data->vmx_on_status = VMX_OFF;

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}

void vmxon_vmexit(guest_cpu_handle_t gcpu)
{
	nestedvt_data_t *data = get_nestedvt_data(gcpu);

	data->vmx_on_status = VMX_ON;

	vm_succeed(gcpu);

	gcpu_skip_instruction(gcpu);
}
