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
#include "isr.h"
#include "guest_cpu.h"
#include "guest_cpu_vmenter_event.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_VMX_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMEXIT_VMX_C, __condition)


vmexit_handling_status_t vmexit_vmxon_instruction(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_mon, level_trace, "%s\n", __FUNCTION__);
#ifdef DEBUG
	MON_DEADLOOP();
#else
	mon_gcpu_inject_invalid_opcode_exception(gcpu);
#endif
	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_vmxoff_instruction(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_mon, level_trace, "%s\n", __FUNCTION__);
#ifdef DEBUG
	MON_DEADLOOP();
#else
	mon_gcpu_inject_invalid_opcode_exception(gcpu);
#endif
	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_vmread_instruction(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_mon, level_trace, "%s\n", __FUNCTION__);
#ifdef DEBUG
	MON_DEADLOOP();
#else
	mon_gcpu_inject_invalid_opcode_exception(gcpu);
#endif
	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_vmwrite_instruction(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_mon, level_trace, "%s\n", __FUNCTION__);
#ifdef DEBUG
	MON_DEADLOOP();
#else
	mon_gcpu_inject_invalid_opcode_exception(gcpu);
#endif
	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_vmptrld_instruction(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_mon, level_trace, "%s\n", __FUNCTION__);
#ifdef DEBUG
	MON_DEADLOOP();
#else
	mon_gcpu_inject_invalid_opcode_exception(gcpu);
#endif
	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_vmptrst_instruction(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_mon, level_trace, "%s\n", __FUNCTION__);
#ifdef DEBUG
	MON_DEADLOOP();
#else
	mon_gcpu_inject_invalid_opcode_exception(gcpu);
#endif
	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_vmlaunch_instruction(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_mon, level_trace, "%s\n", __FUNCTION__);
#ifdef DEBUG
	MON_DEADLOOP();
#else
	mon_gcpu_inject_invalid_opcode_exception(gcpu);
#endif
	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_vmresume_instruction(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_mon, level_trace, "%s\n", __FUNCTION__);
#ifdef DEBUG
	MON_DEADLOOP();
#else
	mon_gcpu_inject_invalid_opcode_exception(gcpu);
#endif
	return VMEXIT_HANDLED;
}

vmexit_handling_status_t vmexit_vmclear_instruction(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_mon, level_trace, "%s\n", __FUNCTION__);
#ifdef DEBUG
	MON_DEADLOOP();
#else
	mon_gcpu_inject_invalid_opcode_exception(gcpu);
#endif
	return VMEXIT_HANDLED;
}

