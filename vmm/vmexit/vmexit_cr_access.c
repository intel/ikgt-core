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

void cr0_write_register(uint16_t guest_id, cr_write_handler handler, uint64_t mask)
{
	uint32_t i;
	guest_handle_t guest = guest_handle(guest_id);

	VMM_ASSERT_EX(handler, "cr0 write handler is NULL\n");
	VMM_ASSERT_EX(mask, "cr0 write mask is 0\n");
	VMM_ASSERT_EX(((mask & guest->cr0_mask) == 0),
			"mask(0x%llx) has already been registered\n", mask);

	for (i=0; i<CR_HANDLER_NUM; i++)
	{
		if (guest->cr0_handlers[i] == NULL)
		{
			guest->cr0_handlers[i] = handler;
			guest->cr0_mask |= mask;
			return;
		}
	}

	print_panic("cr0 write handler is full\n");
	VMM_DEADLOOP();
}

void cr4_write_register(uint16_t guest_id, cr_write_handler handler, uint64_t mask)
{
	uint32_t i;
	guest_handle_t guest = guest_handle(guest_id);

	VMM_ASSERT_EX(handler, "cr4 write handler is NULL\n");
	VMM_ASSERT_EX(mask, "cr4 write mask is 0\n");
	VMM_ASSERT_EX(((mask & guest->cr4_mask) == 0),
			"mask(0x%llx) has already been registered\n", mask);

	for (i=0; i<CR_HANDLER_NUM; i++)
	{
		if (guest->cr4_handlers[i] == NULL)
		{
			guest->cr4_handlers[i] = handler;
			guest->cr4_mask |= mask;
			return;
		}
	}

	print_panic("cr4 write handler is full\n");
	VMM_DEADLOOP();
}

static boolean_t call_cr0_write_handlers(guest_cpu_handle_t gcpu, uint64_t write_value, uint64_t* cr_value)
{
	uint32_t i;
	guest_handle_t guest_handle = gcpu->guest;

	for (i=0; i<CR_HANDLER_NUM; i++)
	{
		if (guest_handle->cr0_handlers[i] == NULL)
			break;
		if (guest_handle->cr0_handlers[i](gcpu, write_value, cr_value))
			return TRUE;
	}
	return FALSE;
}

static boolean_t call_cr4_write_handlers(guest_cpu_handle_t gcpu, uint64_t write_value, uint64_t* cr_value)
{
	uint32_t i;
	guest_handle_t guest_handle = gcpu->guest;

	for (i=0; i<CR_HANDLER_NUM; i++)
	{
		if (guest_handle->cr4_handlers[i] == NULL)
			break;
		if (guest_handle->cr4_handlers[i](gcpu, write_value, cr_value))
			return TRUE;
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
	uint64_t cr0_value;
	vmcs_obj_t vmcs;

	D(VMM_ASSERT(gcpu));
	vmcs = gcpu->vmcs;

	cr0_value = vmcs_read(vmcs, VMCS_GUEST_CR0);
	if (call_cr0_write_handlers(gcpu, write_value, &cr0_value))
	{
		gcpu_inject_gp0(gcpu);
		return TRUE;
	}
	else
	{
		vmcs_write(vmcs, VMCS_CR0_SHADOW, write_value);
		vmcs_write(vmcs, VMCS_GUEST_CR0, cr0_value);
		return FALSE;
	}
}

/* same as cr0_guest_write() */
boolean_t cr4_guest_write(guest_cpu_handle_t gcpu, uint64_t write_value)
{
	uint64_t cr4_value;
	vmcs_obj_t vmcs;

	D(VMM_ASSERT(gcpu));
	vmcs = gcpu->vmcs;

	cr4_value = vmcs_read(vmcs, VMCS_GUEST_CR4);
	if (call_cr4_write_handlers(gcpu, write_value, &cr4_value))
	{
		gcpu_inject_gp0(gcpu);
		return TRUE;
	}
	else
	{
		 /* TODO: check may1/may0 on the cr_value.
		 ** CR0.may0 is affected by whether EPT is enabled, it makes the logic complicated
		 ** that's why it is not checked now.
		 */
		vmcs_write(vmcs, VMCS_CR4_SHADOW, write_value);
		vmcs_write(vmcs, VMCS_GUEST_CR4, cr4_value);
		return FALSE;
	}

}

static boolean_t cr0_ne_handler(UNUSED guest_cpu_handle_t gcpu, UNUSED uint64_t write_value, uint64_t* cr_value)
{
	// CR0.NE is always 1 due to HW requirement
	*cr_value |= CR0_NE;
	return FALSE;
}

static boolean_t cr4_vmxe_handler(UNUSED guest_cpu_handle_t gcpu, uint64_t write_value, uint64_t* cr_value)
{
	if (write_value & CR4_VMXE) // nested VT is not supported by eVmm
	{
		print_warn("%s(), write_value=0x%llx, injecting #GP\n", __FUNCTION__, write_value);
		return TRUE;
	}
	// CR4.VMXE is always 1 due to HW requirement
	*cr_value |= CR4_VMXE;
	return FALSE;
}

static boolean_t cr4_smxe_handler(UNUSED guest_cpu_handle_t gcpu, uint64_t write_value, uint64_t* cr_value)
{
	if (write_value & CR4_SMXE) // smx is not allowed by eVmm
	{
		print_warn("%s(), write_value=0x%llx, injecting #GP\n", __FUNCTION__, write_value);
		return TRUE;
	}
	// CR4.SMXE is always 0
	*cr_value &= ~CR4_SMXE;
	return FALSE;
}

void cr_write_gcpu_init(guest_cpu_handle_t gcpu)
{
	guest_handle_t guest_handle;

	D(VMM_ASSERT(gcpu));

	guest_handle = gcpu->guest;
	vmcs_write(gcpu->vmcs, VMCS_CR0_MASK, guest_handle->cr0_mask);
	vmcs_write(gcpu->vmcs, VMCS_CR4_MASK, guest_handle->cr4_mask);
}

void cr_write_guest_init(uint16_t guest_id)
{
	uint64_t cr0_may0;
	uint64_t cr4_may1;

	get_cr0_cap(&cr0_may0);
	cr4_may1 = get_cr4_cap(NULL);

	if (cr0_may0 & CR0_NE)
		cr0_write_register(guest_id, cr0_ne_handler, CR0_NE);

	cr4_write_register(guest_id, cr4_vmxe_handler, CR4_VMXE);

	if (cr4_may1 & CR4_SMXE)
		cr4_write_register(guest_id, cr4_smxe_handler, CR4_SMXE);
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

