/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_arch.h"
#include "vmx_cap.h"
#include "heap.h"
#include "gcpu.h"
#include "guest.h"
#include "lock.h"
#include "event.h"
#include "nested_vt_internal.h"

#include "lib/util.h"

#include "modules/nested_vt.h"
#include "modules/msr_monitor.h"

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
	new_data->gvmcs_gpa = 0xFFFFFFFFFFFFFFFFULL;
	new_data->gvmcs = NULL;
	new_data->guest_layer = GUEST_L1;
	new_data->vmx_on_status = VMX_OFF;

	lock_acquire_write(&nestedvt_lock);
	new_data->next = g_nestedvt_data;
	g_nestedvt_data = new_data;
	lock_release(&nestedvt_lock);

	return new_data;
}

#define MAX_SUPPORTED_PROC2 ( \
				PROC2_ENABLE_EPT | \
				PROC2_ENABLE_RDTSCP | \
				PROC2_ENABLEC_VPID | \
				PROC2_UNRESTRICTED_GUEST | \
				PROC2_ENABLE_INVPCID | \
				PROC2_ENABLE_XSAVE \
			    )

static void msr_vmx_proc2_read_handler(guest_cpu_handle_t gcpu, uint32_t msr_id UNUSED)
{
	uint32_t proc2_may0, proc2_may1;

	D(VMM_ASSERT(msr_id == MSR_VMX_PROCBASED_CTRLS2));

	proc2_may1 = get_proctl2_cap(&proc2_may0);

	proc2_may1 &= MAX_SUPPORTED_PROC2;
	proc2_may1 |= proc2_may0;

	gcpu_set_gp_reg(gcpu, REG_RAX, proc2_may0);
	gcpu_set_gp_reg(gcpu, REG_RDX, proc2_may1);

	gcpu_skip_instruction(gcpu);
}

static void nested_vt_guest_setup(guest_cpu_handle_t gcpu UNUSED, void *pv)
{
	guest_handle_t guest = (guest_handle_t)pv;
	D(VMM_ASSERT(guest));

	monitor_msr_read(guest->id, MSR_VMX_PROCBASED_CTRLS2, msr_vmx_proc2_read_handler);
}

void nested_vt_init(void)
{
	lock_init(&nestedvt_lock, "nestedvt_lock");

	vmexit_install_handler(vmclear_vmexit,  REASON_19_VMCLEAR_INSTR);
	vmexit_install_handler(emulate_vmentry, REASON_20_VMLAUNCH_INSTR);
	vmexit_install_handler(vmptrld_vmexit,  REASON_21_VMPTRLD_INSTR);
	vmexit_install_handler(vmptrst_vmexit,  REASON_22_VMPTRST_INSTR);
	vmexit_install_handler(vmread_vmexit,   REASON_23_VMREAD_INSTR);
	vmexit_install_handler(emulate_vmentry, REASON_24_VMRESUME_INSTR);
	vmexit_install_handler(vmwrite_vmexit,  REASON_25_VMWRITE_INSTR);
	vmexit_install_handler(vmxoff_vmexit,   REASON_26_VMXOFF_INSTR);
	vmexit_install_handler(vmxon_vmexit,    REASON_27_VMXON_INSTR);
	vmexit_install_handler(invept_vmexit,   REASON_50_INVEPT_INSTR);
	vmexit_install_handler(invvpid_vmexit,  REASON_53_INVVPID_INSTR);

	event_register(EVENT_GUEST_MODULE_INIT, nested_vt_guest_setup);
	event_register(EVENT_VMEXIT_PRE_HANDLER, emulate_vmexit);
}
