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

#include "mon_defs.h"
#include "vmx_vmcs.h"
#include "vmcs_api.h"
#include "mon_dbg.h"
#include "em64t_defs.h"
#include "mon_events_data.h"
#include "guest_cpu.h"

vmexit_handling_status_t vmexit_invlpg(guest_cpu_handle_t gcpu)
{
	event_gcpu_invalidate_page_data_t data;
	ia32_vmx_exit_qualification_t qualification;
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	qualification.uint64 =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);
	data.invlpg_addr = qualification.invlpg_instruction.address;

	/* Return value of raising event is not important */
	event_raise(EVENT_GCPU_INVALIDATE_PAGE, gcpu, &data);

	/* Instruction will be skipped in upper "bottom-up" handler */


	return VMEXIT_HANDLED;
}
