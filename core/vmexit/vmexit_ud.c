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

vmexit_handling_status_t vmexit_undefined_opcode(guest_cpu_handle_t gcpu)
{
	vmenter_event_t ud_event;

	ud_event.interrupt_info.bits.valid = 1;
	ud_event.interrupt_info.bits.vector =
		IA32_EXCEPTION_VECTOR_UNDEFINED_OPCODE;
	ud_event.interrupt_info.bits.interrupt_type =
		VMENTER_INTERRUPT_TYPE_HARDWARE_EXCEPTION;
	/* no error code delivered */
	ud_event.interrupt_info.bits.deliver_code = 0;
	gcpu_inject_event(gcpu, &ud_event);
	return VMEXIT_HANDLED;
}
