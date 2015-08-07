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
#include "mon_defs.h"
#include "guest_cpu.h"
#include "vmcs_api.h"
#include "mon_dbg.h"
#include "em64t_defs.h"
#include "guest_cpu_vmenter_event.h"
#include "policy_manager.h"
#include "mon_events_data.h"
#include "vmcs_hierarchy.h"
#include "page_walker.h"
#include "ept.h"
#include "unrestricted_guest.h"
#include "mon_callback.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_CR_ACCESS_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMEXIT_CR_ACCESS_C, __condition)

#define CR0_TASK_SWITCH     8

#define GCPU_SET_GUEST_VISIBLE_CONTROL_TO_L0_M(__gcpu, __reg_id, __value) {   \
		if (IA32_CTRL_CR0 == (__reg_id) || IA32_CTRL_CR4 == \
		    (__reg_id)) {          \
			gcpu_set_guest_visible_control_reg_layered(__gcpu, \
				__reg_id, \
				__value, \
				VMCS_LEVEL_0); }             \
		gcpu_set_guest_visible_control_reg_layered(__gcpu, \
			__reg_id, \
			__value, \
			VMCS_MERGED);              \
}

extern boolean_t is_cr4_osxsave_supported(void);

static
mon_event_t lkup_write_event[IA32_CTRL_COUNT] = {
	EVENT_GCPU_AFTER_GUEST_CR0_WRITE,       /* IA32_CTRL_CR0, */
	EVENTS_COUNT,                           /* IA32_CTRL_CR2, */
	EVENT_GCPU_AFTER_GUEST_CR3_WRITE,       /* IA32_CTRL_CR3, */
	EVENT_GCPU_AFTER_GUEST_CR4_WRITE,       /* IA32_CTRL_CR4, */
	EVENTS_COUNT,                           /* IA32_CTRL_CR8, */
};

#define IA32_REG_COUNT 0x10

static
mon_ia32_gp_registers_t lkup_operand[IA32_REG_COUNT] = {
	IA32_REG_RAX,
	IA32_REG_RCX,
	IA32_REG_RDX,
	IA32_REG_RBX,
	IA32_REG_RSP,
	IA32_REG_RBP,
	IA32_REG_RSI,
	IA32_REG_RDI,
	IA32_REG_R8,
	IA32_REG_R9,
	IA32_REG_R10,
	IA32_REG_R11,
	IA32_REG_R12,
	IA32_REG_R13,
	IA32_REG_R14,
	IA32_REG_R15
};

#define IA32_CR_COUNT   0x9

static
mon_ia32_control_registers_t lkup_cr[IA32_CR_COUNT] = {
	IA32_CTRL_CR0,
	UNSUPPORTED_CR,
	UNSUPPORTED_CR,
	IA32_CTRL_CR3,
	IA32_CTRL_CR4,
	UNSUPPORTED_CR,
	UNSUPPORTED_CR,
	UNSUPPORTED_CR,
	IA32_CTRL_CR8
};


/* Method to check if SMEP is supported or not on this processor. Returns 0 if
 * SMEP is not supported.  1 if SMEP is supported.
 * CPUID.(EAX=07H, ECX=0H):EBX.SMAP[bit 7] = ?
 */
boolean_t is_cr4_smep_supported(void)
{
	cpuid_params_t cpuid_params;

	/* Invoke CPUID with RAX = 7 */
	cpuid_params.m_rax = CPUID_LEAF_7H;
	/* Set sub-leaf RCX to 0 */
	cpuid_params.m_rcx = CPUID_SUB_LEAF_0H;

	/* Execute CPUID */
	hw_cpuid(&cpuid_params);

	/* Return whether SMEP is supported or not */
	return (boolean_t)BIT_GET64(cpuid_params.m_rbx,
		CPUID_LEAF_7H_0H_EBX_SMEP_BIT);
}



/* Method to check if SMAP is supported or not on this processor.
 * Returns 0 if SMAP is not supported, 1 if SMAP is supported.
 * CPUID.(EAX=07H, ECX=0H):EBX.SMAP[bit 20] = ?.
 */
boolean_t is_cr4_smap_supported(void)
{
	cpuid_params_t cpuid_params;

	/* Invoke CPUID with leaf RAX = 7 */
	cpuid_params.m_rax = CPUID_LEAF_7H;
	/* Set sub-leaf RCX to 0 */
	cpuid_params.m_rcx = CPUID_SUB_LEAF_0H;

	/* Execute CPUID */
	hw_cpuid(&cpuid_params);

	/* Return whether SMAP is supported or not */
	return (boolean_t)BIT_GET64(cpuid_params.m_rbx,
		CPUID_LEAF_7H_0H_EBX_SMAP_BIT);
}


static
boolean_t vmexit_cr_access_is_gpf0(guest_cpu_handle_t gcpu)
{
	em64t_cr0_t cr0;
	uint64_t cr3;
	em64t_cr4_t cr4;
	ia32_efer_t efer;

	MON_ASSERT(gcpu != NULL);
	cr0.uint64 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR0);
	if ((cr0.bits.pg &&
	     (!cr0.bits.pe)) || (cr0.bits.nw && (!cr0.bits.cd))) {
		return TRUE;
	}

	cr4.uint64 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR4);
	if (cr4.bits.reserved_0 || cr4.bits.reserved_1 ||
	    cr4.bits.reserved_2 || cr4.bits.reserved_3 ||
	    cr4.bits.vmxe || cr4.bits.smxe) {
		return TRUE;
	}

	if (cr4.bits.osxsave && !is_cr4_osxsave_supported()) {
		return TRUE;
	}

	if (cr4.bits.smep && !is_cr4_smep_supported()) {
		return TRUE;
	}

	/* if SMAP is not supported, treat cr4.bits.SMAP as reserved bit */
	if (cr4.bits.smap && !is_cr4_smap_supported()) {
		return TRUE;
	}

	if (cr4.bits.fsgsbase && !is_fsgsbase_supported()) {
		return TRUE;
	}

	efer.uint64 = gcpu_get_msr_reg(gcpu, IA32_MON_MSR_EFER);
	if (efer.bits.lme && (!cr4.bits.pae)) {
		return TRUE;
	}

	/* #GP conditions due to PCIDE feature.  */
	if (cr4.bits.pcide) {
		/* If this bit is not supported by h/w . */
		if (!is_pcid_supported()) {
			return TRUE;
		}

		/* PCIDE bit Can be set only in IA-32e mode (if IA32_EFER.LMA = 1). */
		if (!efer.bits.lma) {
			return TRUE;
		}

		cr3 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR3);

		/* software can change CR4.PCIDE from 0 to 1 only if CR3[11:0] = 000H */
		if (cr3 & 0x0FFF) {
			return TRUE;
		}

		/* MOVtoCR0 causes a #GP if it would clear CR0.PG to 0 while
		 * CR4.PCIDE=1. */
		if (!cr0.bits.pg) {
			return TRUE;
		}
	}

	if (cr0.bits.pg && cr4.bits.pae && (!efer.bits.lme)) {
		uint8_t pdpt[PW_NUM_OF_PDPT_ENTRIES_IN_32_BIT_MODE *
			     PW_SIZE_OF_PAE_ENTRY];

		gcpu_get_32_bit_pdpt(gcpu, pdpt);
		if (!pw_is_pdpt_in_32_bit_pae_mode_valid(gcpu, pdpt)) {
			return TRUE;
		}
	}

	return FALSE;
}

static
boolean_t cr_guest_update(guest_cpu_handle_t gcpu,
			  mon_ia32_control_registers_t reg_id,
			  address_t bits_to_update,
			  ia32_vmx_exit_qualification_t qualification);

static
boolean_t cr_mov(guest_cpu_handle_t gcpu,
		 ia32_vmx_exit_qualification_t qualification);

raise_event_retval_t cr_raise_write_events(guest_cpu_handle_t gcpu,
					   mon_ia32_control_registers_t reg_id,
					   address_t new_value)
{
	event_gcpu_guest_cr_write_data_t event_data = { 0 };
	mon_event_t event;
	raise_event_retval_t result = EVENT_NO_HANDLERS_REGISTERED;

	if (reg_id >= IA32_CTRL_COUNT) {
		return result;
	}

	event = lkup_write_event[reg_id];
	if (event != EVENTS_COUNT) {
		event_data.new_guest_visible_value = new_value;
		if (TRUE == event_raise(event, gcpu, &event_data)) {
			result = EVENT_HANDLED;
		} else {
			result = EVENT_NOT_HANDLED;
		}
	}

	return result;
}

boolean_t cr_guest_update(guest_cpu_handle_t gcpu,
			  mon_ia32_control_registers_t reg_id,
			  address_t bits_to_update,
			  ia32_vmx_exit_qualification_t qualification)
{
	uint64_t guest_cr;
	uint64_t old_visible_reg_value;
	uint64_t visible_guest_cr;
	raise_event_retval_t cr_update_event;
	address_t value;
	report_cr_dr_load_access_data_t cr_access_data;

	if (qualification.cr_access.access_type == 3) {
		value = qualification.cr_access.lmsw_data;
	} else {
		value = 0;
	}

	cr_access_data.qualification = qualification.uint64;
	if (report_mon_event
		    (MON_EVENT_CR_ACCESS, (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    (void *)&cr_access_data)) {
		return FALSE;
	}

	old_visible_reg_value =
		gcpu_get_guest_visible_control_reg_layered(gcpu,
			reg_id,
			VMCS_MERGED);
	visible_guest_cr = old_visible_reg_value;
	BITMAP_ASSIGN64(visible_guest_cr, bits_to_update, value);

	/* update guest visible CR-X */
	GCPU_SET_GUEST_VISIBLE_CONTROL_TO_L0_M(gcpu, reg_id, visible_guest_cr);

	if (vmexit_cr_access_is_gpf0(gcpu)) {
		/* gcpu_set_guest_visible_control_reg_layered(gcpu, reg_id,
		 * old_visible_reg_value, VMCS_MERGED); */
		GCPU_SET_GUEST_VISIBLE_CONTROL_TO_L0_M(gcpu, reg_id,
			old_visible_reg_value);

		/* CR* access vmexit is changed to GPF0 exception. */
		MON_LOG(mask_anonymous,
			level_trace,
			"%s: CR* access caused GPF0\n",
			__FUNCTION__);

		MON_DEBUG_CODE(MON_DEADLOOP());
		mon_gcpu_inject_gp0(gcpu);
		return FALSE;
	}

	/* update guest CR-X */
	guest_cr = gcpu_get_control_reg_layered(gcpu, reg_id, VMCS_MERGED);
	BITMAP_ASSIGN64(guest_cr, bits_to_update, value);
	gcpu_set_control_reg_layered(gcpu, reg_id, guest_cr, VMCS_MERGED);

	cr_update_event = cr_raise_write_events(gcpu, reg_id, visible_guest_cr);
	MON_ASSERT(cr_update_event != EVENT_NOT_HANDLED);

	return TRUE;
}

boolean_t cr_guest_write(guest_cpu_handle_t gcpu,
			 mon_ia32_control_registers_t reg_id, address_t value)
{
	raise_event_retval_t cr_update_event;
	uint64_t old_visible_reg_value;

	const virtual_cpu_id_t *vcpu_id = NULL;
	ept_guest_state_t *ept_guest = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;

	old_visible_reg_value =
		gcpu_get_guest_visible_control_reg_layered(gcpu,
			reg_id,
			VMCS_MERGED);
	GCPU_SET_GUEST_VISIBLE_CONTROL_TO_L0_M(gcpu, reg_id, value);

	if (vmexit_cr_access_is_gpf0(gcpu)) {
		/* gcpu_set_guest_visible_control_reg_layered(gcpu, reg_id,
		 * old_visible_reg_value, VMCS_MERGED); */
		GCPU_SET_GUEST_VISIBLE_CONTROL_TO_L0_M(gcpu, reg_id,
			old_visible_reg_value);

		/* CR* access vmexit is changed to GPF0 exception. */
		MON_LOG(mask_anonymous,
			level_trace,
			"%s: CR* access caused GPF0\n",
			__FUNCTION__);
		MON_DEBUG_CODE(MON_DEADLOOP());
		mon_gcpu_inject_gp0(gcpu);
		return FALSE;
	}

	if (mon_is_unrestricted_guest_supported()) {
		vcpu_id = mon_guest_vcpu(gcpu);
		MON_ASSERT(vcpu_id);
		ept_guest = ept_find_guest_state(vcpu_id->guest_id);
		MON_ASSERT(ept_guest);
		ept_guest_cpu = ept_guest->gcpu_state[vcpu_id->guest_cpu_id];

		ept_guest_cpu->cr0 =
			gcpu_get_control_reg_layered(gcpu,
				IA32_CTRL_CR0,
				VMCS_MERGED);
		ept_guest_cpu->cr4 =
			gcpu_get_control_reg_layered(gcpu,
				IA32_CTRL_CR4,
				VMCS_MERGED);
	}

	gcpu_set_control_reg_layered(gcpu, reg_id, value, VMCS_MERGED);

	cr_update_event = cr_raise_write_events(gcpu, reg_id, value);
	MON_ASSERT(cr_update_event != EVENT_NOT_HANDLED);

	if ((reg_id == IA32_CTRL_CR4) && is_cr4_osxsave_supported()) {
		em64t_cr4_t cr4_mask;

		cr4_mask.uint64 = 0;
		cr4_mask.bits.osxsave = 1;
		mon_vmcs_write(mon_gcpu_get_vmcs(gcpu), VMCS_HOST_CR4,
			(mon_vmcs_read(mon_gcpu_get_vmcs(gcpu), VMCS_HOST_CR4)
			 & ~cr4_mask.uint64) | (value & cr4_mask.uint64));
	}

	return TRUE;
}

boolean_t cr_mov(guest_cpu_handle_t gcpu,
		 ia32_vmx_exit_qualification_t qualification)
{
	mon_ia32_control_registers_t cr_id;
	mon_ia32_gp_registers_t operand;
	address_t cr_value;
	boolean_t status = TRUE;
	report_cr_dr_load_access_data_t cr_access_data;

	cr_access_data.qualification = qualification.uint64;

	if (report_mon_event
		    (MON_EVENT_CR_ACCESS, (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    (void *)&cr_access_data)) {
		return FALSE;
	}

	MON_ASSERT(qualification.cr_access.number < NELEMENTS(lkup_cr));
	cr_id = lkup_cr[qualification.cr_access.number];
	MON_ASSERT(UNSUPPORTED_CR != cr_id);

	MON_ASSERT(qualification.cr_access.move_gpr < NELEMENTS(lkup_operand));
	operand = lkup_operand[qualification.cr_access.move_gpr];

	switch (qualification.cr_access.access_type) {
	case 0:
		/* move to CR */
		cr_value = gcpu_get_gp_reg(gcpu, operand);
		status = cr_guest_write(gcpu, cr_id, cr_value);
		break;

	case 1:
		/* move from CR */
		cr_value = gcpu_get_guest_visible_control_reg(gcpu, cr_id);
		gcpu_set_gp_reg(gcpu, operand, cr_value);
		break;

	default:
		MON_DEADLOOP();
		break;
	}

	return status;
}

vmexit_handling_status_t vmexit_cr_access(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ia32_vmx_exit_qualification_t qualification;
	boolean_t status = TRUE;

	qualification.uint64 =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);

	switch (qualification.cr_access.access_type) {
	case 0:
	/* move to CR */
	case 1:
		/* move from CR */
		status = cr_mov(gcpu, qualification);
		break;

	case 2:
		/* CLTS */
		MON_ASSERT(0 == qualification.cr_access.number);
		status =
			cr_guest_update(gcpu, IA32_CTRL_CR0, CR0_TASK_SWITCH,
				qualification);
		break;

	case 3:
		/* LMSW */
		MON_ASSERT(0 == qualification.cr_access.number);
		status = cr_guest_update(gcpu,
			IA32_CTRL_CR0,
			0xFFFF,
			qualification);
		break;
	}

	if (TRUE == status) {
		gcpu_skip_guest_instruction(gcpu);
	}

	return VMEXIT_HANDLED;
}

mon_ia32_control_registers_t
vmexit_cr_access_get_cr_from_qualification(uint64_t qualification)
{
	ia32_vmx_exit_qualification_t qualification_tmp;

	qualification_tmp.uint64 = qualification;

	if (qualification_tmp.cr_access.number >= IA32_CR_COUNT) {
		return UNSUPPORTED_CR;
	}

	return lkup_cr[qualification_tmp.cr_access.number];
}

mon_ia32_gp_registers_t
vmexit_cr_access_get_operand_from_qualification(uint64_t qualification)
{
	ia32_vmx_exit_qualification_t qualification_tmp;

	qualification_tmp.uint64 = qualification;
	MON_ASSERT(qualification_tmp.cr_access.move_gpr < IA32_REG_COUNT);

	return lkup_operand[qualification_tmp.cr_access.move_gpr];
}
