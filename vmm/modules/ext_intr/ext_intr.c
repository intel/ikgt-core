/*******************************************************************************
* Copyright (c) 2017 Intel Corporation
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
#include "gcpu.h"
#include "guest.h"
#include "vmx_cap.h"
#include "event.h"
#include "modules/ext_intr.h"

/* Note: Currently if external interrupts module enabled, the Guest0(Android) will
 *       hang(too slow) when boot up. */

static void ext_intr_vmexit(guest_cpu_handle_t gcpu)
{
	vmx_exit_interrupt_info_t vmexit_intr_info;
	vmexit_intr_info.uint32 = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_INT_INFO);

	gcpu_set_pending_intr(gcpu, vmexit_intr_info.bits.vector);
}

static void ext_intr_gcpu_init(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	uint32_t pin_ctrl;
	uint32_t exit_ctrl;

	pin_ctrl = PIN_EXINT_EXIT | vmcs_read(gcpu->vmcs, VMCS_PIN_CTRL);
	vmcs_write(gcpu->vmcs, VMCS_PIN_CTRL, pin_ctrl);

	exit_ctrl = EXIT_ACK_INT_EXIT | vmcs_read(gcpu->vmcs, VMCS_EXIT_CTRL);
	vmcs_write(gcpu->vmcs, VMCS_EXIT_CTRL, exit_ctrl);
}

void ext_intr_init(void)
{
	vmexit_install_handler(ext_intr_vmexit, REASON_01_EXT_INT);
	event_register(EVENT_GCPU_MODULE_INIT, ext_intr_gcpu_init);
}
