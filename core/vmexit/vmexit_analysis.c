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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_ANALYSIS_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMEXIT_ANALYSIS_C, __condition)
#include <mon_defs.h>
#include <mon_dbg.h>
#include <vmcs_api.h>
#include <vmx_vmcs.h>
#include <vmx_ctrl_msrs.h>
#include <mon_objects.h>
#include <isr.h>
#include <mon_arch_defs.h>
#include <vmexit_cr_access.h>
#include <guest_cpu.h>
#include <guest.h>
#include <gpm_api.h>
#include <host_memory_manager_api.h>
#include <vmexit_analysis.h>
#include "mon_callback.h"


typedef boolean_t (*func_vmexit_is_control_requested_t) (guest_cpu_handle_t,
							 vmcs_object_t *,
							 vmcs_object_t *);

static
boolean_t vmexit_analysis_true_func(guest_cpu_handle_t gcpu UNUSED,
				    vmcs_object_t *vmexit_vmcs UNUSED,
				    vmcs_object_t *control_vmcs UNUSED)
{
	return TRUE;
}

static
boolean_t vmexit_analysis_false_func(guest_cpu_handle_t gcpu UNUSED,
				     vmcs_object_t *vmexit_vmcs UNUSED,
				     vmcs_object_t *control_vmcs UNUSED)
{
	return FALSE;
}

static
boolean_t vmexit_analysis_interrupt_window_exiting(
	guest_cpu_handle_t gcpu UNUSED,
	vmcs_object_t *
	vmexit_vmcs UNUSED,
	vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t level1_ctrls;

	level1_ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	return level1_ctrls.bits.virtual_interrupt == 1;
}

static
boolean_t vmexit_analysis_nmi_window_exiting(guest_cpu_handle_t gcpu UNUSED,
					     vmcs_object_t *vmexit_vmcs UNUSED,
					     vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t level1_ctrls;

	level1_ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	return level1_ctrls.bits.nmi_window == 1;
}

static
boolean_t vmexit_analysis_hlt_inst_exiting(guest_cpu_handle_t gcpu UNUSED,
					   vmcs_object_t *vmexit_vmcs UNUSED,
					   vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t level1_ctrls;

	level1_ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	return level1_ctrls.bits.hlt == 1;
}

static
boolean_t vmexit_analysis_invlpg_inst_exiting(guest_cpu_handle_t gcpu UNUSED,
					      vmcs_object_t *vmexit_vmcs UNUSED,
					      vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t level1_ctrls;

	level1_ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	return level1_ctrls.bits.invlpg == 1;
}

static
boolean_t vmexit_analysis_rdpmc_inst_exiting(guest_cpu_handle_t gcpu UNUSED,
					     vmcs_object_t *vmexit_vmcs UNUSED,
					     vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t level1_ctrls;

	level1_ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	return level1_ctrls.bits.rdpmc == 1;
}

static
boolean_t vmexit_analysis_rdtsc_inst_exiting(guest_cpu_handle_t gcpu UNUSED,
					     vmcs_object_t *vmexit_vmcs UNUSED,
					     vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t level1_ctrls;

	level1_ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	return level1_ctrls.bits.rdtsc == 1;
}

static
boolean_t vmexit_analysis_dr_access_exiting(guest_cpu_handle_t gcpu UNUSED,
					    vmcs_object_t *vmexit_vmcs UNUSED,
					    vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t level1_ctrls;

	level1_ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	return level1_ctrls.bits.mov_dr == 1;
}

static
boolean_t vmexit_analysis_mwait_inst_exiting(guest_cpu_handle_t gcpu UNUSED,
					     vmcs_object_t *vmexit_vmcs UNUSED,
					     vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t level1_ctrls;

	level1_ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	return level1_ctrls.bits.mwait == 1;
}

static
boolean_t vmexit_analysis_monitor_inst_exiting(guest_cpu_handle_t gcpu UNUSED,
					       vmcs_object_t *vmexit_vmcs UNUSED,
					       vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t level1_ctrls;

	level1_ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	return level1_ctrls.bits.monitor == 1;
}

static
boolean_t vmexit_analysis_pause_inst_exiting(guest_cpu_handle_t gcpu UNUSED,
					     vmcs_object_t *vmexit_vmcs UNUSED,
					     vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t level1_ctrls;

	level1_ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	return level1_ctrls.bits.pause == 1;
}

static
boolean_t vmexit_analysis_softinterrupt_exception_nmi_exiting(guest_cpu_handle_t
							      gcpu UNUSED,
							      vmcs_object_t *
							      vmexit_vmcs,
							      vmcs_object_t *
							      control_vmcs)
{
	ia32_vmx_vmcs_vmexit_info_interrupt_info_t interrupt_info;
	uint32_t vector;

	interrupt_info.uint32 = (uint32_t)mon_vmcs_read(vmexit_vmcs,
		VMCS_EXIT_INFO_EXCEPTION_INFO);
	vector = (uint32_t)interrupt_info.bits.vector;

	if (vector == IA32_EXCEPTION_VECTOR_PAGE_FAULT) {
		ia32_vmcs_exception_bitmap_t level1_exceptions;
		uint32_t pfec = (uint32_t)mon_vmcs_read(vmexit_vmcs,
			VMCS_EXIT_INFO_EXCEPTION_ERROR_CODE);
		uint32_t level1_pfec_mask = (uint32_t)mon_vmcs_read(vmexit_vmcs,
			VMCS_PAGE_FAULT_ERROR_CODE_MASK);
		uint32_t level1_pfec_match = (uint32_t)mon_vmcs_read(
			vmexit_vmcs,
			VMCS_PAGE_FAULT_ERROR_CODE_MATCH);

		MON_ASSERT(interrupt_info.bits.interrupt_type !=
			VMEXIT_INTERRUPT_TYPE_EXTERNAL_INTERRUPT);
		MON_ASSERT(
			interrupt_info.bits.interrupt_type !=
			VMEXIT_INTERRUPT_TYPE_NMI);

		level1_exceptions.uint32 =
			(uint32_t)mon_vmcs_read(control_vmcs,
				VMCS_EXCEPTION_BITMAP);
		if (level1_exceptions.bits.pf == 1) {
			return (pfec & level1_pfec_mask) == level1_pfec_match;
		} else {
			return (pfec & level1_pfec_mask) != level1_pfec_match;
		}
	} else if (interrupt_info.bits.interrupt_type ==
		   VMEXIT_INTERRUPT_TYPE_NMI) {
		pin_based_vm_execution_controls_t level1_pin_ctrls;

		MON_ASSERT(vector == IA32_EXCEPTION_VECTOR_NMI);
		level1_pin_ctrls.uint32 = (uint32_t)mon_vmcs_read(vmexit_vmcs,
			VMCS_CONTROL_VECTOR_PIN_EVENTS);
		return level1_pin_ctrls.bits.nmi == 1;
	} else {
		uint32_t level1_exceptions = (uint32_t)mon_vmcs_read(
			control_vmcs,
			VMCS_EXCEPTION_BITMAP);

		return (level1_exceptions & (1 << vector)) != 0;
	}
}

static
boolean_t vmexit_analysis_hardware_interrupt_exiting(
	guest_cpu_handle_t gcpu UNUSED,
	vmcs_object_t *vmexit_vmcs,
	vmcs_object_t *
	control_vmcs UNUSED)
{
	pin_based_vm_execution_controls_t level1_pin_ctrls;

	level1_pin_ctrls.uint32 = (uint32_t)mon_vmcs_read(vmexit_vmcs,
		VMCS_CONTROL_VECTOR_PIN_EVENTS);
	return level1_pin_ctrls.bits.external_interrupt == 1;
}

static
boolean_t vmexit_analysis_is_cr3_in_target_list(vmcs_object_t *vmcs,
						uint64_t cr3_value)
{
	uint32_t cr3_target_count = (uint32_t)mon_vmcs_read(vmcs,
		VMCS_CR3_TARGET_COUNT);
	uint32_t i;

	MON_ASSERT(cr3_target_count <= 4);
	for (i = 0; i < cr3_target_count; i++) {
		uint64_t value = mon_vmcs_read(vmcs,
			(vmcs_field_t)(VMCS_CR3_TARGET_VALUE_0 + i));

		if (value == cr3_value) {
			return TRUE;
		}
	}

	return FALSE;
}

static
boolean_t vmexit_analysis_is_exit_on_cr_update(vmcs_object_t *vmcs,
					       uint64_t new_value,
					       vmcs_field_t shadow_field,
					       vmcs_field_t mask_field)
{
	uint64_t shadow = mon_vmcs_read(vmcs, shadow_field);
	uint64_t mask = mon_vmcs_read(vmcs, mask_field);
	boolean_t result;

	result = ((shadow & mask) != (new_value & mask));

	return result;
}

static
boolean_t vmexit_analysis_cr_access_exiting(guest_cpu_handle_t gcpu,
					    vmcs_object_t *vmexit_vmcs,
					    vmcs_object_t *control_vmcs)
{
	ia32_vmx_exit_qualification_t qualification;

	qualification.uint64 = mon_vmcs_read(vmexit_vmcs,
		VMCS_EXIT_INFO_QUALIFICATION);

	switch (qualification.cr_access.access_type) {
	case 0:                /* move to CR */
	{
		mon_ia32_control_registers_t cr_id =
			vmexit_cr_access_get_cr_from_qualification
				(qualification.uint64);
		mon_ia32_gp_registers_t operand =
			vmexit_cr_access_get_operand_from_qualification
				(qualification.uint64);
		uint64_t new_value = gcpu_get_gp_reg(gcpu, operand);

		if (cr_id == IA32_CTRL_CR3) {
			/* return TRUE in case the value is not in target list */
			return vmexit_analysis_is_cr3_in_target_list
				       (control_vmcs, new_value) == FALSE;
		} else if (cr_id == IA32_CTRL_CR0) {
			return vmexit_analysis_is_exit_on_cr_update(
				control_vmcs,
				new_value,
				VMCS_CR0_READ_SHADOW,
				VMCS_CR0_MASK);
		} else if (cr_id == IA32_CTRL_CR4) {
			return vmexit_analysis_is_exit_on_cr_update(
				control_vmcs,
				new_value,
				VMCS_CR4_READ_SHADOW,
				VMCS_CR4_MASK);
		} else {
			processor_based_vm_execution_controls_t ctrls;

			MON_ASSERT(cr_id == IA32_CTRL_CR8);
			ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
				VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);

			if (ctrls.bits.cr8_load) {
				return TRUE;
			}

			if (ctrls.bits.tpr_shadow) {
				/* TODO: currently TPR shadow is not supported */
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Currently TPR shadow is not supported\n",
					__FUNCTION__);
				MON_DEADLOOP();
			}

			return FALSE;
		}
		break;
	}
	case 1:                /* move from CR */
	{
		mon_ia32_control_registers_t cr_id =
			vmexit_cr_access_get_cr_from_qualification
				(qualification.uint64);

		if (cr_id == IA32_CTRL_CR3) {
			return TRUE;
		} else {
			processor_based_vm_execution_controls_t ctrls;

			MON_ASSERT(cr_id == IA32_CTRL_CR8);

			ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
				VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
			if (ctrls.bits.cr8_store) {
				return TRUE;
			}

			if (ctrls.bits.tpr_shadow) {
				/* TODO: currently TPR shadow is not supported */
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Currently TPR shadow is not supported\n");
				MON_DEADLOOP();
			}

			return FALSE;
		}

		break;
	}

	case 2:                /* CLTS */
	{
		em64t_cr0_t cr0_shadow;
		em64t_cr0_t cr0_mask;

		MON_ASSERT(0 == qualification.cr_access.number);

		cr0_shadow.uint64 = mon_vmcs_read(control_vmcs,
			VMCS_CR0_READ_SHADOW);
		cr0_mask.uint64 = mon_vmcs_read(control_vmcs, VMCS_CR0_MASK);

		return (cr0_mask.bits.ts == 1) && (cr0_shadow.bits.ts != 0);
		break;
	}

	case 3:                /* LMSW */
	{
		em64t_cr0_t cr0_shadow;
		em64t_cr0_t cr0_mask;
		uint32_t mask_tmp;

		MON_ASSERT(0 == qualification.cr_access.number);

		cr0_shadow.uint64 = mon_vmcs_read(control_vmcs,
			VMCS_CR0_READ_SHADOW);
		cr0_mask.uint64 = mon_vmcs_read(control_vmcs, VMCS_CR0_MASK);
		mask_tmp = (uint32_t)(cr0_mask.uint64 & 0xffff);
		return (mask_tmp != 0) &&
		       ((cr0_shadow.uint64 & mask_tmp) !=
			(qualification.cr_access.lmsw_data & mask_tmp));
		break;
	}
	}

	/* should not reach here */
	MON_DEADLOOP();
	return FALSE;
}

static
void *vmexit_analysis_retrieve_ptr_to_additional_memory(IN vmcs_object_t *vmcs,
							IN vmcs_field_t field,
							IN boolean_t
							convert_gpa_to_hpa)
{
	uint64_t bitmap_pa = mon_vmcs_read(vmcs, field);
	uint64_t bitmap_hpa;
	uint64_t bitmap_hva;
	mam_attributes_t attrs;

	if (convert_gpa_to_hpa) {
		guest_cpu_handle_t gcpu = vmcs_get_owner(vmcs);
		guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
		gpm_handle_t gpm = gcpu_get_current_gpm(guest);
		if (!mon_gpm_gpa_to_hpa(gpm, bitmap_pa, &bitmap_hpa, &attrs)) {
			MON_DEADLOOP();
		}
	} else {
		bitmap_hpa = bitmap_pa;
	}

	if (!mon_hmm_hpa_to_hva(bitmap_hpa, &bitmap_hva)) {
		MON_DEADLOOP();
	}

	return (void *)bitmap_hva;
}

static
boolean_t vmexit_analysis_is_bit_set_in_bitmap(void *bitmap, uint32_t bit_pos)
{
	uint32_t byte = bit_pos >> 3;
	uint32_t pos_in_byte = bit_pos & 0x7;
	uint8_t *bitmap_tmp = (uint8_t *)bitmap;

	return (bitmap_tmp[byte] & (1 << pos_in_byte)) != 0;
}

static
boolean_t vmexit_analysis_io_exiting(guest_cpu_handle_t gcpu UNUSED,
				     vmcs_object_t *vmexit_vmcs,
				     vmcs_object_t *control_vmcs)
{
	processor_based_vm_execution_controls_t ctrls;
	ia32_vmx_exit_qualification_t qualification;
	uint32_t port;
	uint32_t size = 0;
	vmcs_level_t control_vmcs_level;

	ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);

	if (ctrls.bits.activate_io_bitmaps == 0) {
		return ctrls.bits.unconditional_io == 1;
	}

	qualification.uint64 = mon_vmcs_read(vmexit_vmcs,
		VMCS_EXIT_INFO_QUALIFICATION);
	port = qualification.io_instruction.port_number;
	switch (qualification.io_instruction.size) {
	case 0:
		size = 1;
		break;
	case 1:
		size = 2;
		break;
	case 3:
		size = 4;
		break;
	default:
		MON_DEADLOOP();
	}

	if ((port + size) > 0xffff) {
		/* wrap around */
		return TRUE;
	}

	control_vmcs_level = vmcs_get_level(control_vmcs);
	if (port < 0x7fff) {
		void *bitmap =
			vmexit_analysis_retrieve_ptr_to_additional_memory(
				control_vmcs,
				VMCS_IO_BITMAP_ADDRESS_A,
				(control_vmcs_level == VMCS_LEVEL_1));
		return vmexit_analysis_is_bit_set_in_bitmap(bitmap, port);
	} else {
		void *bitmap =
			vmexit_analysis_retrieve_ptr_to_additional_memory(
				control_vmcs,
				VMCS_IO_BITMAP_ADDRESS_B,
				(control_vmcs_level == VMCS_LEVEL_1));
		uint32_t bit_pos = port & 0x7fff;
		return vmexit_analysis_is_bit_set_in_bitmap(bitmap, bit_pos);
	}
}

static
boolean_t vmexit_analysis_msr_access_exiting(guest_cpu_handle_t gcpu,
					     vmcs_object_t *control_vmcs,
					     boolean_t is_rdmsr)
{
	msr_id_t msr_id;
	hva_t bitmap_hva;
	uint32_t bitmap_pos;
	void *bitmap;
	vmcs_level_t control_vmcs_level;
	processor_based_vm_execution_controls_t ctrls;

	ctrls.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);

	if (ctrls.bits.use_msr_bitmaps == 0) {
		return TRUE;
	}

	msr_id = (msr_id_t)gcpu_get_native_gp_reg(gcpu, IA32_REG_RCX);

	if ((msr_id > 0x1fff) && (msr_id < 0xc0000000)) {
		return TRUE;
	}

	if (msr_id > 0xc0001fff) {
		return TRUE;
	}

	control_vmcs_level = vmcs_get_level(control_vmcs);
	bitmap_hva =
		(hva_t)vmexit_analysis_retrieve_ptr_to_additional_memory(
			control_vmcs,
			VMCS_MSR_BITMAP_ADDRESS,
			(control_vmcs_level == VMCS_LEVEL_1));
	bitmap_pos = msr_id & 0x1fff;

	if (is_rdmsr) {
		if (msr_id <= 0x1fff) {
			bitmap = (void *)bitmap_hva;
		} else {
			MON_ASSERT(msr_id >= 0xc0000000);
			MON_ASSERT(msr_id <= 0xc0001fff);
			bitmap = (void *)(bitmap_hva + (1 KILOBYTE));
		}
	} else {
		if (msr_id <= 0x1fff) {
			bitmap = (void *)(bitmap_hva + (2 KILOBYTES));
		} else {
			MON_ASSERT(msr_id >= 0xc0000000);
			MON_ASSERT(msr_id <= 0xc0001fff);
			bitmap = (void *)(bitmap_hva + (3 KILOBYTES));
		}
	}

	return vmexit_analysis_is_bit_set_in_bitmap(bitmap, bitmap_pos);
}

static
boolean_t vmexit_analysis_rdmsr_exiting(guest_cpu_handle_t gcpu,
					vmcs_object_t *vmexit_vmcs UNUSED,
					vmcs_object_t *control_vmcs)
{
	return vmexit_analysis_msr_access_exiting(gcpu, control_vmcs, TRUE);
}

static
boolean_t vmexit_analysis_wrmsr_exiting(guest_cpu_handle_t gcpu,
					vmcs_object_t *vmexit_vmcs UNUSED,
					vmcs_object_t *control_vmcs)
{
	return vmexit_analysis_msr_access_exiting(gcpu, control_vmcs, FALSE);
}

static
boolean_t vmexit_analysis_timer_exiting(guest_cpu_handle_t gcpu,
					vmcs_object_t *vmexit_vmcs UNUSED,
					vmcs_object_t *control_vmcs)
{
	/* VMEXIT-request Analysis Algorithm:
	 * ---------------------------------
	 * if Save-Value == 0                 VMEXIT-requested = TRUE;
	 * else if (counter <= other-counter) VMEXIT-requested = TRUE;
	 * else                               VMEXIT-requested = FALSE; */

	pin_based_vm_execution_controls_t pin_exec;
	pin_based_vm_execution_controls_t peer_pin_exec;
	boolean_t vmexit_requested = FALSE;
	vmcs_object_t *peer_control_vmcs;
	vmexit_controls_t vmexit_ctrls;
	uint32_t counter_value;
	uint32_t peer_counter_value;

	pin_exec.uint32 = (uint32_t)mon_vmcs_read(control_vmcs,
		VMCS_CONTROL_VECTOR_PIN_EVENTS);
	if (1 == pin_exec.bits.vmx_timer) {
		/* find other VMCS */
		if (VMCS_LEVEL_0 == vmcs_get_level(control_vmcs)) {
			peer_control_vmcs = gcpu_get_vmcs_layered(gcpu,
				VMCS_LEVEL_1);
		} else if (VMCS_LEVEL_1 == vmcs_get_level(control_vmcs)) {
			peer_control_vmcs = gcpu_get_vmcs_layered(gcpu,
				VMCS_LEVEL_0);
		} else {
			MON_ASSERT(0);
			return TRUE;
		}

		peer_pin_exec.uint32 = (uint32_t)mon_vmcs_read(
			peer_control_vmcs,
			VMCS_CONTROL_VECTOR_PIN_EVENTS);
		if (0 == peer_pin_exec.bits.vmx_timer) {
			/* if other vmcs did not requested it */
			/* apparently it did the current level vmcs. don't check further */
			vmexit_requested = TRUE;
		} else {
			/* here both layers requested VMEXIT */
			vmexit_ctrls.uint32 = (uint32_t)mon_vmcs_read(
				control_vmcs,
				VMCS_EXIT_CONTROL_VECTOR);
			if (vmexit_ctrls.bits.save_vmx_timer) {
				counter_value =
					(uint32_t)mon_vmcs_read(control_vmcs,
						VMCS_PREEMPTION_TIMER);
				peer_counter_value =
					(uint32_t)mon_vmcs_read(
						peer_control_vmcs,
						VMCS_PREEMPTION_TIMER);
				if (counter_value <= peer_counter_value) {
					vmexit_requested = TRUE;
				}
			} else {
				/* :BUGBUG: Dima insists to handle this case in a more precise
				 * way */
				MON_ASSERT(0);
				vmexit_requested = TRUE;
			}
		}
	}

	return vmexit_requested;
}

func_vmexit_is_control_requested_t
	vmexit_is_control_requested_func[IA32_VMX_EXIT_BASIC_REASON_COUNT] = {
	/* 0 IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI */
	vmexit_analysis_softinterrupt_exception_nmi_exiting,
	/* 1 IA32_VMX_EXIT_BASIC_REASON_HARDWARE_INTERRUPT */
	vmexit_analysis_hardware_interrupt_exiting,
	/* 2 IA32_VMX_EXIT_BASIC_REASON_TRIPLE_FAULT */
	vmexit_analysis_true_func,
	/* 3 IA32_VMX_EXIT_BASIC_REASON_INIT_EVENT */
	vmexit_analysis_true_func,
	/* 4 IA32_VMX_EXIT_BASIC_REASON_SIPI_EVENT */
	vmexit_analysis_true_func,
	/* 5 IA32_VMX_EXIT_BASIC_REASON_SMI_IO_EVENT */
	vmexit_analysis_true_func,
	/* 6 IA32_VMX_EXIT_BASIC_REASON_SMI_OTHER_EVENT */
	vmexit_analysis_true_func,
	/* 7 IA32_VMX_EXIT_BASIC_REASON_PENDING_INTERRUPT */
	vmexit_analysis_interrupt_window_exiting,
	/* 8 IA32_VMX_EXIT_NMI_WINDOW */
	vmexit_analysis_nmi_window_exiting,
	/* 9 IA32_VMX_EXIT_BASIC_REASON_TASK_SWITCH */
	vmexit_analysis_true_func,
	/* 10 IA32_VMX_EXIT_BASIC_REASON_CPUID_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 11 IA32_VMX_EXIT_BASIC_REASON_GETSEC_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 12 IA32_VMX_EXIT_BASIC_REASON_HLT_INSTRUCTION */
	vmexit_analysis_hlt_inst_exiting,
	/* 13 IA32_VMX_EXIT_BASIC_REASON_INVD_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 14 IA32_VMX_EXIT_BASIC_REASON_INVLPG_INSTRUCTION */
	vmexit_analysis_invlpg_inst_exiting,
	/* 15 IA32_VMX_EXIT_BASIC_REASON_RDPMC_INSTRUCTION */
	vmexit_analysis_rdpmc_inst_exiting,
	/* 16 IA32_VMX_EXIT_BASIC_REASON_RDTSC_INSTRUCTION */
	vmexit_analysis_rdtsc_inst_exiting,
	/* 17 IA32_VMX_EXIT_BASIC_REASON_RSM_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 18 IA32_VMX_EXIT_BASIC_REASON_VMCALL_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 19 IA32_VMX_EXIT_BASIC_REASON_VMCLEAR_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 20 IA32_VMX_EXIT_BASIC_REASON_VMLAUNCH_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 21 IA32_VMX_EXIT_BASIC_REASON_VMPTRLD_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 22 IA32_VMX_EXIT_BASIC_REASON_VMPTRST_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 23 IA32_VMX_EXIT_BASIC_REASON_VMREAD_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 24 IA32_VMX_EXIT_BASIC_REASON_VMRESUME_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 25 IA32_VMX_EXIT_BASIC_REASON_VMWRITE_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 26 IA32_VMX_EXIT_BASIC_REASON_VMXOFF_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 27 IA32_VMX_EXIT_BASIC_REASON_VMXON_INSTRUCTION */
	vmexit_analysis_true_func,
	/* 28 IA32_VMX_EXIT_BASIC_REASON_CR_ACCESS */
	vmexit_analysis_cr_access_exiting,
	/* 29 IA32_VMX_EXIT_BASIC_REASON_DR_ACCESS */
	vmexit_analysis_dr_access_exiting,
	/* 30 IA32_VMX_EXIT_BASIC_REASON_IO_INSTRUCTION */
	vmexit_analysis_io_exiting,
	/* 31 IA32_VMX_EXIT_BASIC_REASON_MSR_READ */
	vmexit_analysis_rdmsr_exiting,
	/* 32 IA32_VMX_EXIT_BASIC_REASON_MSR_WRITE */
	vmexit_analysis_wrmsr_exiting,
	/* 33 IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_GUEST_STATE */
	vmexit_analysis_true_func,
	/* 34 IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_MSR_LOADING */
	vmexit_analysis_true_func,
	/* 35 IA32_VMX_EXIT_BASIC_REASON_FAILED_VMEXIT */
	vmexit_analysis_false_func,
	/* 36 IA32_VMX_EXIT_BASIC_REASON_MWAIT_INSTRUCTION */
	vmexit_analysis_mwait_inst_exiting,
	/* 37 IA32_VMX_EXIT_BASIC_REASON_MONITOR_TRAP_FLAG */
	vmexit_analysis_false_func,
	/* 38 IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_38 */
	vmexit_analysis_false_func,
	/* 39 IA32_VMX_EXIT_BASIC_REASON_MONITOR */
	vmexit_analysis_monitor_inst_exiting,
	/* 40 IA32_VMX_EXIT_BASIC_REASON_PAUSE */
	vmexit_analysis_pause_inst_exiting,
	/* 41 IA32_VMX_EXIT_BASIC_REASON_FAILURE_DUE_MACHINE_CHECK */
	vmexit_analysis_true_func,
	/* 42 IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_42 */
	vmexit_analysis_false_func,
	/* 43 IA32_VMX_EXIT_BASIC_REASON_TPR_BELOW_THRESHOLD */
	vmexit_analysis_false_func,
	/* 44 IA32_VMX_EXIT_BASIC_REASON_APIC_ACCESS */
	vmexit_analysis_false_func,
	/* 45 IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_45 */
	vmexit_analysis_false_func,
	/* 46 IA32_VMX_EXIT_BASIC_REASON_GDTR_LDTR_ACCESS */
	vmexit_analysis_false_func,
	/* 47 IA32_VMX_EXIT_BASIC_REASON_LDTR_TR_ACCESS */
	vmexit_analysis_false_func,
	/* 48 IA32_VMX_EXIT_BASIC_REASON_EPT_VIOLATION */
	vmexit_analysis_false_func,
	/* 48 IA32_VMX_EXIT_BASIC_REASON_EPT_MISCONFIGURATION */
	vmexit_analysis_false_func,
	/* 50 IA32_VMX_EXIT_BASIC_REASON_INVEPT_INSTRUCTION */
	vmexit_analysis_false_func,
	/* 51 IA32_VMX_EXIT_BASIC_REASON_RDTSCP_INSTRUCTION */
	vmexit_analysis_false_func,
	/* 52 IA32_VMX_EXIT_BASIC_REASON_PREEMPTION_TIMER_EXPIRED */
	vmexit_analysis_timer_exiting,
	/* 53 IA32_VMX_EXIT_BASIC_REASON_INVVPID_INSTRUCTION */
	vmexit_analysis_false_func,
	/* 54 IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_54 */
	vmexit_analysis_false_func,
	/* 55 IA32_VMX_EXIT_BASIC_REASON_XSETBV_INSTRUCTION */
	vmexit_analysis_true_func
};

boolean_t vmexit_analysis_was_control_requested(guest_cpu_handle_t gcpu,
						vmcs_object_t *vmexit_vmcs,
						vmcs_object_t *control_vmcs,
						ia32_vmx_exit_basic_reason_t
						exit_reason)
{
	if (exit_reason >= IA32_VMX_EXIT_BASIC_REASON_COUNT) {
		return FALSE;
	}

	MON_ASSERT(vmexit_vmcs != NULL);
	MON_ASSERT(control_vmcs != NULL);

	return vmexit_is_control_requested_func[exit_reason] (gcpu, vmexit_vmcs,
							      control_vmcs);
}
