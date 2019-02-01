/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "gcpu.h"
#include "vmcs.h"
#include "dbg.h"
#include "gcpu_inject_event.h"
#include "vmexit_cr_access.h"
#include "vmx_cap.h"
#include "guest.h"
#include "vmm_arch.h"
#include "heap.h"

#include "lib/util.h"

static void cr_write_register(cr_access_t *cr_access, cr_pre_handler pre_handler, cr_handler handler,
				cr_post_handler post_handler, uint64_t mask)
{
	cr_handlers_t *cr_handler;

	cr_handler = (cr_handlers_t *)mem_alloc(sizeof(cr_handlers_t));
	memset(cr_handler, 0, sizeof(cr_handlers_t));

	cr_handler->pre_handler = pre_handler;
	cr_handler->handler = handler;
	cr_handler->post_handler = post_handler;

	cr_access->cr_mask |= mask;
	cr_handler->next = cr_access->cr_handlers;
	cr_access->cr_handlers = cr_handler;
}

void cr0_write_register(guest_handle_t guest, cr_pre_handler pre_handler, cr_handler handler,
				cr_post_handler post_handler, uint64_t mask)
{
	D(VMM_ASSERT(guest));

	VMM_ASSERT_EX(mask, "cr0 write mask is 0\n");
	VMM_ASSERT_EX(((mask & guest->cr0_access.cr_mask) == 0),
			"mask(0x%llx) has already been registered\n", mask);

	if (handler == NULL){
		print_warn("%s:handler is NULL.\n", __FUNCTION__);
	}

	cr_write_register(&guest->cr0_access, pre_handler, handler, post_handler, mask);
}

void cr4_write_register(guest_handle_t guest, cr_pre_handler pre_handler, cr_handler handler,
				cr_post_handler post_handler, uint64_t mask)
{
	D(VMM_ASSERT(guest));

	VMM_ASSERT_EX(mask, "cr4 write mask is 0\n");
	VMM_ASSERT_EX(((mask & guest->cr4_access.cr_mask) == 0),
			"mask(0x%llx) has already been registered\n", mask);

	if (handler == NULL){
		print_warn("%s:handler is NULL.\n", __FUNCTION__);
	}

	cr_write_register(&guest->cr4_access, pre_handler, handler, post_handler, mask);
}

static boolean_t cr_guest_write(guest_cpu_handle_t gcpu, uint64_t write_value, boolean_t is_cr0)
{
	guest_handle_t guest = gcpu->guest;
	cr_access_t *cr_access;
	vmcs_obj_t vmcs = gcpu->vmcs;
	cr_handlers_t *cr_handler;
	uint64_t cr_value;

	if (is_cr0){
		cr_access = &guest->cr0_access;
	}else{
		cr_access = &guest->cr4_access;
	}

	for (cr_handler = cr_access->cr_handlers; cr_handler; cr_handler = cr_handler->next)
	{
		if (cr_handler->pre_handler){
			if (cr_handler->pre_handler(write_value)){
				gcpu_inject_gp0(gcpu);
				return TRUE;
			}
		}
	}

	if (is_cr0){
		cr_value = vmcs_read(vmcs, VMCS_GUEST_CR0);
	}else{
		cr_value = vmcs_read(vmcs, VMCS_GUEST_CR4);
	}

	for (cr_handler = cr_access->cr_handlers; cr_handler; cr_handler = cr_handler->next)
	{
		if (cr_handler->handler){
			cr_handler->handler(write_value, &cr_value);
		}
	}

	if (is_cr0){
		vmcs_write(vmcs, VMCS_GUEST_CR0, cr_value);
		vmcs_write(vmcs, VMCS_CR0_SHADOW, write_value);
	}else{
		vmcs_write(vmcs, VMCS_GUEST_CR4, cr_value);
		vmcs_write(vmcs, VMCS_CR4_SHADOW, write_value);
	}

	for (cr_handler = cr_access->cr_handlers; cr_handler; cr_handler = cr_handler->next)
	{
		if (cr_handler->post_handler){
			cr_handler->post_handler(gcpu);
		}
	}

	return FALSE;
}

/* cr0_guest_write() will set value to shadow and call registered handlers to
** update bits from existing VMCS_GUEST_CR0.
** when new CR0 is assigned from host (not guest), a vmcs write to VMCS_GUEST_CR0
** is required before calling cr0_guest_write()
*/
boolean_t cr0_guest_write(guest_cpu_handle_t gcpu, uint64_t write_value)
{
	D(VMM_ASSERT(gcpu));

	return cr_guest_write(gcpu, write_value, TRUE);
}

/* same as cr0_guest_write() */
boolean_t cr4_guest_write(guest_cpu_handle_t gcpu, uint64_t write_value)
{
	D(VMM_ASSERT(gcpu));

	return cr_guest_write(gcpu, write_value, FALSE);
}

static void cr0_ne_handler(UNUSED uint64_t write_value, uint64_t* cr_value)
{
	// CR0.NE is always 1 due to HW requirement
	*cr_value |= CR0_NE;
}

static boolean_t cr4_vmxe_pre_handler(uint64_t write_value)
{
	if (write_value & CR4_VMXE) // nested VT is not supported by eVmm
	{
		print_warn("%s(), write_value=0x%llx, injecting #GP\n", __FUNCTION__, write_value);
		return TRUE;
	}

	return FALSE;
}

static void cr4_vmxe_handler(UNUSED uint64_t write_value, uint64_t* cr_value)
{
	// CR4.VMXE is always 1 due to HW requirement
	*cr_value |= CR4_VMXE;
}

static boolean_t cr4_smxe_pre_handler(uint64_t write_value)
{
	if (write_value & CR4_SMXE) // smx is not allowed by eVmm
	{
		print_warn("%s(), write_value=0x%llx, injecting #GP\n", __FUNCTION__, write_value);
		return TRUE;
	}

	return FALSE;
}

static void cr4_smxe_handler(UNUSED uint64_t write_value, uint64_t* cr_value)
{
	// CR4.SMXE is always 0
	*cr_value &= ~CR4_SMXE;
}

void cr_write_gcpu_init(guest_cpu_handle_t gcpu)
{
	guest_handle_t guest_handle;

	D(VMM_ASSERT(gcpu));

	guest_handle = gcpu->guest;
	vmcs_write(gcpu->vmcs, VMCS_CR0_MASK, guest_handle->cr0_access.cr_mask);
	vmcs_write(gcpu->vmcs, VMCS_CR4_MASK, guest_handle->cr4_access.cr_mask);
}

void cr_write_guest_init(guest_handle_t guest)
{
	uint64_t cr0_may0;
	uint64_t cr4_may1;

	D(VMM_ASSERT(guest));

	get_cr0_cap(&cr0_may0);
	cr4_may1 = get_cr4_cap(NULL);

	if (cr0_may0 & CR0_NE)
		cr0_write_register(guest, NULL, cr0_ne_handler, NULL, CR0_NE);

	cr4_write_register(guest, cr4_vmxe_pre_handler, cr4_vmxe_handler, NULL, CR4_VMXE);

	if (cr4_may1 & CR4_SMXE)
		cr4_write_register(guest, cr4_smxe_pre_handler, cr4_smxe_handler, NULL, CR4_SMXE);
}

void cr_write_init(void)
{
	uint64_t cr0_may0;
	uint64_t cr4_may0;

	get_cr0_cap(&cr0_may0);
	get_cr4_cap(&cr4_may0);
	// make sure there's no new bits set in cr0_may0 except below bits
	VMM_ASSERT_EX(((cr0_may0 | CR0_PG | CR0_NE | CR0_PE) == (CR0_PG | CR0_NE | CR0_PE)),
			"unexpected cr0_may0\n");
	// make sure there's no new bits set in cr4_may0 except below bits
	VMM_ASSERT_EX(((cr4_may0 | CR4_VMXE) == CR4_VMXE),
			"unexpected cr4_may0\n");
}

void vmexit_cr_access(guest_cpu_handle_t gcpu)
{
	vmx_exit_qualification_t qualification;
	uint64_t write_value;

	D(VMM_ASSERT(gcpu));

	qualification.uint64 =
		vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);

	switch (qualification.cr_access.access_type)
	{
		case 0: /* move to CR */
			write_value = gcpu_get_gp_reg(gcpu, qualification.cr_access.move_gpr);

			if (qualification.cr_access.number == 0)
				cr0_guest_write(gcpu, write_value);
			else if (qualification.cr_access.number == 4)
				cr4_guest_write(gcpu, write_value);
			else
				VMM_DEADLOOP();
			break;
		case 3: /* LMSW, update lowest 4 bits in CR0: PE, MP, EM, TS*/
			write_value = gcpu_get_visible_cr0(gcpu);
			write_value &= (~0xFULL); // clear lowest 4 bits
			write_value |= (uint64_t)(qualification.cr_access.lmsw_data & 0xF); // set lowest 4 bit from LMSW data
			cr0_guest_write(gcpu, write_value);
			break;
		default:
			print_panic("%s(): unsupported access type %llu, qualification=0x%llx\n",
				__FUNCTION__, qualification.cr_access.access_type, qualification.uint64);
			VMM_DEADLOOP();
			break;
	}

	/* crX_guest_write() will update whole shadow and some bits (cared by host) in guest_cr
	** not skip instruction here. CR will be written from guest again. since shadow is updated,
	** there will be no VMExit again.
	** in the second write, the bits not cared by host will be checked by HW to determine
	** whether to generate #GP. so, we don't need to do the judgement in host.
	*/
}

