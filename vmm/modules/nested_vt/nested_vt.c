/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "modules/nested_vt.h"
#include "lib/util.h"
#include "heap.h"
#include "gcpu.h"
#include "lock.h"

#define GUEST_L1 0
#define GUEST_L2 1

#define VMX_ON 1
#define VMX_OFF 0

typedef struct nestedvt_data {
	guest_cpu_handle_t gcpu;
	uint64_t gvmcs_gpa;
	uint64_t *gvmcs;
	uint8_t guest_layer;
	uint8_t vmx_on_status;
	uint8_t pad[6];
	struct nestedvt_data *next;
} nestedvt_data_t;

static nestedvt_data_t *g_nestedvt_data = NULL;
static vmm_lock_t nestedvt_lock = {0};

nestedvt_data_t *get_nestedvt_data(guest_cpu_handle_t gcpu)
{
	nestedvt_data_t *p;
	nestedvt_data_t *new_data;

	D(VMM_ASSERT(gcpu));

	p = g_nestedvt_data;

	/* No need to add read lock because different physical cpu points to different gcpu.
           Data for same gcpu will not be created twice */
	while (p) {
		if (gcpu == p->gcpu) {
			return p;
		}
		p = p->next;
	}

	new_data = (nestedvt_data_t *)mem_alloc(sizeof(nestedvt_data_t));

	new_data->gcpu = gcpu;
	/* It stores the value FFFFFFFF_FFFFFFFF if there is no current VMCS according to IA spec*/
	new_data->gvmcs_gpa = 0xFFFFFFFFFFFFFFFF;
	new_data->gvmcs = NULL;
	new_data->guest_layer = GUEST_L1;
	new_data->vmx_on_status = VMX_OFF;

	lock_acquire_write(&nestedvt_lock);
	new_data->next = g_nestedvt_data;
	g_nestedvt_data = new_data;
	lock_release(&nestedvt_lock);

	return new_data;
}

void gvmcs_write(uint64_t *gvmcs, vmcs_field_t field_id, uint64_t value)
{
	D(VMM_ASSERT(gvmcs));
	D(VMM_ASSERT(field_id < VMCS_FIELD_COUNT));

	gvmcs[field_id] = value;

	return;
}

uint64_t gvmcs_read(uint64_t *gvmcs, vmcs_field_t field_id)
{
	D(VMM_ASSERT(gvmcs));
	D(VMM_ASSERT(field_id < VMCS_FIELD_COUNT));

	return gvmcs[field_id];
}

void nested_vt_init(void)
{
	lock_init(&nestedvt_lock, "nestedvt_lock");
}
