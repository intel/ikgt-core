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

#include "gcpu.h"
#include "guest.h"
#include "vmx_asm.h"
#include "vmx_cap.h"
#include "dbg.h"
#include "vmcs.h"
#include "vmexit_cr_access.h"
#include "host_cpu.h"
#include "gcpu_inject_event.h"
#include "event.h"
#include "vmm_util.h"
#include "stack.h"

#include "lib/util.h"

#define local_print(fmt, ...)
//#define local_print(fmt, ...) vmm_printf(fmt, ##__VA_ARGS__)

/*---------------------------------------------------------------------------
 *
 * Context switching
 *
 *-------------------------------------------------------------------------- */

/* perform full state save before switching to another guest */
void gcpu_swap_out(guest_cpu_handle_t gcpu)
{
	D(VMM_ASSERT(gcpu));

	memcpy(&(gcpu->gp_reg[0]), gcpu->gp_ptr,
			REG_GP_COUNT * sizeof(uint64_t));

	gcpu->gp_ptr = &(gcpu->gp_reg[0]);

	event_raise(gcpu, EVENT_GCPU_SWAPOUT, NULL);
}

/* perform state restore after switching from another guest */
void gcpu_swap_in(const guest_cpu_handle_t gcpu)
{
	D(VMM_ASSERT(gcpu));

	gcpu->gp_ptr = (uint64_t *)(stack_get_cpu_sp(host_cpu_id())
			- (REG_GP_COUNT * sizeof(uint64_t)));

	memcpy(gcpu->gp_ptr, &(gcpu->gp_reg[0]),
			REG_GP_COUNT * sizeof(uint64_t));

	event_raise(gcpu, EVENT_GCPU_SWAPIN, NULL);

	vmcs_set_ptr(gcpu->vmcs);
}

/* should be called in gcpu_resume() for all vmexits */
static void gcpu_inject_pending_nmi(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;
	uint32_t pending_nmi = host_cpu_get_pending_nmi();
	uint32_t activity_state;
	vmx_entry_info_t interrupt_info;
	uint32_t interruptibility;
	boolean_t allowed = FALSE;
	uint32_t pin;

	// no need to assert gcpu, caller (vmexit_common_handler) makes sure it is valid

	if (pending_nmi == 0)
		return;

	if (gcpu->guest->id != 0) // nmi will be injected to primary guest only
		return;

	vmcs = gcpu->vmcs;

	activity_state = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_ACTIVITY_STATE);
	if (activity_state == ACTIVITY_STATE_WAIT_FOR_SIPI)
	{
		host_cpu_clear_pending_nmi();
		return;
	}

	interrupt_info.uint32 = (uint32_t)vmcs_read(vmcs, VMCS_ENTRY_INTR_INFO);
	if (interrupt_info.bits.valid) // another event already exists
		goto end_of_check;

	interruptibility = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_INTERRUPTIBILITY);
	// only bit3 (block_by_nmi) might be 1, others must be 0
	VMM_ASSERT_EX(((interruptibility & 0xFFFFFFF7) == 0),
		"unexpected bit in guest interruptibility(=%x)\n", interruptibility);
	if (interruptibility) // block by NMI
		goto end_of_check;

	allowed = TRUE;

end_of_check:
	local_print("%s(): pending_nmi=%d, allowed=%d\n", __FUNCTION__,
		pending_nmi, allowed);
	if (allowed)
	{
		// interrupt type: nmi=2; deliver error code=0; vector=2
		vmcs_write(vmcs, VMCS_ENTRY_INTR_INFO, 0x80000202);
		// error code and instruction length are not required
		host_cpu_clear_pending_nmi(); // don't let nmi accumulate
		local_print("hcpu%d, %s(): nmi=%d\n", host_cpu_id(),
				__FUNCTION__, host_cpu_get_pending_nmi());
	}
	else // cannot inject now, open WINDOW
	{
		pin = (uint32_t)vmcs_read(vmcs, VMCS_PIN_CTRL);
		if (pin & PIN_VIRTUAL_NMI) // nmi windows is allowed when virtual_nmi is 1
		{
			// enable nmi window
			vmcs_write(vmcs, VMCS_PROC_CTRL1,
				vmcs_read(vmcs, VMCS_PROC_CTRL1) |
				PROC_NMI_WINDOW_EXIT);
		}
		else // use intr window as workaround
		{
			// enable interrupt window
			vmcs_write(vmcs, VMCS_PROC_CTRL1,
				vmcs_read(vmcs, VMCS_PROC_CTRL1) |
				PROC_INT_WINDOW_EXIT);
		}
	}
}

static void gcpu_handle_nmi(guest_cpu_handle_t gcpu)
{
	uint32_t handled_nmi;
	uint32_t pending_nmi;

	// TODO: put below NMI code to the as later as possible in gcpu_resume()
	handled_nmi = 0;
	event_raise(gcpu, EVENT_PROCESS_NMI_BEFORE_RESUME, &handled_nmi);
	pending_nmi = host_cpu_get_pending_nmi();
	if (pending_nmi < handled_nmi) // hw nmi missing
	{
		print_warn("hcpu%d, nmi missing: pending_nmi=%d, handled_nmi=%d\n",
			host_cpu_id(), pending_nmi, handled_nmi);
	}
	if (pending_nmi && handled_nmi) // both not 0, need to decrease pending nmi number
	{
		local_print("hcpu%d, pending_nmi=%d, handled_nmi=%d\n",
			host_cpu_id(), pending_nmi, handled_nmi);
		host_cpu_dec_pending_nmi(MIN(pending_nmi, handled_nmi));
	}
	// to prevent ipc nmi injected during above period. some guest OS doesn't "like" NMI
	if (pending_nmi > handled_nmi)
		gcpu_inject_pending_nmi(gcpu);
}

/* should be called in gcpu_resume() for all vmexits */
static void gcpu_inject_pending_intr(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;
	uint8_t vector = gcpu_get_pending_intr(gcpu);
	uint32_t activity_state;
	vmx_entry_info_t interrupt_info;
	uint32_t interruptibility;
	uint64_t guest_rflags;
	boolean_t allowed = FALSE;

	// no need to assert gcpu, caller (vmexit_common_handler) makes sure it is valid

	if (vector < 0x20) // no pending intr
		return;

	vmcs = gcpu->vmcs;

	activity_state = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_ACTIVITY_STATE);
	if (activity_state > ACTIVITY_STATE_HLT) // inject interrupt is only allowed in Active and HLT
		return; // TODO: should we remove all pending intr here?

	interrupt_info.uint32 = (uint32_t)vmcs_read(vmcs, VMCS_ENTRY_INTR_INFO);
	if (interrupt_info.bits.valid) // another event already exists
		goto end_of_check;

	guest_rflags = vmcs_read(vmcs, VMCS_GUEST_RFLAGS);
	if (!(guest_rflags & RFLAGS_IF))
		goto end_of_check;

	interruptibility = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_INTERRUPTIBILITY);

	/* only both of the below bits are clear, the external interrupt injection to guest is allowed */
	if (interruptibility & (INTR_BLK_BY_STI | INTR_BLK_BY_MOV_SS))
		goto end_of_check;

	allowed = TRUE;

end_of_check:
	local_print("%s(): vector=%d, allowed=%d\n", __FUNCTION__,
		vector, allowed);
	if (allowed)
	{
		// interrupt type: external interrupt=0; deliver error code=0
		vmcs_write(vmcs, VMCS_ENTRY_INTR_INFO, 0x80000000ULL | (uint64_t)vector);
		// error code and instruction length are not required
		gcpu_clear_pending_intr(gcpu, vector);
	}
	if (gcpu_get_pending_intr(gcpu) >= 0x20) // still has pending interrupt
	{
		// enable interrupt window
		vmcs_write(vmcs, VMCS_PROC_CTRL1,
			vmcs_read(vmcs, VMCS_PROC_CTRL1) |
			PROC_INT_WINDOW_EXIT);
	}
}

/*---------------------------------------------------------------------------
 *
 * Resume execution.
 * never returns.
 *
 *-------------------------------------------------------------------------- */
void gcpu_resume(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;

	D(VMM_ASSERT(gcpu));

	vmcs = gcpu->vmcs;

	gcpu_handle_nmi(gcpu);

	gcpu_inject_pending_intr(gcpu);

	/* flush VMCS */
	vmcs_flush(vmcs);

	/* check for Launch and resume */
	if (!vmcs_is_launched(vmcs)) {
		vmcs_set_launched(vmcs);
		clear_deadloop_flag();
		/* call assembler launch */
		vmentry_func(TRUE);
		print_panic(
			"VmLaunch failed for GCPU %d GUEST %d\n",
			gcpu->id, gcpu->guest->id);
	} else {
		clear_deadloop_flag();
		/* call assembler resume */
		vmentry_func(FALSE);
		print_panic(
			"VmResume failed for GCPU %d GUEST %d\n",
			gcpu->id, gcpu->guest->id);
	}

	VMM_DEADLOOP();
}

