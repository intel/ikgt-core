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
#ifndef EXTERN_EVENT_HANDLER
#include "mon_callback.h"


boolean_t report_mon_event(report_mon_event_t event,
			   mon_identification_data_t gcpu,
			   const guest_vcpu_t *vcpu_id,
			   void *event_specific_data)
{
	boolean_t status = TRUE;

	switch (event) {
	case MON_EVENT_INITIALIZATION_BEFORE_APS_STARTED:
		break;
	case MON_EVENT_INITIALIZATION_AFTER_APS_STARTED:
		break;
	case MON_EVENT_EPT_VIOLATION:
		break;
	case MON_EVENT_MTF_VMEXIT:
		break;
	case MON_EVENT_CR_ACCESS:
		status = FALSE;
		break;
	case MON_EVENT_DR_LOAD_ACCESS:
		break;
	case MON_EVENT_LDTR_LOAD_ACCESS:
		break;
	case MON_EVENT_GDTR_IDTR_ACCESS:
		break;
	case MON_EVENT_MSR_READ_ACCESS:
		break;
	case MON_EVENT_MSR_WRITE_ACCESS:
		break;
	case MON_EVENT_SET_ACTIVE_EPTP:
		break;
	case MON_EVENT_INITIAL_VMEXIT_CHECK:
		status = FALSE;
		break;
	case MON_EVENT_SINGLE_STEPPING_CHECK:
		break;
	case MON_EVENT_MON_TEARDOWN:
		break;
	case MON_EVENT_INVALID_FAST_VIEW_SWITCH:
		break;
	case MON_EVENT_VMX_PREEMPTION_TIMER:
		break;
	case MON_EVENT_HALT_INSTRUCTION:
		break;
	case MON_EVENT_LOG:
		break;
	case MON_EVENT_CPUID:
		status = FALSE;
		break;
	}

	return status;
}


#endif
