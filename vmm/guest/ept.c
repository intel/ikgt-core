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

#include "dbg.h"
#include "vmx_cap.h"
#include "gcpu.h"
#include "vmcs.h"
#include "guest.h"
#include "ept.h"
#include "gpm.h"
#include "vmexit_cr_access.h"
#include "hmm.h"
#include "event.h"
#include "vmm_arch.h"

/* ept_set_pdptes() will only be called when policy.ug_real_mode=0(always enable EPT).
 * it is bacause when policy.ug_real_mode=1, in cr0_pg_hanlder():
 * 1. when the PG in write_value is set, ept will be disabled. when ept is disabled,
 *    VMCS_GUES_PDPTEx are ignored in vmentry.
 * 2. when the PG in write_value is cleared, ept will be enabled. but in this case,
 *    the guest paging mode is definitely not PAE(since PG is 0, paging is not used)
 *    so, the only possible caller of this function is ept_gcpu_init() with
 *    policy.ug_real_mode=0. the initial gcpu paging mode might be PAE.
 */
static void ept_set_pdptes(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs = gcpu->vmcs;
	guest_handle_t guest = gcpu->guest;
	uint64_t cr0, cr4, efer, cr3, pdpt_gpa;
	uint64_t* pdpt_hva;

	cr4 = vmcs_read(vmcs, VMCS_GUEST_CR4);
	cr0 = vmcs_read(vmcs, VMCS_GUEST_CR0);
	efer = vmcs_read(vmcs, VMCS_GUEST_EFER);

	if ((cr0 & CR0_PG) && (cr4 & CR4_PAE) && ((efer & EFER_LME) == 0))
	{
		cr3 = vmcs_read(vmcs, VMCS_GUEST_CR3);
		pdpt_gpa = cr3 & MASK64_MID(31, 5); // bit[63:32] and bit[4:0] will be ignored.
		VMM_ASSERT_EX(gpm_gpa_to_hva(guest, pdpt_gpa,
			GUEST_CAN_READ | GUEST_CAN_WRITE, (uint64_t *)(&pdpt_hva)),
			"guest %d failed to convert gpa 0x%llX to hva\n", guest->id, pdpt_gpa);
		vmcs_write(vmcs, VMCS_GUEST_PDPTR0, pdpt_hva[0]);
		vmcs_write(vmcs, VMCS_GUEST_PDPTR1, pdpt_hva[1]);
		vmcs_write(vmcs, VMCS_GUEST_PDPTR2, pdpt_hva[2]);
		vmcs_write(vmcs, VMCS_GUEST_PDPTR3, pdpt_hva[3]);
	}
	else
	{
		vmcs_write(vmcs, VMCS_GUEST_PDPTR0, 0);
		vmcs_write(vmcs, VMCS_GUEST_PDPTR1, 0);
		vmcs_write(vmcs, VMCS_GUEST_PDPTR2, 0);
		vmcs_write(vmcs, VMCS_GUEST_PDPTR3, 0);
	}
}

static void ept_enable(guest_cpu_handle_t gcpu, boolean_t enable_ug)
{
	vmcs_obj_t vmcs = gcpu->vmcs;
	uint32_t proc2;

	proc2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	if ((proc2 & PRO2C_ENABLE_EPT) == 0)
	{
		proc2 |= PRO2C_ENABLE_EPT;
		if (enable_ug)
			proc2 |= PROC2_UNRESTRICTED_GUEST;
		vmcs_write(vmcs, VMCS_PROC_CTRL2, proc2);
		vmcs_write(vmcs, VMCS_EPTP_ADDRESS, gcpu->guest->eptp);
		print_trace("%s(ug=%d) on guest %d cpu%d\n",
				__FUNCTION__, enable_ug, gcpu->guest->id, gcpu->id);
	}
}

static void ept_disable(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs = gcpu->vmcs;
	uint32_t proc2;

	proc2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	if (proc2 & PRO2C_ENABLE_EPT)
	{
		proc2 &= ~(PRO2C_ENABLE_EPT);
		proc2 &= ~(PROC2_UNRESTRICTED_GUEST);
		vmcs_write(vmcs, VMCS_PROC_CTRL2, proc2);
		vmcs_write(vmcs, VMCS_EPTP_ADDRESS, 0);
		print_trace("%s() on guest %d cpu%d\n", __FUNCTION__, gcpu->guest->id, gcpu->id);
	}
}

// cr0_pg_handler() is registered for policy "ug_real_mode" only
static boolean_t cr0_pg_pre_handler(uint64_t write_value)
{

	if (write_value & CR0_PG)
	{
		if ((write_value & CR0_PE) == 0)
		{
			print_warn("%s(), write_value=0x%llx, injecting #GP\n", __FUNCTION__, write_value);
			return TRUE;
		}
	}

	return FALSE;
}

static void cr0_pg_handler(uint64_t write_value, uint64_t* cr_value)
{
	uint64_t mask;

	// change both PG and PE, if no need to inject #GP
	mask = CR0_PG | CR0_PE;
	*cr_value = (*cr_value&~mask) | (write_value&mask);
}

static void cr0_pg_post_handler(guest_cpu_handle_t gcpu)
{
	uint64_t cr0;

	cr0 = gcpu_get_visible_cr0(gcpu);

	if (cr0 & CR0_PG)
	{
		ept_disable(gcpu);
	}
	else
	{
		ept_enable(gcpu, TRUE);
	}
	// update guest mode
	gcpu_update_guest_mode(gcpu);
}

void vmexit_ept_violation(UNUSED guest_cpu_handle_t gcpu)
{
	vmx_exit_qualification_t qualification;
	event_ept_violation_t event_ept_violation;

	D(VMM_ASSERT(gcpu));

	event_ept_violation.handled = FALSE;

	event_raise(gcpu, EVENT_EPT_VIOLATION, (void *)&event_ept_violation);
	if (TRUE == event_ept_violation.handled) {
		return;
	}

	qualification.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);

	print_panic("%s(guest%d, cpu%d):\n", __FUNCTION__,
		gcpu->guest->id, host_cpu_id());
	print_panic("\tqualification=0x%llx\n", qualification.uint64);
	print_panic("\tgpa=0x%llx\n",
		vmcs_read(gcpu->vmcs, VMCS_GUEST_PHY_ADDR));
	if (qualification.ept_violation.gla_validity)
	{
		print_panic("\tgva=0x%llx\n",
			vmcs_read(gcpu->vmcs, VMCS_GUEST_LINEAR_ADDR));
	}
	print_panic("\tRIP=0x%llx\n", vmcs_read(gcpu->vmcs, VMCS_GUEST_RIP));

	VMM_DEADLOOP();
}

void vmexit_ept_misconfiguration(UNUSED guest_cpu_handle_t gcpu)
{
	D(VMM_ASSERT(gcpu));

	print_panic("%s(guest%d, cpu%d):\n", __FUNCTION__,
		gcpu->guest->id, host_cpu_id());
	print_panic("\tgpa=0x%llx\n",
		vmcs_read(gcpu->vmcs, VMCS_GUEST_PHY_ADDR));

	VMM_DEADLOOP();
}

static uint64_t ept_calculate_eptp(guest_handle_t guest)
{
	vmx_ept_vpid_cap_t ept_vpid_cap;
	eptp_t eptp;

	ept_vpid_cap.uint64 = get_ept_vpid_cap();
	VMM_ASSERT_EX(ept_vpid_cap.bits.page_walk_len_4,
		"ept page-walk length is not 4\n");

	// setp eptp.bits.pml4_hpa. return from mam_get_table_hpa() is page aligned
	eptp.uint64 = mam_get_table_hpa(guest->gpa_to_hpa);
	D(VMM_ASSERT((eptp.uint64 & 0xFFF) == 0));
	if (ept_vpid_cap.bits.wb)
	{
		eptp.bits.emt = CACHE_TYPE_WB;
	}
	else
	{
		VMM_ASSERT_EX(ept_vpid_cap.bits.uc,
			"ept_vpid_cap.bits.uc is not supported\n");
		eptp.bits.emt = CACHE_TYPE_UC;
	}
	eptp.bits.gaw = 3;
	return eptp.uint64;
}

void ept_guest_init(guest_handle_t guest)
{
	D(VMM_ASSERT(guest));

	if (guest->ept_policy.bits.enabled)
	{
		//PRO2C_ENABLE_EPT is checked in vmx_cap_init()
		guest->eptp = ept_calculate_eptp(guest);
		if (guest->ept_policy.bits.ug_real_mode)
		{
			VMM_ASSERT_EX((get_proctl2_cap(NULL) & PROC2_UNRESTRICTED_GUEST),
				"the unrestricted guest is not supported\n");
			cr0_write_register(guest, cr0_pg_pre_handler, cr0_pg_handler, cr0_pg_post_handler, CR0_PG);
		}
	}
}

void ept_gcpu_init(guest_cpu_handle_t gcpu)
{
	uint32_t proc2_may1;

	D(VMM_ASSERT(gcpu));

	proc2_may1 = get_proctl2_cap(NULL);

	// enable EPT/UG
	if (gcpu->guest->ept_policy.bits.enabled)
	{
		if (gcpu->guest->ept_policy.bits.ug_real_mode == 0) // always enable EPT.
		{
			// enable UG if supported
			ept_enable(gcpu, ((proc2_may1 & PROC2_UNRESTRICTED_GUEST)!= 0));
			ept_set_pdptes(gcpu);
		}
	}
}


