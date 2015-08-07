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
#include "guest_cpu.h"
#include "mon_events_data.h"
#include "vmcs_api.h"
#include "hw_utils.h"
#include "mon_callback.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_EPT_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMEXIT_EPT_C, __condition)

vmexit_handling_status_t vmexit_mtf(guest_cpu_handle_t gcpu)
{
	if (!report_mon_event
		    (MON_EVENT_MTF_VMEXIT, (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu), NULL)) {
		MON_LOG(mask_mon, level_trace, "Report MTF VMExit failed.\n");
	}

	return VMEXIT_HANDLED;
}


boolean_t ept_violation_vmexit(guest_cpu_handle_t gcpu, void *pv);

vmexit_handling_status_t vmexit_ept_violation(guest_cpu_handle_t gcpu)
{
	event_gcpu_ept_violation_data_t data;
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	data.qualification.uint64 = mon_vmcs_read(vmcs,
		VMCS_EXIT_INFO_QUALIFICATION);
	data.guest_linear_address =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_GUEST_LINEAR_ADDRESS);
	data.guest_physical_address =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_GUEST_PHYSICAL_ADDRESS);
	data.processed = FALSE;

	ept_violation_vmexit(gcpu, &data);

	if (!data.processed) {
		MON_LOG(mask_anonymous,
			level_trace,
			"Unsupported ept violation in \n");
		PRINT_GCPU_IDENTITY(gcpu);
		MON_DEADLOOP();
	}

	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_ept_misconfiguration(guest_cpu_handle_t gcpu)
{
	event_gcpu_ept_misconfiguration_data_t data;
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	data.guest_physical_address =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_GUEST_PHYSICAL_ADDRESS);
	data.processed = FALSE;

	event_raise(EVENT_GCPU_EPT_MISCONFIGURATION, gcpu, &data);

	MON_ASSERT(data.processed);

	if (!data.processed) {
		MON_LOG(mask_anonymous, level_trace,
			"Unsupported ept misconfiguration in \n");
		PRINT_GCPU_IDENTITY(gcpu);
		MON_DEADLOOP();
	}

	return VMEXIT_HANDLED;
}
