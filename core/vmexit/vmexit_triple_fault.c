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
#include "hw_utils.h"
#include "scheduler.h"
#include "mon_dbg.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_TRIPLE_fault_C)
#define MON_ASSERT(__condition) \
	MON_ASSERT_LOG(VMEXIT_TRIPLE_fault_C, __condition)

vmexit_handling_status_t vmexit_triple_fault(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_anonymous, level_trace, "Triple Fault Occured on \n");
	PRINT_GCPU_IDENTITY(gcpu);
	MON_LOG(mask_anonymous, level_trace, "  Reset the System.\n");
	MON_DEBUG_CODE(MON_DEADLOOP());
	hw_reset_platform();
	/* TODO: Tear down the guest */

	/* just to pass release compilation */
	if (0) {
		gcpu = NULL;
	}
	return VMEXIT_HANDLED;
}
