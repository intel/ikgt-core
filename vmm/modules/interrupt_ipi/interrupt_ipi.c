/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "gcpu.h"
#include "event.h"

#include "lib/lapic_ipi.h"
#include "modules/interrupt_ipi.h"

static void inject_intr_by_ipi(guest_cpu_handle_t gcpu, void *pv)
{
	uint8_t vector;
	boolean_t *handled = (boolean_t *)pv;

	for(vector = gcpu_get_pending_intr(gcpu); vector >= 0x20; vector = gcpu_get_pending_intr(gcpu)) {
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
