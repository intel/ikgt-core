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
#include "guest.h"
#include "gcpu.h"
#include "vmexit.h"
#include "event.h"
#include "gcpu_inject_event.h"

void vmexit_msr_read(guest_cpu_handle_t gcpu)
{
	event_msr_vmexit_t msr_vmexit = {FALSE, FALSE};

	D(VMM_ASSERT(gcpu));

	event_raise(gcpu, EVENT_MSR_ACCESS, &msr_vmexit);

	if (!msr_vmexit.handled) {
		gcpu_inject_gp0(gcpu);
	}
}

void vmexit_msr_write(guest_cpu_handle_t gcpu)
{
	event_msr_vmexit_t msr_vmexit = {TRUE, FALSE};

	D(VMM_ASSERT(gcpu));

	event_raise(gcpu, EVENT_MSR_ACCESS, &msr_vmexit);

	if (!msr_vmexit.handled) {
		gcpu_inject_gp0(gcpu);
	}
}
