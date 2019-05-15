/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "lib/util.h"
#include "heap.h"
#include "gcpu.h"
#include "lock.h"
#include "nested_vt_internal.h"

#include "modules/nested_vt.h"

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

void nested_vt_init(void)
{
	lock_init(&nestedvt_lock, "nestedvt_lock");

	vmexit_install_handler(vmptrld_vmexit, REASON_21_VMPTRLD_INSTR);
	vmexit_install_handler(vmptrst_vmexit, REASON_22_VMPTRST_INSTR);
	vmexit_install_handler(vmread_vmexit,  REASON_23_VMREAD_INSTR);
	vmexit_install_handler(vmwrite_vmexit, REASON_25_VMWRITE_INSTR);
	vmexit_install_handler(vmxoff_vmexit,  REASON_26_VMXOFF_INSTR);
	vmexit_install_handler(vmxon_vmexit,   REASON_27_VMXON_INSTR);
}
