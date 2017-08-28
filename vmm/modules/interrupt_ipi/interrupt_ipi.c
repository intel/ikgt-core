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
#include "vmm_base.h"
#include "gcpu.h"
#include "event.h"

#include "lib/lapic_ipi.h"
#include "modules/interrupt_ipi.h"

static void inject_intr_by_ipi(guest_cpu_handle_t gcpu, void *pv)
{
	uint8_t vector;
	boolean_t *handled = (boolean_t *)pv;

	for(vector = gcpu_get_pending_intr(gcpu); vector > 0x20; vector = gcpu_get_pending_intr(gcpu)) {
		if(!send_self_ipi(vector)) {
			print_warn("Inject INTR failed: failed to send self IPI!\n");
			*handled = FALSE;
			return;
		}
		gcpu_clear_pending_intr(gcpu, vector);
	}
	*handled = TRUE;
}

void interrupt_ipi_init(void)
{
	event_register(EVENT_INJECT_INTR, inject_intr_by_ipi);
}
