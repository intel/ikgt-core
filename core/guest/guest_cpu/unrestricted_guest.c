/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
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

#include "vmcs_init.h"
#include "ept.h"
#include "ept_hw_layer.h"
#include "scheduler.h"
#include "vmx_ctrl_msrs.h"
#include "unrestricted_guest.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(UNRESTRICTED_GUEST_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(UNRESTRICTED_GUEST_C, \
	__condition)

/* Temporary external declarations */
extern boolean_t mon_ept_enable(guest_cpu_handle_t gcpu);

/* Check whether Unrestricted guest is enabled */
boolean_t mon_is_unrestricted_guest_enabled(guest_cpu_handle_t gcpu)
{
	boolean_t res = FALSE;

	res = hw_is_unrestricted_guest_enabled(gcpu);
	return res;
}

boolean_t hw_is_unrestricted_guest_enabled(guest_cpu_handle_t gcpu)
{
	processor_based_vm_execution_controls2_t proc_ctrls2;

	CHECK_EXECUTION_ON_LOCAL_HOST_CPU(gcpu);

	proc_ctrls2.uint32 =
		(uint32_t)mon_vmcs_read(mon_gcpu_get_vmcs(gcpu),
			VMCS_CONTROL2_VECTOR_PROCESSOR_EVENTS);

	return proc_ctrls2.bits.unrestricted_guest;
}

void unrestricted_guest_hw_disable(guest_cpu_handle_t gcpu)
{
	processor_based_vm_execution_controls2_t proc_ctrls2;
	vmexit_control_t vmexit_request;

	CHECK_EXECUTION_ON_LOCAL_HOST_CPU(gcpu);
	proc_ctrls2.uint32 = 0;
	mon_zeromem(&vmexit_request, sizeof(vmexit_request));

	proc_ctrls2.bits.unrestricted_guest = 1;
	vmexit_request.proc_ctrls2.bit_mask = proc_ctrls2.uint32;
	vmexit_request.proc_ctrls2.bit_request = 0;
	gcpu_control2_setup(gcpu, &vmexit_request);
}

void mon_unrestricted_guest_disable(guest_cpu_handle_t gcpu)
{
	MON_ASSERT(gcpu);
	CLR_UNRESTRICTED_GUEST_FLAG(gcpu);
	unrestricted_guest_hw_disable(gcpu);
}

/****************************************************************************
 * Function Name:  unrestricted_guest_enable
 * Arguments: gcpu: the guest cpu handle. Function assumes the input is
 *            validated by caller functions.
 ***************************************************************************/
void mon_unrestricted_guest_enable(guest_cpu_handle_t gcpu)
{
	uint64_t cr4;
	processor_based_vm_execution_controls2_t proc_ctrls2;
	vmexit_control_t vmexit_request;

	if (IS_MODE_UNRESTRICTED_GUEST(gcpu)) { /* vmcs cache counter will increment, */
		/* preventing disable until counter goes to 0, so don't let it get > 1. */
		return;
	}
	SET_UNRESTRICTED_GUEST_FLAG(gcpu);

	ept_acquire_lock();
	proc_ctrls2.uint32 = 0;
	mon_zeromem(&vmexit_request, sizeof(vmexit_request));

	proc_ctrls2.bits.unrestricted_guest = 1;
	vmexit_request.proc_ctrls2.bit_mask = proc_ctrls2.uint32;
	vmexit_request.proc_ctrls2.bit_request = UINT64_ALL_ONES;
	gcpu_control2_setup(gcpu, &vmexit_request);
	MON_ASSERT(mon_is_unrestricted_guest_enabled(gcpu));

	if (!mon_ept_is_ept_enabled(gcpu)) {
		mon_ept_enable(gcpu);
		cr4 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR4);
		ept_set_pdtprs(gcpu, cr4);
	}

	MON_ASSERT(mon_ept_is_ept_enabled(gcpu));

	ept_release_lock();
}
