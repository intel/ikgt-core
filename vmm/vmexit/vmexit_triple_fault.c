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
#include "gcpu.h"
#include "guest.h"
#include "vmm_util.h"
#include "scheduler.h"
#include "dbg.h"

void vmexit_triple_fault(guest_cpu_handle_t gcpu)
{

	print_trace("Triple Fault Occured \n");
	if (gcpu) {
		print_trace("Guest:%d,CPUID:%d,RIP:0x%llx\n",
			gcpu->guest->id,
			gcpu->id,
			vmcs_read(gcpu->vmcs, VMCS_GUEST_RIP));
	} else {
		 print_trace("gcpu is NULL\n");
	}

	VMM_DEADLOOP();
}
