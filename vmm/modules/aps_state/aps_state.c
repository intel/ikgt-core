/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "guest.h"
#include "gcpu.h"
#include "gcpu_state.h"
#include "event.h"

static const gcpu_state_t *g0ap_state;

static void ap_set_init_state(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	if (gcpu->id == 0)
		return;

	gcpu_set_init_state(gcpu, &g0ap_state[gcpu->id - 1]);
}

void prepare_g0ap_init_state(const gcpu_state_t *ap_state)
{
	g0ap_state = ap_state;

	event_register(EVENT_GCPU_MODULE_INIT, ap_set_init_state);
}
