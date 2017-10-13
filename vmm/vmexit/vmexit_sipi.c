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

#include "vmm_base.h"
#include "vmm_util.h"
#include "vmcs.h"
#include "gcpu.h"
#include "event.h"
#include "vmexit_cr_access.h"


/*-------------------------------------------------------------------------*
*  FUNCTION : vmexit_sipi_event()
*  PURPOSE  : Configure VMCS register-state with CPU Real Mode state.
*           : and launch emulator.
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : void
*  NOTE     : The hard-coded values used for VMCS-registers initialization
*           : are the values CPU sets its registers with after RESET.
*           : See Intel(R)64 and IA-32 Architectures Software Developer's
*           : Manual Volume 3A: System Programming Guide, Part 1
*           : Table 9-1. IA-32 Processor States Following Power-up, Reset, or
*           : INIT
*-------------------------------------------------------------------------*/
void vmexit_sipi_event(guest_cpu_handle_t gcpu)
{
	vmx_exit_qualification_t qualification;
	uint16_t real_mode_segment = 0;
	uint8_t vector;
	event_sipi_vmexit_t sipi_vmexit_param;

	D(VMM_ASSERT(gcpu));

	qualification.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
	vector = (uint8_t)(qualification.sipi.vector);
	sipi_vmexit_param.vector = vector;
	sipi_vmexit_param.handled = FALSE;

	print_trace("sipi vector=0x%x\n", (uint16_t)qualification.sipi.vector);

	/* Check if this is IPC SIPI signal. */
	event_raise(gcpu, EVENT_SIPI_VMEXIT, (void*)(&sipi_vmexit_param));
	if (sipi_vmexit_param.handled) {
		return;
	}

	print_trace("CPU-%d Leave SIPI State\n", host_cpu_id());
	real_mode_segment = (uint16_t)qualification.sipi.vector << 8;
	gcpu_set_seg(gcpu,
		SEG_CS,
		real_mode_segment,
		real_mode_segment << 4, 0xFFFF, 0x9B);

	vmcs_write(gcpu->vmcs, VMCS_GUEST_RIP, 0);

	vmcs_write(gcpu->vmcs, VMCS_GUEST_ACTIVITY_STATE,
			ACTIVITY_STATE_ACTIVE);
}
