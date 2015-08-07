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

#include "file_codes.h"
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(EPT_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(EPT_C, __condition)
#include "mon_callback.h"
#include "vmcs_init.h"
#include "guest_cpu.h"
#include "event_mgr.h"
#include "mon_events_data.h"
#include "vmcs_api.h"
#include "guest.h"
#include "ept.h"
#include "policy_manager.h"
#include "memory_allocator.h"
#include "memory_address_mapper_api.h"
#include "gpm_api.h"
#include "hw_utils.h"
#include "mtrrs_abstraction.h"
#include "libc.h"
#include "host_memory_manager_api.h"
#include "ept_hw_layer.h"
#include "ipc.h"
#include "guest_cpu_vmenter_event.h"
#include "lock.h"
#include "scheduler.h"
#include "page_walker.h"
#include "guest_cpu_internal.h"
#include "unrestricted_guest.h"
#include "fvs.h"
#include "ve.h"

ept_state_t ept;
hpa_t redirect_physical_addr = 0;


/* macro #define's */
#define PDPTR_NXE_DISABLED_RESERVED_BITS_MASK ((uint64_t)0xffffff00000001e6)
#define PDPTR_NXE_ENABLED_RESERVED_BITS_MASK  ((uint64_t)0x7fffff00000001e6)
#define PRESENT_BIT                           ((uint64_t)0x1)

/* static functions */
static
boolean_t ept_guest_cpu_initialize(guest_cpu_handle_t gcpu);

boolean_t ept_page_walk(uint64_t first_table, uint64_t addr, uint32_t gaw);

void ept_set_remote_eptp(cpu_id_t from, void *arg);

void ept_set_pdtprs(guest_cpu_handle_t gcpu, uint64_t cr4_value)
{
	uint64_t pdpt[4];
	boolean_t status = TRUE;
	boolean_t pdptr_required = FALSE;

	if (cr4_value & CR4_PAE) {
		/* PAE mode */
		uint64_t efer = gcpu_get_msr_reg(gcpu, IA32_MON_MSR_EFER);

		if (0 == (efer & EFER_LME)) {
			/* 32-bit mode */
			status = gcpu_get_32_bit_pdpt(gcpu, pdpt)
				 && pw_is_pdpt_in_32_bit_pae_mode_valid(gcpu,
				pdpt);
			if (TRUE == status) {
				pdptr_required = TRUE;
				ept_hw_set_pdtprs(gcpu, pdpt);
			}
		}
	}

	if (FALSE == pdptr_required) {
		mon_zeromem(pdpt, sizeof(pdpt));
		ept_hw_set_pdtprs(gcpu, pdpt);
	}
}

void ept_acquire_lock(void)
{
	if (ept.lock.owner_cpu_id == hw_cpu_id()) {
		ept.lock_count++;
		return;
	}
	interruptible_lock_acquire(&ept.lock);
	ept.lock_count = 1;
}

void ept_release_lock(void)
{
	ept.lock_count--;
	if (ept.lock_count == 0) {
		lock_release(&ept.lock);
	}
}

boolean_t mon_ept_is_cpu_in_non_paged_mode(guest_id_t guest_id)
{
	ept_guest_state_t *ept_guest = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;
	uint32_t i = 0;
	guest_cpu_handle_t gcpu =
		mon_scheduler_get_current_gcpu_for_guest(guest_id);

	/* for UG system, flat page table will never be used, so, this function
	 * should always return FALSE. */
	if (mon_is_unrestricted_guest_enabled(gcpu)) {
		return FALSE;
	}

	ept_guest = ept_find_guest_state(guest_id);
	MON_ASSERT(ept_guest);

	for (i = 0; i < ept.num_of_cpus; i++) {
		ept_guest_cpu = ept_guest->gcpu_state[i];
		MON_ASSERT(ept_guest_cpu);
		if (ept_guest_cpu->is_initialized &&
		    (ept_guest_cpu->cr0 & CR0_PG) == 0) {
			/* cannot change perms - another gcpu not paged and uses flat page
			 * tables */
			return TRUE;
		}
	}

	return FALSE;
}

/* EPT vmexits */
/**************************************************************
 *  Function name: ept_violation_vmexit
 *  Parameters: Function does not validate gcpu. Assumes valid.
 ***************************************************************/
boolean_t ept_violation_vmexit(guest_cpu_handle_t gcpu, void *pv)
{
	report_ept_violation_data_t violation_data;
	event_gcpu_ept_violation_data_t *data =
		(event_gcpu_ept_violation_data_t *)pv;
	const virtual_cpu_id_t *vcpu_id = NULL;
	ia32_vmx_exit_qualification_t ept_violation_qualification;

	vcpu_id = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu_id);
	/* Report EPT violation to the VIEW module */
	violation_data.qualification = data->qualification.uint64;
	violation_data.guest_linear_address = data->guest_linear_address;
	violation_data.guest_physical_address = data->guest_physical_address;

	ept_violation_qualification.uint64 = violation_data.qualification;
	if (ept_violation_qualification.ept_violation.nmi_unblocking) {
		vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
		ia32_vmx_vmcs_vmexit_info_idt_vectoring_t idt_vectoring_info;

		idt_vectoring_info.uint32 =
			(uint32_t)mon_vmcs_read(vmcs,
				VMCS_EXIT_INFO_IDT_VECTORING);

		if (!idt_vectoring_info.bits.valid) {
			ia32_vmx_vmcs_guest_interruptibility_t
				guest_interruptibility;

			guest_interruptibility.uint32 =
				(uint32_t)mon_vmcs_read(vmcs,
					VMCS_GUEST_INTERRUPTIBILITY);
			guest_interruptibility.bits.block_nmi = 1;
			mon_vmcs_write(vmcs, VMCS_GUEST_INTERRUPTIBILITY,
				(uint64_t)guest_interruptibility.uint32);
		}
	}

	if (!report_mon_event
		    (MON_EVENT_EPT_VIOLATION, (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)vcpu_id, (void *)&violation_data)) {
		MON_LOG(mask_anonymous,
			level_trace,
			"report_ept_violation failed\n");
	}

	data->processed = TRUE;
	return TRUE;
}


boolean_t ept_misconfiguration_vmexit(guest_cpu_handle_t gcpu UNUSED, void *pv)
{
	eptp_t eptp;
	event_gcpu_ept_misconfiguration_data_t *data =
		(event_gcpu_ept_misconfiguration_data_t *)pv;

	EPT_PRINTERROR("\r\n****EPT Misconfiguration:****\n");
	EPT_PRINTERROR("gpa_t=%p\n", data->guest_physical_address);

	eptp.uint64 = ept_get_eptp(gcpu);
	MON_LOG(mask_anonymous,
		level_trace,
		"eptp_t.etmt: 0x%X eptp_t.gaw: 0x%X eptp_t.ASR: 0x%X\n",
		eptp.bits.etmt,
		eptp.bits.gaw,
		eptp.uint64 & ~PAGE_4KB_MASK);
	MON_LOG(mask_anonymous, level_trace, "Is native %p\r\n",
		gcpu_is_native_execution(gcpu));
	ept_page_walk((uint64_t)eptp.uint64 & ~PAGE_4KB_MASK,
		data->guest_physical_address,
		ept_hw_get_guest_address_width_from_encoding((uint32_t)
			eptp.bits.gaw));

	MON_DEADLOOP();

	data->processed = TRUE;
	return TRUE;
}

mam_ept_super_page_support_t mon_ept_get_mam_super_page_support(void)
{
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();
	ia32_vmx_ept_vpid_cap_t ept_cap = hw_constraints->ept_vpid_capabilities;
	mam_ept_super_page_support_t sp_support = MAM_EPT_NO_SUPER_PAGE_SUPPORT;

	/* Currently we support 2MB pages in implementation */
	if (ept_cap.bits.sp_21_bit) {
		sp_support |= MAM_EPT_SUPPORT_2MB_PAGE;
	}
#if 0
	if (ept_cap.bits.sp_30_bit) {
		sp_support |= MAM_EPT_SUPPORT_1GB_PAGE;
	}
	if (ept_cap.bits.sp_39_bit) {
		sp_support |= MAM_EPT_SUPPORT_512_GB_PAGE;
	}
#endif
	return sp_support;
}

void ept_get_current_ept(guest_cpu_handle_t gcpu, uint64_t *ept_root_table_hpa,
			 uint32_t *ept_gaw)
{
	const virtual_cpu_id_t *vcpu_id = NULL;
	ept_guest_state_t *ept_guest = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;

	MON_ASSERT(gcpu);

	vcpu_id = mon_guest_vcpu(gcpu);
	/* paranoid check. If assertion fails, possible memory corruption. */
	MON_ASSERT(vcpu_id);
	ept_guest = ept_find_guest_state(vcpu_id->guest_id);
	MON_ASSERT(ept_guest);
	ept_guest_cpu = ept_guest->gcpu_state[vcpu_id->guest_cpu_id];
	MON_ASSERT(ept_guest_cpu);

	*ept_root_table_hpa = ept_guest_cpu->active_ept_root_table_hpa;
	*ept_gaw = ept_guest_cpu->active_ept_gaw;
}

void mon_ept_set_current_ept(guest_cpu_handle_t gcpu,
			     uint64_t ept_root_table_hpa,
			     uint32_t ept_gaw)
{
	const virtual_cpu_id_t *vcpu_id = NULL;
	ept_guest_state_t *ept_guest = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;

	MON_ASSERT(gcpu);

	vcpu_id = mon_guest_vcpu(gcpu);
	/* paranoid check. If assertion fails, possible memory corruption. */
	MON_ASSERT(vcpu_id);
	ept_guest = ept_find_guest_state(vcpu_id->guest_id);
	MON_ASSERT(ept_guest);
	ept_guest_cpu = ept_guest->gcpu_state[vcpu_id->guest_cpu_id];
	MON_ASSERT(ept_guest_cpu);

	ept_guest_cpu->active_ept_root_table_hpa = ept_root_table_hpa;
	ept_guest_cpu->active_ept_gaw = ept_gaw;
}

void ept_get_default_ept(guest_handle_t guest, uint64_t *ept_root_table_hpa,
			 uint32_t *ept_gaw)
{
	ept_guest_state_t *ept_guest = NULL;

	MON_ASSERT(guest);

	ept_guest = ept_find_guest_state(guest_get_id(guest));
	MON_ASSERT(ept_guest);

	*ept_root_table_hpa = ept_guest->ept_root_table_hpa;
	*ept_gaw = ept_guest->gaw;
}

void ept_create_default_ept(guest_handle_t guest, gpm_handle_t gpm)
{
	ept_guest_state_t *ept_guest = NULL;

	MON_ASSERT(guest);
	MON_ASSERT(gpm);

	ept_guest = ept_find_guest_state(guest_get_id(guest));
	MON_ASSERT(ept_guest);

	if (ept_guest->address_space != MAM_INVALID_HANDLE) {
		mon_mam_destroy_mapping(ept_guest->address_space);
		ept_guest->address_space = MAM_INVALID_HANDLE;
	}

	ept_guest->gaw =
		mon_ept_hw_get_guest_address_width(
			mon_ept_get_guest_address_width(gpm));
	MON_ASSERT(ept_guest->gaw != (uint32_t)-1);

	ept_guest->address_space =
		mon_ept_create_guest_address_space(gpm, TRUE);
	MON_ASSERT(mon_mam_convert_to_ept(ept_guest->address_space,
			mon_ept_get_mam_super_page_support(),
			mon_ept_get_mam_supported_gaw(ept_guest->gaw),
			mon_ve_is_hw_supported(),
			&(ept_guest->ept_root_table_hpa)));
}

mam_ept_supported_gaw_t mon_ept_get_mam_supported_gaw(uint32_t gaw)
{
	return (mam_ept_supported_gaw_t)
	       mon_ept_hw_get_guest_address_width_encoding(gaw);
}

static
boolean_t ept_begin_gpm_modification_before_cpus_stop(
	guest_cpu_handle_t gcpu UNUSED,
	void *pv UNUSED)
{
	ept_acquire_lock();
	return TRUE;
}

static
boolean_t ept_end_gpm_modification_before_cpus_resume(guest_cpu_handle_t gcpu,
						      void *pv)
{
	guest_handle_t guest = NULL;
	ept_set_eptp_cmd_t set_eptp_cmd;
	ept_invept_cmd_t invept_cmd;
	ipc_destination_t ipc_dest;
	event_gpm_modification_data_t *gpm_modification_data =
		(event_gpm_modification_data_t *)pv;
	uint64_t default_ept_root_table_hpa;
	uint32_t default_ept_gaw;

	MON_ASSERT(pv);

	guest = mon_guest_handle(gpm_modification_data->guest_id);
	if (gpm_modification_data->operation == MON_MEM_OP_UPDATE) {
		ept_get_default_ept(guest, &default_ept_root_table_hpa,
			&default_ept_gaw);
		invept_cmd.host_cpu_id = ANY_CPU_ID;
		invept_cmd.cmd = INVEPT_CONTEXT_WIDE;
		invept_cmd.eptp =
			mon_ept_compute_eptp(guest, default_ept_root_table_hpa,
				default_ept_gaw);

		mon_ept_invalidate_ept(ANY_CPU_ID, &invept_cmd);

		ipc_dest.addr_shorthand = IPI_DST_ALL_EXCLUDING_SELF;
		ipc_execute_handler_sync(ipc_dest, mon_ept_invalidate_ept,
			(void *)&invept_cmd);
	} else if (gpm_modification_data->operation == MON_MEM_OP_RECREATE) {
		/* Recreate Default EPT */
		ept_create_default_ept(guest, mon_guest_get_startup_gpm(guest));

		ept_get_default_ept(guest, &default_ept_root_table_hpa,
			&default_ept_gaw);

		/* Reset the Default EPT on current CPU */
		ept_set_eptp(gcpu, default_ept_root_table_hpa, default_ept_gaw);

		invept_cmd.host_cpu_id = ANY_CPU_ID;
		invept_cmd.cmd = INVEPT_CONTEXT_WIDE;
		invept_cmd.eptp =
			mon_ept_compute_eptp(guest,
				default_ept_root_table_hpa,
				default_ept_gaw);
		mon_ept_invalidate_ept(ANY_CPU_ID, &invept_cmd);

		set_eptp_cmd.guest_id = gpm_modification_data->guest_id;
		set_eptp_cmd.ept_root_table_hpa = default_ept_root_table_hpa;
		set_eptp_cmd.gaw = default_ept_gaw;
		set_eptp_cmd.invept_cmd = &invept_cmd;

		ipc_dest.addr_shorthand = IPI_DST_ALL_EXCLUDING_SELF;
		ipc_execute_handler_sync(ipc_dest, ept_set_remote_eptp,
			(void *)&set_eptp_cmd);
	} else {
		/* switch */
		MON_ASSERT(gpm_modification_data->operation ==
			MON_MEM_OP_SWITCH);
	}

	return TRUE;
}

static
boolean_t ept_end_gpm_modification_after_cpus_resume(
	guest_cpu_handle_t gcpu UNUSED,
	void *pv UNUSED)
{
	ept_release_lock();

	return TRUE;
}

static
boolean_t ept_cr0_update(guest_cpu_handle_t gcpu, void *pv)
{
	uint64_t value =
		((event_gcpu_guest_cr_write_data_t *)pv)->
		new_guest_visible_value;
	boolean_t pg;
	boolean_t prev_pg = 0;
	const virtual_cpu_id_t *vcpu_id = NULL;
	ept_guest_state_t *ept_guest = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;
	uint64_t cr4;
	ia32_efer_t efer;
	vmentry_controls_t entry_ctrl_mask;
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	ept_acquire_lock();

	vcpu_id = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu_id);
	ept_guest = ept_find_guest_state(vcpu_id->guest_id);
	MON_ASSERT(ept_guest);
	ept_guest_cpu = ept_guest->gcpu_state[vcpu_id->guest_cpu_id];

	prev_pg = (ept_guest_cpu->cr0 & CR0_PG) != 0;
	ept_guest_cpu->cr0 = value;
	pg = (ept_guest_cpu->cr0 & CR0_PG) != 0;

	if (mon_is_unrestricted_guest_supported()) {
		/* IA Manual 3B: 27.9.4: IA32_EFER.LMA is always set by the processor
		 * to equal IA32_EFER.LME & CR0.PG
		 * Update LMA and IA32e bits based on LME and PG bit on systems with UG
		 * Set VMCS.GUEST.EFER_MSR.LMA = (GUEST.CR0.PG & GUEST.EFER.LME)
		 * Set VMCS.ENTRY_CONTROL.IA32e = (GUEST.CR0.PG & GUEST.EFER.LME)
		 * On systems w/o UG, LMA and IA32e are updated when EFER.LME is
		 * updated, since PG is always 1 */
		efer.uint64 = gcpu_get_msr_reg(gcpu, IA32_MON_MSR_EFER);
		efer.bits.lma = (pg & efer.bits.lme);
		gcpu_set_msr_reg(gcpu, IA32_MON_MSR_EFER, efer.uint64);
		entry_ctrl_mask.uint32 = 0;
		entry_ctrl_mask.bits.ia32e_mode_guest = 1;
		vmcs_update(vmcs, VMCS_ENTER_CONTROL_VECTOR,
			(efer.bits.lma) ? UINT64_ALL_ONES : 0,
			(uint64_t)entry_ctrl_mask.uint32);
	}

	if (pg != prev_pg) {
		/* INVVPID for this guest */
		ept_hw_invvpid_single_context(1 + gcpu->vcpu.guest_id);
	}

	if ((pg) && (pg != prev_pg)) {
		/* Enable EPT on systems w/o UG, when PG is turned on */
		if (!mon_is_unrestricted_guest_supported() &&
		    !mon_ept_is_ept_enabled(gcpu)) {
			mon_ept_enable(gcpu);
		}
		cr4 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR4);
		ept_set_pdtprs(gcpu, cr4);
	}

	/* Disable EPT on systems without UG, when PG is turned off */
	if (!pg && !mon_is_unrestricted_guest_supported() &&
	    mon_ept_is_ept_enabled(gcpu)) {
		mon_ept_disable(gcpu);
	}

	/* EPT_LOG("EPT CPU#%d: ept_cr0_update %p\r\n", hw_cpu_id(),
	 * ept_guest_cpu->cr0); */

	ept_release_lock();
	return TRUE;
}

static
boolean_t ept_cr3_update(guest_cpu_handle_t gcpu, void *pv UNUSED)
{
	const virtual_cpu_id_t *vcpu_id = NULL;
	ept_guest_state_t *ept_guest = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;

	ept_acquire_lock();

	vcpu_id = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu_id);
	ept_guest = ept_find_guest_state(vcpu_id->guest_id);
	MON_ASSERT(ept_guest);
	ept_guest_cpu = ept_guest->gcpu_state[vcpu_id->guest_cpu_id];

	if ((ept_guest_cpu->cr0 & CR0_PG) &&
	    (ept_guest_cpu->cr4 & CR4_PAE)) {
		/* if paging is enabled and PAE mode is active */
		ept_set_pdtprs(gcpu, ept_guest_cpu->cr4);
	}

	/* Flush TLB */
	ept_hw_invvpid_single_context(1 + gcpu->vcpu.guest_id);

	ept_release_lock();

	return TRUE;
}

static
boolean_t ept_cr4_update(guest_cpu_handle_t gcpu, void *pv)
{
	uint64_t new_cr4 =
		((event_gcpu_guest_cr_write_data_t *)pv)->
		new_guest_visible_value;
	boolean_t pg;
	boolean_t pae = 0;
	boolean_t prev_pae = 0;
	const virtual_cpu_id_t *vcpu_id = NULL;
	ept_guest_state_t *ept_guest = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;
	uint64_t cr4;

	ept_acquire_lock();

	vcpu_id = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu_id);
	ept_guest = ept_find_guest_state(vcpu_id->guest_id);
	MON_ASSERT(ept_guest);
	ept_guest_cpu = ept_guest->gcpu_state[vcpu_id->guest_cpu_id];

	prev_pae = (ept_guest_cpu->cr4 & CR4_PAE) != 0;

	ept_guest_cpu->cr4 = new_cr4;

	pg = (ept_guest_cpu->cr0 & CR0_PG) != 0;
	pae = (ept_guest_cpu->cr4 & CR4_PAE) != 0;

	if (mon_ept_is_ept_enabled(gcpu) && pae != prev_pae) {
		cr4 = ept_guest_cpu->cr4;
		ept_set_pdtprs(gcpu, cr4);
	}

	/* Flush TLB */
	ept_hw_invvpid_single_context(1 + gcpu->vcpu.guest_id);

	ept_release_lock();

	/* EPT_LOG("EPT CPU#%d: ept_cr4_update %p\r\n", hw_cpu_id(),
	 * ept_guest_cpu->cr4); */
	return TRUE;
}

static
boolean_t ept_emulator_enter(guest_cpu_handle_t gcpu, void *pv UNUSED)
{
	const virtual_cpu_id_t *vcpu_id = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;
	ept_guest_state_t *ept_guest_state = NULL;

	vcpu_id = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu_id);
	ept_guest_state = ept_find_guest_state(vcpu_id->guest_id);
	MON_ASSERT(ept_guest_state);
	ept_guest_cpu = ept_guest_state->gcpu_state[vcpu_id->guest_cpu_id];

	ept_guest_cpu->cr0 =
		gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR0);
	ept_guest_cpu->cr4 =
		gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR4);
	ept_guest_cpu->ept_enabled_save = FALSE;
	if (mon_ept_is_ept_enabled(gcpu)) {
		ept_guest_cpu->ept_enabled_save = TRUE;
		mon_ept_disable(gcpu);
	}

	return TRUE;
}

static
boolean_t ept_emulator_exit(guest_cpu_handle_t gcpu, void *pv UNUSED)
{
	const virtual_cpu_id_t *vcpu_id = NULL;
	ept_guest_state_t *ept_guest = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;
	event_gcpu_guest_cr_write_data_t write_data = { 0 };
	uint64_t cr0, cr4;

	ept_acquire_lock();

	vcpu_id = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu_id);
	ept_guest = ept_find_guest_state(vcpu_id->guest_id);
	MON_ASSERT(ept_guest);
	ept_guest_cpu = ept_guest->gcpu_state[vcpu_id->guest_cpu_id];

	if (ept_guest_cpu->ept_enabled_save) {
		mon_ept_enable(gcpu);
	}

	cr0 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR0);
	cr4 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR4);

	/* Do not assume that the CR0 must be changed when emulator exits.
	 * comment out this line to fix the issue "ETP disabled after S3 in
	 * ThinkCentre desktop".  */
	if (cr0 != ept_guest_cpu->cr0) {
		write_data.new_guest_visible_value = cr0;
		ept_cr0_update(gcpu, &write_data);
	}

	if (cr4 != ept_guest_cpu->cr4) {
		write_data.new_guest_visible_value = cr4;
		ept_cr4_update(gcpu, &write_data);
	}
	ept_release_lock();
	return TRUE;
}

static
void ept_register_events(guest_cpu_handle_t gcpu)
{
	event_gcpu_register(EVENT_GCPU_AFTER_GUEST_CR0_WRITE,
		gcpu,
		ept_cr0_update);
	event_gcpu_register(EVENT_GCPU_AFTER_GUEST_CR3_WRITE,
		gcpu,
		ept_cr3_update);
	event_gcpu_register(EVENT_GCPU_AFTER_GUEST_CR4_WRITE,
		gcpu,
		ept_cr4_update);
	event_gcpu_register(EVENT_EMULATOR_AS_GUEST_ENTER, gcpu,
		ept_emulator_enter);
	event_gcpu_register(EVENT_EMULATOR_AS_GUEST_LEAVE,
		gcpu,
		ept_emulator_exit);
	event_gcpu_register(EVENT_GCPU_EPT_MISCONFIGURATION, gcpu,
		ept_misconfiguration_vmexit);
	event_gcpu_register(EVENT_GCPU_EPT_VIOLATION, gcpu,
		ept_violation_vmexit);
}

INLINE
boolean_t ept_is_gcpu_active(ia32_vmx_vmcs_guest_sleep_state_t activity_state)
{
	return (IA32_VMX_VMCS_GUEST_SLEEP_STATE_WAIT_FOR_SIPI !=
		activity_state) &&
	       ((IA32_VMX_VMCS_GUEST_SLEEP_STATE_TRIPLE_FAULT_SHUTDOWN !=
		 activity_state));
}

static
void ept_gcpu_activity_state_change(guest_cpu_handle_t gcpu,
				    event_gcpu_activity_state_change_data_t *pv)
{
	const virtual_cpu_id_t *vcpu_id = NULL;
	ept_guest_state_t *ept_guest = NULL;

	MON_ASSERT(gcpu);
	MON_ASSERT(pv);

	EPT_LOG("ept CPU#%d: activity state change: new state %d\r\n",
		hw_cpu_id(),
		pv->new_state);

	vcpu_id = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu_id);
	ept_guest = ept_find_guest_state(vcpu_id->guest_id);
	MON_ASSERT(ept_guest);

	if (ept_is_gcpu_active(pv->new_state)) {
		ept_guest_cpu_initialize(gcpu);
	}
}

uint32_t mon_ept_get_guest_address_width(gpm_handle_t gpm)
{
	gpm_ranges_iterator_t gpm_iter = 0;
	gpa_t guest_range_addr = 0;
	uint64_t guest_range_size = 0;
	gpa_t guest_highest_range_addr = 0;
	uint64_t guest_highest_range_size = 0;
	uint64_t guest_address_limit = 0;
	uint32_t guest_address_limit_msb_index = 0;

	MON_ASSERT(gpm);

	gpm_iter = gpm_get_ranges_iterator(gpm);

	while (GPM_INVALID_RANGES_ITERATOR != gpm_iter) {
		/* for each range in GPM */
		gpm_iter = gpm_get_range_details_from_iterator(gpm,
			gpm_iter,
			&guest_range_addr,
			&guest_range_size);
		if (guest_range_addr > guest_highest_range_addr) {
			guest_highest_range_addr = guest_range_addr;
			guest_highest_range_size = guest_range_size;
		}
	}

	guest_address_limit = guest_highest_range_addr +
			      guest_highest_range_size;

	hw_scan_bit_backward64(&guest_address_limit_msb_index,
		guest_address_limit);

	return guest_address_limit_msb_index + 1;
}

mam_handle_t mon_ept_create_guest_address_space(gpm_handle_t gpm,
						boolean_t original_perms)
{
	mam_handle_t address_space = NULL;
	mam_attributes_t attributes = { 0 }
	, hpa_attrs;
	gpm_ranges_iterator_t gpm_iter = 0;
	gpa_t guest_range_addr = 0;
	uint64_t guest_range_size = 0;
	hpa_t host_range_addr = 0;
	boolean_t status = FALSE;
	uint64_t same_memory_type_range_size = 0, covered_guest_range_size = 0;
	mon_phys_mem_type_t mem_type;

	MON_ASSERT(gpm);

	/* if (original_perms == FALSE) then permissions = RWX (default) */
	attributes.ept_attr.readable = 1;
	attributes.ept_attr.writable = 1;
	attributes.ept_attr.executable = 1;

	address_space = mam_create_mapping(attributes);
	MON_ASSERT(address_space);

	gpm_iter = gpm_get_ranges_iterator(gpm);

	while (GPM_INVALID_RANGES_ITERATOR != gpm_iter) {
		/* for each range in GPM */
		gpm_iter = gpm_get_range_details_from_iterator(gpm,
			gpm_iter,
			&guest_range_addr,
			&guest_range_size);
		status = mon_gpm_gpa_to_hpa(gpm,
			guest_range_addr,
			&host_range_addr,
			&hpa_attrs);

		/*
		 * EPT_LOG("ept_create_guest_address_space: EPT GPM range: "
		 *         "gpa %p -> hpa %p; size %p attrs 0x%x\r\n",
		 *         guest_range_addr, host_range_addr,
		 *         guest_range_size, hpa_attrs.uint32);
		 */

		if (original_perms) {
			attributes.ept_attr.readable =
				hpa_attrs.ept_attr.readable;
			attributes.ept_attr.writable =
				hpa_attrs.ept_attr.writable;
			attributes.ept_attr.executable =
				hpa_attrs.ept_attr.executable;
		}

		if (status) {
			covered_guest_range_size = 0;
			do {    /* add separate mapping per memory type */
				mem_type =
					mtrrs_abstraction_get_range_memory_type(host_range_addr +
						covered_guest_range_size,
						&same_memory_type_range_size,
						guest_range_size -
						covered_guest_range_size);

				if (MON_PHYS_MEM_UNDEFINED == mem_type) {
					EPT_LOG
					(
						"  EPT %s:  Undefined mem-type for region %P. Use Uncached\n",
						guest_range_addr +
						covered_guest_range_size);
					mem_type = MON_PHYS_MEM_UNCACHED;
				}

				attributes.ept_attr.emt = mem_type;

				if (covered_guest_range_size +
				    same_memory_type_range_size >
				    guest_range_size) { /* normalize */
					same_memory_type_range_size =
						guest_range_size - covered_guest_range_size;
				}
				/*
				 * debug
				 */
				/* EPT_LOG(
				 *  "EPT add range: gpa %p -> hpa %p; size %p; mem_type %d\r\n",
				 *         guest_range_addr + covered_guest_range_size,
				 *         host_range_addr + covered_guest_range_size,
				 *         same_memory_type_range_size,
				 *         mem_type);
				 */
				if (!mam_insert_range(address_space,
					guest_range_addr + covered_guest_range_size,
					host_range_addr + covered_guest_range_size,
					same_memory_type_range_size,
					attributes)) {
					EPT_LOG
					(
						"EPT add range failed,"
						"gpa %p -> hpa %p; size %p; mem_type %d\r\n",
						guest_range_addr + covered_guest_range_size,
						host_range_addr + covered_guest_range_size,
						same_memory_type_range_size,
						attributes.ept_attr.emt);
					return NULL;
				}

				covered_guest_range_size +=
					same_memory_type_range_size;
			} while (covered_guest_range_size < guest_range_size);
		}
	}

	return address_space;
}

void mon_ept_invalidate_ept(cpu_id_t from UNUSED, void *arg)
{
	ept_invept_cmd_t *invept_cmd = (ept_invept_cmd_t *)arg;

	if (invept_cmd->host_cpu_id != ANY_CPU_ID &&
	    invept_cmd->host_cpu_id != hw_cpu_id()) {
		/* not for this CPU -- ignore command */
		return;
	}

	/* MON_LOG(mask_anonymous, level_trace, "Invalidate ept on CPU#%d\r\n",
	 * hw_cpu_id()); */

	switch (invept_cmd->cmd) {
	/* Not being used currently */
	case INVEPT_ALL_CONTEXTS:
		ept_hw_invept_all_contexts();
		break;

	case INVEPT_CONTEXT_WIDE:
		ept_hw_invept_context(invept_cmd->eptp);
		break;

	/* Not being used currently */
	case INVEPT_INDIVIDUAL_ADDRESS:
		ept_hw_invept_individual_address(invept_cmd->eptp,
			invept_cmd->gpa);
		break;

	default:
		MON_ASSERT(0);
	}
}

boolean_t ept_is_ept_supported(void)
{
	return ept_hw_is_ept_supported();
}

boolean_t mon_ept_is_ept_enabled(guest_cpu_handle_t gcpu)
{
	return ept_hw_is_ept_enabled(gcpu);
}

uint64_t mon_ept_compute_eptp(guest_handle_t guest, uint64_t ept_root_table_hpa,
			      uint32_t gaw)
{
	eptp_t eptp;

	MON_ASSERT(guest);
	MON_ASSERT(ept_root_table_hpa);
	MON_ASSERT(gaw);

	eptp.uint64 = ept_root_table_hpa;
	eptp.bits.gaw = mon_ept_hw_get_guest_address_width_encoding(gaw);
	eptp.bits.etmt = mon_ept_hw_get_ept_memory_type();
	eptp.bits.reserved = 0;

	return eptp.uint64;
}

/* NOTE: This function is expected to be always called with the lock acquired */
boolean_t mon_ept_enable(guest_cpu_handle_t gcpu)
{
	uint64_t ept_root_table_hpa = 0;
	uint32_t gaw = 0;

	MON_ASSERT(gcpu);

	ept_get_current_ept(gcpu, &ept_root_table_hpa, &gaw);
	if (!ept_set_eptp(gcpu, ept_root_table_hpa, gaw)) {
		EPT_PRINTERROR("EPT: failed to set eptp\r\n");
		goto failure;
	}

	if (!ept_hw_enable_ept(gcpu)) {
		EPT_PRINTERROR("EPT: failed to enable ept\r\n");
		goto failure;
	}

	return TRUE;

failure:
	return FALSE;
}

/* NOTE: This function is expected to be always called with the lock acquired */
void mon_ept_disable(guest_cpu_handle_t gcpu)
{
	ept_hw_disable_ept(gcpu);
}

uint64_t ept_get_eptp(guest_cpu_handle_t gcpu)
{
	MON_ASSERT(gcpu);
	return ept_hw_get_eptp(gcpu);
}

boolean_t ept_set_eptp(guest_cpu_handle_t gcpu, uint64_t ept_root_table_hpa,
		       uint32_t gaw)
{
	MON_ASSERT(gcpu);
	return ept_hw_set_eptp(gcpu, ept_root_table_hpa, gaw);
}

void ept_set_remote_eptp(cpu_id_t from, void *arg)
{
	ept_set_eptp_cmd_t *set_eptp_cmd = arg;
	guest_cpu_handle_t gcpu;

	gcpu = mon_scheduler_get_current_gcpu_for_guest(
		set_eptp_cmd->guest_id);

	if (gcpu == NULL || !mon_ept_is_ept_enabled(gcpu)) {
		return;
	}

	ept_set_eptp(gcpu, set_eptp_cmd->ept_root_table_hpa, set_eptp_cmd->gaw);

	mon_ept_invalidate_ept(ANY_CPU_ID, set_eptp_cmd->invept_cmd);
}

ept_guest_state_t *ept_find_guest_state(guest_id_t guest_id)
{
	ept_guest_state_t *ept_guest_state = NULL;
	list_element_t *iter = NULL;
	boolean_t found = FALSE;

	LIST_FOR_EACH(ept.guest_state, iter) {
		ept_guest_state = LIST_ENTRY(iter, ept_guest_state_t, list);
		if (ept_guest_state->guest_id == guest_id) {
			found = TRUE;
			break;
		}
	}
	if (found) {
		return ept_guest_state;
	}
	return NULL;
}

static
boolean_t ept_guest_initialize(guest_handle_t guest)
{
	uint32_t i;
	ept_guest_state_t *ept_guest = NULL;

	ept_guest = (ept_guest_state_t *)mon_malloc(sizeof(ept_guest_state_t));
	MON_ASSERT(ept_guest);

	ept_guest->guest_id = guest_get_id(guest);
	list_add(ept.guest_state, ept_guest->list);

	ept_guest->gcpu_state =
		(ept_guest_cpu_state_t **)mon_malloc(ept.num_of_cpus *
			sizeof(ept_guest_cpu_state_t *));
	MON_ASSERT(ept_guest->gcpu_state);

	for (i = 0; i < ept.num_of_cpus; i++) {
		ept_guest->gcpu_state[i] =
			(ept_guest_cpu_state_t *)mon_malloc(sizeof(
					ept_guest_cpu_state_t));
		MON_ASSERT(ept_guest->gcpu_state[i]);
	}

	event_global_register(EVENT_BEGIN_GPM_MODIFICATION_BEFORE_CPUS_STOPPED,
		ept_begin_gpm_modification_before_cpus_stop);
	event_global_register(EVENT_END_GPM_MODIFICATION_BEFORE_CPUS_RESUMED,
		ept_end_gpm_modification_before_cpus_resume);
	event_global_register(EVENT_END_GPM_MODIFICATION_AFTER_CPUS_RESUMED,
		ept_end_gpm_modification_after_cpus_resume);

	return TRUE;
}

static
boolean_t ept_guest_cpu_initialize(guest_cpu_handle_t gcpu)
{
	const virtual_cpu_id_t *vcpu_id = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;
	ept_guest_state_t *ept_guest_state = NULL;

	EPT_LOG("EPT: CPU#%d ept_guest_cpu_initialize\r\n", hw_cpu_id());

	vcpu_id = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu_id);

	ept_guest_state = ept_find_guest_state(vcpu_id->guest_id);
	MON_ASSERT(ept_guest_state);
	ept_guest_cpu = ept_guest_state->gcpu_state[vcpu_id->guest_cpu_id];

	/*
	 * During S3 resume, these values need to be updated
	 */
	ept_guest_cpu->cr0 =
		gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR0);
	ept_guest_cpu->cr4 =
		gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR4);

	if (!ept_guest_cpu->is_initialized) {
		ept_register_events(gcpu);
		ept_guest_cpu->is_initialized = TRUE;
	}

	return TRUE;
}

static
void ept_fill_vmexit_request(vmexit_control_t *vmexit_request)
{
	mon_zeromem(vmexit_request, sizeof(vmexit_control_t));
	if (!mon_is_unrestricted_guest_supported()) {
		vmexit_request->cr0.bit_request = CR0_PG;
		vmexit_request->cr0.bit_mask = CR0_PG;

		vmexit_request->cr4.bit_request = CR4_PAE;
		vmexit_request->cr4.bit_mask = CR4_PAE;
	}
}

static
boolean_t ept_add_gcpu(guest_cpu_handle_t gcpu, void *pv UNUSED)
{
	event_gcpu_activity_state_change_data_t activity_state;
	vmexit_control_t vmexit_request;

	mon_zeromem(&activity_state, sizeof(activity_state));
	mon_zeromem(&vmexit_request, sizeof(vmexit_request));

	event_gcpu_register(EVENT_GCPU_ACTIVITY_STATE_CHANGE, gcpu,
		(event_callback_t)ept_gcpu_activity_state_change);

	activity_state.new_state = gcpu_get_activity_state(gcpu);
	if (ept_is_gcpu_active(activity_state.new_state)) {
		/* if gcpu already active, fire manually */
		ept_gcpu_activity_state_change(gcpu, &activity_state);
	}

	/* setup control only if gcpu is added on this host CPU */
	if (hw_cpu_id() == scheduler_get_host_cpu_id(gcpu)) {
		ept_fill_vmexit_request(&vmexit_request);
		gcpu_control_setup(gcpu, &vmexit_request);
	}
	return TRUE;
}

static
void ept_add_static_guest(guest_handle_t guest)
{
	guest_cpu_handle_t gcpu;
	guest_gcpu_econtext_t gcpu_context;
	vmexit_control_t vmexit_request;
	uint64_t ept_root_table_hpa = 0;
	uint32_t ept_gaw = 0;

	EPT_LOG("ept CPU#%d: activate ept\r\n", hw_cpu_id());

	ept_fill_vmexit_request(&vmexit_request);

	/* request needed vmexits */
	guest_control_setup(guest, &vmexit_request);

	ept_guest_initialize(guest);

	/* Initialize default EPT */
	ept_create_default_ept(guest, mon_guest_get_startup_gpm(guest));
	/* Get default EPT */
	ept_get_default_ept(guest, &ept_root_table_hpa, &ept_gaw);

	for (gcpu = mon_guest_gcpu_first(guest, &gcpu_context); gcpu;
	     gcpu = mon_guest_gcpu_next(&gcpu_context)) {
		ept_add_gcpu(gcpu, NULL);
		/* Set EPT pointer (of each GCPU) to default EPT */
		mon_ept_set_current_ept(gcpu, ept_root_table_hpa, ept_gaw);
	}
}

static
boolean_t ept_add_dynamic_guest(guest_cpu_handle_t gcpu UNUSED, void *pv)
{
	event_guest_create_data_t *guest_create_event_data =
		(event_guest_create_data_t *)pv;
	guest_handle_t guest = mon_guest_handle(
		guest_create_event_data->guest_id);
	mon_paging_policy_t pg_policy;
	pol_retval_t policy_status;

	policy_status = get_paging_policy(guest_policy(guest), &pg_policy);
	MON_ASSERT(POL_RETVAL_SUCCESS == policy_status);

	if (POL_PG_EPT == pg_policy) {
		ept_guest_initialize(mon_guest_handle(guest_create_event_data->
				guest_id));
	}

	return TRUE;
}

void init_ept_addon(uint32_t num_of_cpus)
{
	guest_handle_t guest;
	guest_econtext_t guest_ctx;

	if (!global_policy_uses_ept()) {
		return;
	}

	mon_zeromem(&ept, sizeof(ept));
	ept.num_of_cpus = num_of_cpus;

	EPT_LOG("init_ept_addon: Initialize EPT num_cpus %d\n", num_of_cpus);

	list_init(ept.guest_state);
	lock_initialize(&ept.lock);

	event_global_register(EVENT_GUEST_CREATE, ept_add_dynamic_guest);
	event_global_register(EVENT_GCPU_ADD, (event_callback_t)ept_add_gcpu);

	for (guest = guest_first(&guest_ctx); guest;
	     guest = guest_next(&guest_ctx))
		ept_add_static_guest(guest);
}

boolean_t ept_page_walk(uint64_t first_table, uint64_t addr, uint32_t gaw)
{
	uint64_t *table = (uint64_t *)first_table;
	uint64_t *entry = NULL;

	EPT_LOG("EPT page walk addr %p\r\n", addr);
	if (gaw > 39) {
		entry = &table[(addr & 0xff8000000000) >> 39];
		EPT_LOG("Level 4: table %p entry %p\r\n", table, *entry);
		table = (uint64_t *)((*entry) & ~0xfff);
		if (((*entry) & 0x1) == 0) {
			EPT_LOG("Entry not present\r\n");
			return FALSE;
		}
	}
	entry = &table[(addr & 0x7fc0000000) >> 30];
	EPT_LOG("Level 3: table %p entry %p\r\n", table, *entry);
	if (((*entry) & 0x1) == 0) {
		EPT_LOG("Entry not present\r\n");
		return FALSE;
	}
	table = (uint64_t *)((*entry) & ~0xfff);
	entry = &table[(addr & 0x3fe00000) >> 21];
	EPT_LOG("Level 2: table %p entry %p\r\n", table, *entry);
	table = (uint64_t *)((*entry) & ~0xfff);
	if (((*entry) & 0x1) == 0) {
		EPT_LOG("Entry not present\r\n");
		return FALSE;
	}
	entry = &table[(addr & 0x1ff000) >> 12];
	EPT_LOG("Level 1: table %p entry %p\r\n", table, *entry);
	return TRUE;
}

#ifdef DEBUG
void mon_ept_print(IN guest_handle_t guest, IN mam_handle_t address_space)
{
	mam_memory_ranges_iterator_t iter;
	mam_mapping_result_t res;

	iter = mam_get_memory_ranges_iterator(address_space);

	while (iter != MAM_INVALID_MEMORY_RANGES_ITERATOR) {
		gpa_t curr_gpa;
		uint64_t curr_size;
		hpa_t curr_hpa;
		mam_attributes_t attrs;
		iter = mam_get_range_details_from_iterator(address_space,
			iter,
			(uint64_t *)&curr_gpa,
			&curr_size);
		MON_ASSERT(curr_size != 0);

		res =
			mam_get_mapping(address_space,
				curr_gpa,
				&curr_hpa,
				&attrs);
		if (res == MAM_MAPPING_SUCCESSFUL) {
			EPT_LOG("EPT guest#%d: gpa_t %p -> hpa_t %p\r\n",
				curr_gpa,
				curr_hpa);
		}
	}
}
#endif
