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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMCS_MERGE_SPLIT_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMCS_MERGE_SPLIT_C, __condition)
#include <mon_defs.h>
#include <mon_dbg.h>
#include <vmcs_api.h>
#include <vmx_ctrl_msrs.h>
#include <vmx_vmcs.h>
#include <pfec.h>
#include <host_memory_manager_api.h>
#include <guest.h>
#include <guest_cpu.h>
#include <em64t_defs.h>
#include <gpm_api.h>
#include <ia32_defs.h>
#include "vmcs_internal.h"


typedef uint32_t msr_list_copy_mode_t;  /* mitmask */
#define MSR_LIST_COPY_NO_CHANGE 0x0
#define MSR_LIST_COPY_WITH_EFER_CHANGE 0x1
#define MSR_LIST_COPY_AND_SET_32_BIT_MODE_IN_EFER \
	(0x00 | MSR_LIST_COPY_WITH_EFER_CHANGE)
#define MSR_LIST_COPY_AND_SET_64_BIT_MODE_IN_EFER \
	(0x10 | MSR_LIST_COPY_WITH_EFER_CHANGE)
#define MSR_LIST_COPY_UPDATE_GCPU 0x100

typedef enum {
	MS_HVA,
	MS_GPA,
	MS_HPA
} ms_mem_address_type_t;

static void ms_merge_timer_to_level2(vmcs_object_t *vmcs_0,
				     vmcs_object_t *vmcs_1,
				     vmcs_object_t *vmcs_m);
static void ms_split_timer_from_level2(vmcs_object_t *vmcs_0,
				       vmcs_object_t *vmcs_1,
				       vmcs_object_t *vmcs_m);

static
void ms_copy_guest_state_to_level1_vmcs(IN guest_cpu_handle_t gcpu,
					IN boolean_t copy_crs)
{
	IN vmcs_object_t *level1_vmcs =
		vmcs_hierarchy_get_vmcs(gcpu_get_vmcs_hierarchy(
				gcpu), VMCS_LEVEL_1);
	IN vmcs_object_t *merged_vmcs =
		vmcs_hierarchy_get_vmcs(gcpu_get_vmcs_hierarchy(
				gcpu), VMCS_MERGED);
	uint64_t value;
	uint16_t selector;
	uint64_t base;
	uint32_t limit;
	uint32_t ar;
	uint64_t vmentry_control;

	if (copy_crs) {
		value = gcpu_get_control_reg_layered(gcpu,
			IA32_CTRL_CR0,
			VMCS_MERGED);
		gcpu_set_control_reg_layered(gcpu,
			IA32_CTRL_CR0,
			value,
			VMCS_LEVEL_1);

		value = gcpu_get_control_reg_layered(gcpu,
			IA32_CTRL_CR3,
			VMCS_MERGED);
		gcpu_set_control_reg_layered(gcpu,
			IA32_CTRL_CR3,
			value,
			VMCS_LEVEL_1);

		value = gcpu_get_control_reg_layered(gcpu,
			IA32_CTRL_CR4,
			VMCS_MERGED);
		gcpu_set_control_reg_layered(gcpu,
			IA32_CTRL_CR4,
			value,
			VMCS_LEVEL_1);
	}

	value = gcpu_get_debug_reg_layered(gcpu, IA32_REG_DR7, VMCS_MERGED);
	gcpu_set_debug_reg_layered(gcpu, IA32_REG_DR7, value, VMCS_LEVEL_1);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_ES,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_MERGED);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_ES,
		selector,
		base,
		limit,
		ar,
		VMCS_LEVEL_1);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_CS,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_MERGED);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_CS,
		selector,
		base,
		limit,
		ar,
		VMCS_LEVEL_1);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_SS,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_MERGED);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_SS,
		selector,
		base,
		limit,
		ar,
		VMCS_LEVEL_1);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_DS,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_MERGED);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_DS,
		selector,
		base,
		limit,
		ar,
		VMCS_LEVEL_1);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_FS,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_MERGED);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_FS,
		selector,
		base,
		limit,
		ar,
		VMCS_LEVEL_1);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_GS,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_MERGED);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_GS,
		selector,
		base,
		limit,
		ar,
		VMCS_LEVEL_1);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_LDTR,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_MERGED);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_LDTR,
		selector,
		base,
		limit,
		ar,
		VMCS_LEVEL_1);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_TR,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_MERGED);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_TR,
		selector,
		base,
		limit,
		ar,
		VMCS_LEVEL_1);

	gcpu_get_gdt_reg_layered(gcpu, &base, &limit, VMCS_MERGED);
	gcpu_set_gdt_reg_layered(gcpu, base, limit, VMCS_LEVEL_1);

	gcpu_get_idt_reg_layered(gcpu, &base, &limit, VMCS_MERGED);
	gcpu_set_idt_reg_layered(gcpu, base, limit, VMCS_LEVEL_1);

	value = gcpu_get_gp_reg_layered(gcpu, IA32_REG_RSP, VMCS_MERGED);
	gcpu_set_gp_reg_layered(gcpu, IA32_REG_RSP, value, VMCS_LEVEL_1);

	value = gcpu_get_gp_reg_layered(gcpu, IA32_REG_RIP, VMCS_MERGED);
	gcpu_set_gp_reg_layered(gcpu, IA32_REG_RIP, value, VMCS_LEVEL_1);

	value = gcpu_get_gp_reg_layered(gcpu, IA32_REG_RFLAGS, VMCS_MERGED);
	gcpu_set_gp_reg_layered(gcpu, IA32_REG_RFLAGS, value, VMCS_LEVEL_1);

	value = gcpu_get_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_CS,
		VMCS_MERGED);
	gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_CS, value,
		VMCS_LEVEL_1);

	value = gcpu_get_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_ESP,
		VMCS_MERGED);
	gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_ESP, value,
		VMCS_LEVEL_1);

	value = gcpu_get_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_EIP,
		VMCS_MERGED);
	gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_EIP, value,
		VMCS_LEVEL_1);

	value = gcpu_get_pending_debug_exceptions_layered(gcpu, VMCS_MERGED);
	gcpu_set_pending_debug_exceptions_layered(gcpu, value, VMCS_LEVEL_1);

	value =
		gcpu_get_msr_reg_layered(gcpu, IA32_MON_MSR_SMBASE,
			VMCS_MERGED);
	gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_SMBASE, value,
		VMCS_LEVEL_1);

	value = gcpu_get_msr_reg_layered(gcpu,
		IA32_MON_MSR_DEBUGCTL,
		VMCS_MERGED);
	gcpu_set_msr_reg_layered(gcpu,
		IA32_MON_MSR_DEBUGCTL,
		value,
		VMCS_LEVEL_1);

	if (vmcs_field_is_supported(VMCS_GUEST_IA32_PERF_GLOBAL_CTRL)) {
		value =
			gcpu_get_msr_reg_layered(gcpu,
				IA32_MON_MSR_PERF_GLOBAL_CTRL,
				VMCS_MERGED);
		gcpu_set_msr_reg_layered(gcpu,
			IA32_MON_MSR_PERF_GLOBAL_CTRL,
			value,
			VMCS_LEVEL_1);
	}

	value = mon_vmcs_read(merged_vmcs, VMCS_GUEST_WORKING_VMCS_PTR);
	mon_vmcs_write(level1_vmcs, VMCS_GUEST_WORKING_VMCS_PTR, value);

	value = gcpu_get_interruptibility_state_layered(gcpu, VMCS_MERGED);
	gcpu_set_interruptibility_state_layered(gcpu,
		(uint32_t)value,
		VMCS_LEVEL_1);

	value = gcpu_get_activity_state_layered(gcpu, VMCS_MERGED);
	gcpu_set_activity_state_layered(gcpu,
		(ia32_vmx_vmcs_guest_sleep_state_t)value,
		VMCS_LEVEL_1);

	/*
	 * Copy IA32e Guest bit is a part of guest state, so copy it here
	 */
#define VMENTER_IA32E_MODE_GUEST 0x200
	vmentry_control = mon_vmcs_read(merged_vmcs, VMCS_ENTER_CONTROL_VECTOR);
	vmcs_update(level1_vmcs, VMCS_ENTER_CONTROL_VECTOR, vmentry_control,
		VMENTER_IA32E_MODE_GUEST);

	/* TODO VMCS v2 fields */
}

static
void ms_copy_guest_state_flom_level1(IN guest_cpu_handle_t gcpu,
				     IN boolean_t copy_crs)
{
	IN vmcs_object_t *level1_vmcs =
		vmcs_hierarchy_get_vmcs(gcpu_get_vmcs_hierarchy(
				gcpu), VMCS_LEVEL_1);
	IN vmcs_object_t *merged_vmcs =
		vmcs_hierarchy_get_vmcs(gcpu_get_vmcs_hierarchy(
				gcpu), VMCS_MERGED);
	uint64_t value;
	uint16_t selector;
	uint64_t base;
	uint32_t limit;
	uint32_t ar;

	if (copy_crs) {
		value = gcpu_get_control_reg_layered(gcpu,
			IA32_CTRL_CR0,
			VMCS_LEVEL_1);
		gcpu_set_control_reg_layered(gcpu,
			IA32_CTRL_CR0,
			value,
			VMCS_MERGED);

		value = gcpu_get_control_reg_layered(gcpu,
			IA32_CTRL_CR3,
			VMCS_LEVEL_1);
		gcpu_set_control_reg_layered(gcpu,
			IA32_CTRL_CR3,
			value,
			VMCS_MERGED);

		value = gcpu_get_control_reg_layered(gcpu,
			IA32_CTRL_CR4,
			VMCS_LEVEL_1);
		gcpu_set_control_reg_layered(gcpu,
			IA32_CTRL_CR4,
			value,
			VMCS_MERGED);
	}

	value = gcpu_get_debug_reg_layered(gcpu, IA32_REG_DR7, VMCS_LEVEL_1);
	gcpu_set_debug_reg_layered(gcpu, IA32_REG_DR7, value, VMCS_MERGED);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_ES,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_LEVEL_1);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_ES,
		selector,
		base,
		limit,
		ar,
		VMCS_MERGED);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_CS,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_LEVEL_1);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_CS,
		selector,
		base,
		limit,
		ar,
		VMCS_MERGED);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_SS,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_LEVEL_1);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_SS,
		selector,
		base,
		limit,
		ar,
		VMCS_MERGED);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_DS,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_LEVEL_1);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_DS,
		selector,
		base,
		limit,
		ar,
		VMCS_MERGED);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_FS,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_LEVEL_1);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_FS,
		selector,
		base,
		limit,
		ar,
		VMCS_MERGED);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_GS,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_LEVEL_1);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_GS,
		selector,
		base,
		limit,
		ar,
		VMCS_MERGED);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_LDTR,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_LEVEL_1);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_LDTR,
		selector,
		base,
		limit,
		ar,
		VMCS_MERGED);

	gcpu_get_segment_reg_layered(gcpu,
		IA32_SEG_TR,
		&selector,
		&base,
		&limit,
		&ar,
		VMCS_LEVEL_1);
	gcpu_set_segment_reg_layered(gcpu,
		IA32_SEG_TR,
		selector,
		base,
		limit,
		ar,
		VMCS_MERGED);

	gcpu_get_gdt_reg_layered(gcpu, &base, &limit, VMCS_LEVEL_1);
	gcpu_set_gdt_reg_layered(gcpu, base, limit, VMCS_MERGED);

	gcpu_get_idt_reg_layered(gcpu, &base, &limit, VMCS_LEVEL_1);
	gcpu_set_idt_reg_layered(gcpu, base, limit, VMCS_MERGED);

	value = gcpu_get_gp_reg_layered(gcpu, IA32_REG_RSP, VMCS_LEVEL_1);
	gcpu_set_gp_reg_layered(gcpu, IA32_REG_RSP, value, VMCS_MERGED);

	value = gcpu_get_gp_reg_layered(gcpu, IA32_REG_RIP, VMCS_LEVEL_1);
	gcpu_set_gp_reg_layered(gcpu, IA32_REG_RIP, value, VMCS_MERGED);

	value = gcpu_get_gp_reg_layered(gcpu, IA32_REG_RFLAGS, VMCS_LEVEL_1);
	gcpu_set_gp_reg_layered(gcpu, IA32_REG_RFLAGS, value, VMCS_MERGED);

	value = gcpu_get_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_CS,
		VMCS_LEVEL_1);
	gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_CS, value,
		VMCS_MERGED);

	value = gcpu_get_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_ESP,
		VMCS_LEVEL_1);
	gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_ESP, value,
		VMCS_MERGED);

	value = gcpu_get_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_EIP,
		VMCS_LEVEL_1);
	gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_SYSENTER_EIP, value,
		VMCS_MERGED);

	value = gcpu_get_pending_debug_exceptions_layered(gcpu, VMCS_LEVEL_1);
	gcpu_set_pending_debug_exceptions_layered(gcpu, value, VMCS_MERGED);

	value =
		gcpu_get_msr_reg_layered(gcpu, IA32_MON_MSR_SMBASE,
			VMCS_LEVEL_1);
	gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_SMBASE, value, VMCS_MERGED);

	value = gcpu_get_msr_reg_layered(gcpu,
		IA32_MON_MSR_DEBUGCTL,
		VMCS_LEVEL_1);
	gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_DEBUGCTL, value,
		VMCS_MERGED);

	if (vmcs_field_is_supported(VMCS_GUEST_IA32_PERF_GLOBAL_CTRL)) {
		value = gcpu_get_msr_reg_layered(gcpu,
			IA32_MON_MSR_PERF_GLOBAL_CTRL,
			VMCS_LEVEL_1);
		gcpu_set_msr_reg_layered(gcpu,
			IA32_MON_MSR_PERF_GLOBAL_CTRL,
			value,
			VMCS_MERGED);
	}
	value = mon_vmcs_read(level1_vmcs, VMCS_GUEST_WORKING_VMCS_PTR);
	mon_vmcs_write(merged_vmcs, VMCS_GUEST_WORKING_VMCS_PTR, value);

	value = gcpu_get_interruptibility_state_layered(gcpu, VMCS_LEVEL_1);
	gcpu_set_interruptibility_state_layered(gcpu,
		(uint32_t)value,
		VMCS_MERGED);

	value = (uint64_t)gcpu_get_activity_state_layered(gcpu, VMCS_LEVEL_1);
	gcpu_set_activity_state_layered(gcpu,
		(ia32_vmx_vmcs_guest_sleep_state_t)value,
		VMCS_MERGED);

	/* TODO VMCS v2 fields */
}

static
void ms_copy_data_fields(IN OUT vmcs_object_t *vmcs_to,
			 IN vmcs_object_t *vmcs_from)
{
	uint64_t value;

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_INSTRUCTION_ERROR_CODE);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_INSTRUCTION_ERROR_CODE,
		value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_REASON);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_REASON, value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_EXCEPTION_INFO);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_EXCEPTION_INFO, value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_EXCEPTION_ERROR_CODE);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_EXCEPTION_ERROR_CODE, value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_IDT_VECTORING);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_IDT_VECTORING, value);

	value =
		mon_vmcs_read(vmcs_from,
			VMCS_EXIT_INFO_IDT_VECTORING_ERROR_CODE);
	vmcs_write_nocheck(vmcs_to,
		VMCS_EXIT_INFO_IDT_VECTORING_ERROR_CODE,
		value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_INSTRUCTION_LENGTH);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_INSTRUCTION_LENGTH, value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_INSTRUCTION_INFO);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_INSTRUCTION_INFO, value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_QUALIFICATION);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_QUALIFICATION, value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_IO_RCX);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_IO_RCX, value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_IO_RSI);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_IO_RSI, value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_IO_RDI);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_IO_RDI, value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_IO_RIP);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_IO_RIP, value);

	value = mon_vmcs_read(vmcs_from, VMCS_EXIT_INFO_GUEST_LINEAR_ADDRESS);
	vmcs_write_nocheck(vmcs_to, VMCS_EXIT_INFO_GUEST_LINEAR_ADDRESS, value);

	/* TODO: Copy VMCS v2 fields */
}

static
void ms_copy_host_state(IN OUT vmcs_object_t *vmcs_to,
			IN vmcs_object_t *vmcs_from)
{
	uint64_t value;

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_CR0);
	mon_vmcs_write(vmcs_to, VMCS_HOST_CR0, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_CR3);
	mon_vmcs_write(vmcs_to, VMCS_HOST_CR3, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_CR4);
	mon_vmcs_write(vmcs_to, VMCS_HOST_CR4, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_ES_SELECTOR);
	mon_vmcs_write(vmcs_to, VMCS_HOST_ES_SELECTOR, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_CS_SELECTOR);
	mon_vmcs_write(vmcs_to, VMCS_HOST_CS_SELECTOR, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_SS_SELECTOR);
	mon_vmcs_write(vmcs_to, VMCS_HOST_SS_SELECTOR, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_DS_SELECTOR);
	mon_vmcs_write(vmcs_to, VMCS_HOST_DS_SELECTOR, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_FS_SELECTOR);
	mon_vmcs_write(vmcs_to, VMCS_HOST_FS_SELECTOR, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_FS_BASE);
	mon_vmcs_write(vmcs_to, VMCS_HOST_FS_BASE, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_GS_SELECTOR);
	mon_vmcs_write(vmcs_to, VMCS_HOST_GS_SELECTOR, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_GS_BASE);
	mon_vmcs_write(vmcs_to, VMCS_HOST_GS_BASE, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_TR_SELECTOR);
	mon_vmcs_write(vmcs_to, VMCS_HOST_TR_SELECTOR, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_TR_BASE);
	mon_vmcs_write(vmcs_to, VMCS_HOST_TR_BASE, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_GDTR_BASE);
	mon_vmcs_write(vmcs_to, VMCS_HOST_GDTR_BASE, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_IDTR_BASE);
	mon_vmcs_write(vmcs_to, VMCS_HOST_IDTR_BASE, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_RSP);
	mon_vmcs_write(vmcs_to, VMCS_HOST_RSP, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_RIP);
	mon_vmcs_write(vmcs_to, VMCS_HOST_RIP, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_SYSENTER_CS);
	mon_vmcs_write(vmcs_to, VMCS_HOST_SYSENTER_CS, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_SYSENTER_ESP);
	mon_vmcs_write(vmcs_to, VMCS_HOST_SYSENTER_ESP, value);

	value = mon_vmcs_read(vmcs_from, VMCS_HOST_SYSENTER_EIP);
	mon_vmcs_write(vmcs_to, VMCS_HOST_SYSENTER_EIP, value);

	/* TODO VMCS v2 fields */
}

static
boolean_t may_cause_vmexit_on_page_fault(IN guest_cpu_handle_t gcpu,
					 IN vmcs_level_t level)
{
	uint32_t possible_pfec_mask = (1 << MON_PFEC_NUM_OF_USED_BITS) - 1;
	uint32_t vmcs_pfec_mask;
	uint32_t vmcs_pfec_match;
	ia32_vmcs_exception_bitmap_t exception_ctrls;

	gcpu_get_pf_error_code_mask_and_match_layered(gcpu,
		level,
		&vmcs_pfec_mask,
		&vmcs_pfec_match);

	exception_ctrls.uint32 =
		(uint32_t)gcpu_get_exceptions_map_layered(gcpu, level);

	if (exception_ctrls.bits.pf == 1) {
		if ((vmcs_pfec_match & possible_pfec_mask) != vmcs_pfec_match) {
			/* There are bits which are set in PFEC_MATCH, but will be
			 * cleared in actual PFEC */
			return FALSE;
		}

		if ((vmcs_pfec_mask & vmcs_pfec_match) != vmcs_pfec_match) {
			/* There are bits which are set in PFEC_MATCH, but are
			 * cleared in PFEC_MASK */
			return FALSE;
		}

		/* There still can be values of PFEC_MASK and PFEC_MATCH that will
		 * never cause VMExits on PF. */
		return TRUE;
	} else {
		if ((vmcs_pfec_match == 0x00000000) &&
		    ((vmcs_pfec_mask & possible_pfec_mask) == 0)) {
			return FALSE;
		}

		return TRUE;
	}
}

static
uint64_t ms_merge_cr_shadow(IN guest_cpu_handle_t gcpu,
			    IN mon_ia32_control_registers_t reg)
{
	uint64_t level1_shadow =
		gcpu_get_guest_visible_control_reg_layered(gcpu,
			reg,
			VMCS_LEVEL_1);
	uint64_t level0_mask;
	uint64_t level1_mask;
	uint64_t level1_reg = gcpu_get_control_reg_layered(gcpu,
		reg,
		VMCS_LEVEL_1);
	uint64_t merged_shadow;
	uint64_t mask_tmp;

	if (reg == IA32_CTRL_CR0) {
		level0_mask = gcpu_get_cr0_reg_mask_layered(gcpu, VMCS_LEVEL_0);
		level1_mask = gcpu_get_cr0_reg_mask_layered(gcpu, VMCS_LEVEL_1);
	} else {
		MON_ASSERT(reg == IA32_CTRL_CR4);
		level0_mask = gcpu_get_cr4_reg_mask_layered(gcpu, VMCS_LEVEL_0);
		level1_mask = gcpu_get_cr4_reg_mask_layered(gcpu, VMCS_LEVEL_1);
	}

	merged_shadow = level1_shadow;

	/* clear all bits that are 0 in mask */
	merged_shadow &= level1_mask;

	/* Copy bits that are 0 in level1_mask and
	 * 1 in level0_mask from level1_reg */
	mask_tmp = (level0_mask ^ level1_mask) & level0_mask;
	merged_shadow |= (mask_tmp & level1_reg);

	return merged_shadow;
}

static
void *ms_retrieve_ptr_to_additional_memory(IN vmcs_object_t *vmcs,
					   IN vmcs_field_t field,
					   IN ms_mem_address_type_t mem_type)
{
	uint64_t addr_value = mon_vmcs_read(vmcs, field);
	uint64_t addr_hpa;
	uint64_t addr_hva;
	mam_attributes_t attrs;

	if (mem_type == MS_HVA) {
		return (void *)addr_value;
	}

	if (mem_type == MS_GPA) {
		guest_cpu_handle_t gcpu = vmcs_get_owner(vmcs);
		guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
		gpm_handle_t gpm = gcpu_get_current_gpm(guest);
		if (!mon_gpm_gpa_to_hpa(gpm, addr_value, &addr_hpa, &attrs)) {
			MON_DEADLOOP();
		}
	} else {
		MON_ASSERT(mem_type == MS_HPA);
		addr_hpa = addr_value;
	}

	if (!mon_hmm_hpa_to_hva(addr_hpa, &addr_hva)) {
		MON_DEADLOOP();
	}

	return (void *)addr_hva;
}

static
void ms_merge_bitmaps(IN void *bitmap0,
		      IN void *bitmap1, IN OUT void *merged_bitmap)
{
	uint64_t bitmap0_hva = (uint64_t)bitmap0;
	uint64_t bitmap1_hva = (uint64_t)bitmap1;
	uint64_t merged_bitmap_hva = (uint64_t)merged_bitmap;
	uint64_t merged_bitmap_hva_final = merged_bitmap_hva + PAGE_4KB_SIZE;

	MON_ASSERT((bitmap0 != NULL) || (bitmap1 != NULL));

	MON_ASSERT(merged_bitmap);

	while (merged_bitmap_hva < merged_bitmap_hva_final) {
		uint64_t value0 =
			(bitmap0 ==
			 NULL) ? (uint64_t)0 : *((uint64_t *)bitmap0_hva);
		uint64_t value1 =
			(bitmap1 ==
			 NULL) ? (uint64_t)0 : *((uint64_t *)bitmap1_hva);
		uint64_t merged_value = value0 | value1;

		*((uint64_t *)merged_bitmap_hva) = merged_value;

		bitmap0_hva += sizeof(uint64_t);
		bitmap1_hva += sizeof(uint64_t);
		merged_bitmap_hva += sizeof(uint64_t);
	}
}

static
boolean_t ms_is_msr_in_list(IN ia32_vmx_msr_entry_t *list,
			    IN uint32_t msr_index,
			    IN uint32_t count, OUT uint64_t *value)
{
	uint32_t i;

	for (i = count; i > 0; i--) {
		if (list[i - 1].msr_index == msr_index) {
			if (value != NULL) {
				*value = list[i - 1].msr_data;
			}
			return TRUE;
		}
	}
	return FALSE;
}

static
void ms_merge_msr_list(IN guest_cpu_handle_t gcpu,
		       IN vmcs_object_t *merged_vmcs,
		       IN ia32_vmx_msr_entry_t *first_list,
		       IN ia32_vmx_msr_entry_t *second_list,
		       IN uint32_t first_list_count,
		       IN uint32_t second_list_count,
		       IN msr_list_copy_mode_t copy_mode,
		       IN func_vmcs_add_msr_t add_msr_func,
		       IN func_vmcs_clear_msr_list_t clear_list_func,
		       IN func_vmcs_is_msr_in_list_t is_msr_in_list_func,
		       IN vmcs_field_t msr_list_addr_field,
		       IN vmcs_field_t msr_list_count_field)
{
	uint32_t i;

	clear_list_func(merged_vmcs);

	for (i = 0; i < first_list_count; i++)
		add_msr_func(merged_vmcs, first_list[i].msr_index,
			first_list[i].msr_data);

	for (i = 0; i < second_list_count; i++) {
		if (!is_msr_in_list_func(merged_vmcs,
			    second_list[i].msr_index)) {
			add_msr_func(merged_vmcs, second_list[i].msr_index,
				second_list[i].msr_data);
		}
	}

	if (copy_mode != MSR_LIST_COPY_NO_CHANGE) {
		ia32_vmx_msr_entry_t *merged_list =
			ms_retrieve_ptr_to_additional_memory(merged_vmcs,
				msr_list_addr_field, MS_HPA);
		uint32_t merged_list_count =
			(uint32_t)mon_vmcs_read(merged_vmcs,
				msr_list_count_field);

		for (i = 0; i < merged_list_count; i++) {
			if ((copy_mode & MSR_LIST_COPY_WITH_EFER_CHANGE) &&
			    (merged_list[i].msr_index == IA32_MSR_EFER)) {
				ia32_efer_t *efer =
					(ia32_efer_t *)(&(merged_list[i].
							  msr_data));
				efer->bits.lme =
					((copy_mode &
					  MSR_LIST_COPY_AND_SET_64_BIT_MODE_IN_EFER)
					 ==
					 MSR_LIST_COPY_AND_SET_64_BIT_MODE_IN_EFER)
					? 1 : 0;
				efer->bits.lma = efer->bits.lme;
			}

			if (copy_mode & MSR_LIST_COPY_UPDATE_GCPU) {
				gcpu_set_msr_reg_by_index_layered(gcpu,
					merged_list[i].msr_index,
					merged_list[i].msr_data,
					VMCS_MERGED);
			}
		}
	}
}

static
void ms_split_msr_lists(IN guest_cpu_handle_t gcpu,
			IN ia32_vmx_msr_entry_t *merged_list,
			IN uint32_t merged_list_count)
{
	uint32_t i;

	/* Copy while there is match */
	for (i = 0; i < merged_list_count; i++) {
		gcpu_set_msr_reg_by_index_layered(gcpu,
			merged_list[i].msr_index,
			merged_list[i].msr_data,
			VMCS_LEVEL_0);
		gcpu_set_msr_reg_by_index_layered(gcpu,
			merged_list[i].msr_index,
			merged_list[i].msr_data,
			VMCS_LEVEL_1);
	}
}

static
void ms_perform_cr_split(IN guest_cpu_handle_t gcpu,
			 IN mon_ia32_control_registers_t reg)
{
	uint64_t level1_mask;
	uint64_t merged_mask;
	uint64_t merged_shadow =
		gcpu_get_guest_visible_control_reg_layered(gcpu,
			reg,
			VMCS_MERGED);
	uint64_t level1_reg = gcpu_get_control_reg_layered(gcpu,
		reg,
		VMCS_LEVEL_1);
	uint64_t merged_reg = gcpu_get_control_reg_layered(gcpu,
		reg,
		VMCS_MERGED);
	uint64_t bits_to_take_from_merged_reg;
	uint64_t bits_to_take_from_merged_shadow;

	if (reg == IA32_CTRL_CR0) {
		level1_mask = gcpu_get_cr0_reg_mask_layered(gcpu, VMCS_LEVEL_1);
		merged_mask = gcpu_get_cr0_reg_mask_layered(gcpu, VMCS_MERGED);
	} else {
		MON_ASSERT(reg == IA32_CTRL_CR4);
		level1_mask = gcpu_get_cr4_reg_mask_layered(gcpu, VMCS_LEVEL_1);
		merged_mask = gcpu_get_cr4_reg_mask_layered(gcpu, VMCS_MERGED);
	}

	/* There should not be any bit that is set level1_mask and cleared in
	 * merged_mask */
	MON_ASSERT(((~merged_mask) & level1_mask) == 0);

	bits_to_take_from_merged_reg = ~merged_mask;
	/* bits that 1 in merged and 0 in level1 masks */
	bits_to_take_from_merged_shadow = (merged_mask ^ level1_mask);

	level1_reg = (level1_reg & level1_mask) |
		     (merged_reg & bits_to_take_from_merged_reg) |
		     (merged_shadow & bits_to_take_from_merged_shadow);
	gcpu_set_control_reg_layered(gcpu, reg, level1_reg, VMCS_LEVEL_1);
}

void ms_merge_to_level2(IN guest_cpu_handle_t gcpu,
			IN boolean_t merge_only_dirty)
{
	/* TODO: merge only dirty */
	vmcs_hierarchy_t *hierarchy = gcpu_get_vmcs_hierarchy(gcpu);
	vmcs_object_t *level0_vmcs = vmcs_hierarchy_get_vmcs(hierarchy,
		VMCS_LEVEL_0);
	vmcs_object_t *level1_vmcs = vmcs_hierarchy_get_vmcs(hierarchy,
		VMCS_LEVEL_1);
	vmcs_object_t *merged_vmcs = vmcs_hierarchy_get_vmcs(hierarchy,
		VMCS_MERGED);
	processor_based_vm_execution_controls_t controls0;
	processor_based_vm_execution_controls_t controls1;
	processor_based_vm_execution_controls2_t controls0_2;
	processor_based_vm_execution_controls2_t controls1_2;
	processor_based_vm_execution_controls_t merged_controls;
	processor_based_vm_execution_controls2_t merged_controls_2;

	MON_ASSERT(level0_vmcs && level1_vmcs);

	if ((merge_only_dirty) &&
	    (!vmcs_is_dirty(level0_vmcs)) && (!vmcs_is_dirty(level1_vmcs))) {
		return;
	}

	/* -----------------GUEST_STATE------------------- */
	/* Copy guest state from level-1 vmcs */
	ms_copy_guest_state_flom_level1(gcpu, TRUE /* copy CRs */);

	/* -------------------CONTROLS-------------------- */
	/* Merging controls */

	controls0.uint32 =
		(uint32_t)gcpu_get_processor_ctrls_layered(gcpu, VMCS_LEVEL_0);
	controls1.uint32 =
		(uint32_t)gcpu_get_processor_ctrls_layered(gcpu, VMCS_LEVEL_1);
	controls0_2.uint32 =
		(uint32_t)gcpu_get_processor_ctrls2_layered(gcpu, VMCS_LEVEL_0);
	controls1_2.uint32 =
		(uint32_t)gcpu_get_processor_ctrls2_layered(gcpu, VMCS_LEVEL_1);
	merged_controls.uint32 =
		(uint32_t)gcpu_get_processor_ctrls_layered(gcpu, VMCS_MERGED);
	merged_controls_2.uint32 =
		(uint32_t)gcpu_get_processor_ctrls2_layered(gcpu, VMCS_MERGED);

	/* Pin-based controls */
	{
		uint32_t value0 = (uint32_t)gcpu_get_pin_ctrls_layered(gcpu,
			VMCS_LEVEL_0);
		uint32_t value1 = (uint32_t)gcpu_get_pin_ctrls_layered(gcpu,
			VMCS_LEVEL_1);
		uint32_t merged_value = value0 | value1;

		gcpu_set_pin_ctrls_layered(gcpu, VMCS_MERGED, merged_value);
	}

	/* Exceptions bitmap */
	{
		uint32_t value0 =
			(uint32_t)gcpu_get_exceptions_map_layered(gcpu,
				VMCS_LEVEL_0);
		uint32_t value1 =
			(uint32_t)gcpu_get_exceptions_map_layered(gcpu,
				VMCS_LEVEL_1);
		uint32_t merged_value = value0 | value1;

		gcpu_set_exceptions_map_layered(gcpu, VMCS_MERGED,
			merged_value);
	}

	/* Primary and secondary processor-based controls */
	{
		boolean_t is_ia32e_mode = FALSE;
		vmentry_controls_t entry_ctrls;

		/* bit 2 */
		merged_controls.bits.software_interrupt =
			controls0.bits.software_interrupt |
			controls1.bits.software_interrupt;

		/* bit 3 */
		merged_controls.bits.use_tsc_offsetting =
			controls0.bits.use_tsc_offsetting |
			controls1.bits.use_tsc_offsetting;

		/* bit 7 */
		merged_controls.bits.hlt = controls0.bits.hlt |
					   controls1.bits.hlt;

		/* bit 9 */
		merged_controls.bits.invlpg =
			controls0.bits.invlpg | controls1.bits.invlpg;

		/* bit 10 */
		merged_controls.bits.mwait =
			controls0.bits.mwait | controls1.bits.mwait;

		/* bit 11 */
		merged_controls.bits.rdpmc =
			controls0.bits.rdpmc | controls1.bits.rdpmc;

		/* bit 12 */
		merged_controls.bits.rdtsc =
			controls0.bits.rdtsc | controls1.bits.rdtsc;

		/* bit 19 */
		entry_ctrls.uint32 =
			(uint32_t)gcpu_get_enter_ctrls_layered(gcpu,
				VMCS_LEVEL_1);
		is_ia32e_mode = entry_ctrls.bits.ia32e_mode_guest;
		if (is_ia32e_mode) {
			merged_controls.bits.cr8_load =
				controls0.bits.cr8_load |
				controls1.bits.cr8_load;
		}

		/* bit 20 */
		if (is_ia32e_mode) {
			merged_controls.bits.cr8_store =
				controls0.bits.cr8_store |
				controls1.bits.cr8_store;
		}

		/* bit 21
		 * TPR shadow is currently not supported
		 * TODO: Support for TPR shadow in layering */
		MON_ASSERT(controls0.bits.tpr_shadow == 0);
		MON_ASSERT(controls1.bits.tpr_shadow == 0);

		/* bit 22 */
		merged_controls.bits.nmi_window =
			controls0.bits.nmi_window | controls1.bits.nmi_window;

		/* bit 23 */
		merged_controls.bits.mov_dr =
			controls0.bits.mov_dr | controls1.bits.mov_dr;

		/* bits 24 and 25 */
		if (((controls0.bits.unconditional_io == 1)
		     && (controls0.bits.activate_io_bitmaps == 0))
		    || ((controls1.bits.unconditional_io == 1)
			&& (controls1.bits.activate_io_bitmaps == 0))) {
			merged_controls.bits.unconditional_io = 1;
			merged_controls.bits.activate_io_bitmaps = 0;
		} else {
			merged_controls.bits.unconditional_io = 0;
			merged_controls.bits.activate_io_bitmaps =
				controls0.bits.activate_io_bitmaps | controls1.
				bits.activate_io_bitmaps;
		}

		/* bit 28 */
		merged_controls.bits.use_msr_bitmaps =
			controls0.bits.use_msr_bitmaps &
			controls1.bits.use_msr_bitmaps;

		/* bit 29 */
		merged_controls.bits.monitor =
			controls0.bits.monitor | controls1.bits.monitor;

		/* bit 30 */
		merged_controls.bits.pause =
			controls0.bits.pause | controls1.bits.pause;

		/* bit 31 */
		merged_controls.bits.secondary_controls =
			controls0.bits.secondary_controls |
			controls1.bits.secondary_controls;

		gcpu_set_processor_ctrls_layered(gcpu, VMCS_MERGED,
			merged_controls.uint32);

		/* Secondary controls */
		if (controls0.bits.secondary_controls == 0) {
			controls0_2.uint32 = 0;
		}

		if (controls1.bits.secondary_controls == 0) {
			controls1_2.uint32 = 0;
		}

		merged_controls_2.uint32 = controls0_2.uint32 |
					   controls1_2.uint32;

		gcpu_set_processor_ctrls2_layered(gcpu, VMCS_MERGED,
			merged_controls_2.uint32);
	}

	/* Executive VMCS pointer */
	{
		uint64_t value =
			mon_vmcs_read(level1_vmcs,
				VMCS_OSV_CONTROLLING_VMCS_ADDRESS);
		mon_vmcs_write(merged_vmcs,
			VMCS_OSV_CONTROLLING_VMCS_ADDRESS,
			value);
	}

	/* Entry controls */
	{
		uint32_t value =
			(uint32_t)gcpu_get_enter_ctrls_layered(gcpu,
				VMCS_LEVEL_1);
		gcpu_set_enter_ctrls_layered(gcpu, VMCS_MERGED, value);

#ifdef DEBUG
		{
			vmentry_controls_t ctrls;
			ctrls.uint32 = value;
			MON_ASSERT(ctrls.bits.load_ia32_perf_global_ctrl == 0);
		}
#endif
	}

	/* Interruption-information field */
	{
		uint32_t value =
			(uint32_t)mon_vmcs_read(level1_vmcs,
				VMCS_ENTER_INTERRUPT_INFO);
		mon_vmcs_write(merged_vmcs, VMCS_ENTER_INTERRUPT_INFO, value);
	}

	/* Exception error code */
	{
		uint32_t value =
			(uint32_t)mon_vmcs_read(level1_vmcs,
				VMCS_ENTER_EXCEPTION_ERROR_CODE);
		mon_vmcs_write(merged_vmcs,
			VMCS_ENTER_EXCEPTION_ERROR_CODE,
			value);
	}

	/* Instruction length */
	{
		uint32_t value =
			(uint32_t)mon_vmcs_read(level1_vmcs,
				VMCS_ENTER_INSTRUCTION_LENGTH);
		mon_vmcs_write(merged_vmcs, VMCS_ENTER_INSTRUCTION_LENGTH,
			value);
	}

	/* TSC offset */
	{
		if (merged_controls.bits.use_tsc_offsetting) {
			uint64_t final_value = 0;

			if ((controls0.bits.use_tsc_offsetting == 1) &&
			    (controls1.bits.use_tsc_offsetting == 0)) {
				final_value = mon_vmcs_read(level0_vmcs,
					VMCS_TSC_OFFSET);
			} else if ((controls0.bits.use_tsc_offsetting == 0) &&
				   (controls1.bits.use_tsc_offsetting == 1)) {
				final_value = mon_vmcs_read(level1_vmcs,
					VMCS_TSC_OFFSET);
			} else {
				uint64_t value0 = mon_vmcs_read(level0_vmcs,
					VMCS_TSC_OFFSET);
				uint64_t value1 = mon_vmcs_read(level1_vmcs,
					VMCS_TSC_OFFSET);

				MON_ASSERT(
					controls0.bits.use_tsc_offsetting == 1);
				MON_ASSERT(
					controls1.bits.use_tsc_offsetting == 1);

				final_value = value0 + value1;
			}

			mon_vmcs_write(merged_vmcs, VMCS_TSC_OFFSET,
				final_value);
		}
	}

	/* APIC-access address */
	{
		if ((merged_controls.bits.secondary_controls == 1) &&
		    (merged_controls_2.bits.virtualize_apic == 1)) {
			/* TODO: Implement APIC-access merge */
			MON_DEADLOOP();
		}
	}

	/* TPR shadow address */
	{
		if (merged_controls.bits.tpr_shadow == 1) {
			/* TODO: Implement TPR-shadow merge */
			MON_DEADLOOP();
		}
	}

	/* "Page-fault error-code mask" and "Page-fault error-code match" */
	{
		ia32_vmcs_exception_bitmap_t exception_ctrls;

		exception_ctrls.uint32 =
			(uint32_t)gcpu_get_exceptions_map_layered(gcpu,
				VMCS_MERGED);

		if (may_cause_vmexit_on_page_fault(gcpu, VMCS_LEVEL_0) ||
		    may_cause_vmexit_on_page_fault(gcpu, VMCS_LEVEL_1)) {
			if (exception_ctrls.bits.pf == 1) {
				gcpu_set_pf_error_code_mask_and_match_layered(
					gcpu,
					VMCS_MERGED,
					0x00000000,
					0x00000000);
			} else {
				gcpu_set_pf_error_code_mask_and_match_layered(
					gcpu,
					VMCS_MERGED,
					0x00000000,
					0xffffffff);
			}
		} else {
			if (exception_ctrls.bits.pf == 1) {
				gcpu_set_pf_error_code_mask_and_match_layered(
					gcpu,
					VMCS_MERGED,
					0x00000000,
					0xffffffff);
			} else {
				gcpu_set_pf_error_code_mask_and_match_layered(
					gcpu,
					VMCS_MERGED,
					0x00000000,
					0x00000000);
			}
		}
	}

	/* CR3 target count */
	{
		/* Target list is not supported */
		mon_vmcs_write(merged_vmcs, VMCS_CR3_TARGET_COUNT, 0);
	}

	/* VM-exit controls */
	{
		vmexit_controls_t merged_exit_controls;

		merged_exit_controls.uint32 =
			(uint32_t)gcpu_get_exit_ctrls_layered(gcpu,
				VMCS_LEVEL_0);
		/* The only difference */
		merged_exit_controls.bits.acknowledge_interrupt_on_exit = 0;

		gcpu_set_exit_ctrls_layered(gcpu, VMCS_MERGED,
			merged_exit_controls.uint32);

		/* VTUNE is not supported */
		MON_ASSERT(
			merged_exit_controls.bits.load_ia32_perf_global_ctrl ==
			0);
	}

	/* Attention !!! ms_merge_timer_to_level2 must be called
	 * after all other control fields were already merged */
	if (vmcs_field_is_supported(VMCS_PREEMPTION_TIMER)) {
		ms_merge_timer_to_level2(level0_vmcs, level1_vmcs, merged_vmcs);
	}

	/* TPR threshold */
	{
		if (merged_controls.bits.tpr_shadow == 1) {
			/* TODO: Implement TPR-threshold merge */
			MON_DEADLOOP();
		}
	}

	/* CR0 guest/host mask */
	{
		uint64_t mask0 = gcpu_get_cr0_reg_mask_layered(gcpu,
			VMCS_LEVEL_0);
		uint64_t mask1 = gcpu_get_cr0_reg_mask_layered(gcpu,
			VMCS_LEVEL_1);
		uint64_t merged_mask = mask0 | mask1;

		gcpu_set_cr0_reg_mask_layered(gcpu, VMCS_MERGED, merged_mask);
	}

	/* CR4 guest/host mask */
	{
		uint64_t mask0 = gcpu_get_cr4_reg_mask_layered(gcpu,
			VMCS_LEVEL_0);
		uint64_t mask1 = gcpu_get_cr4_reg_mask_layered(gcpu,
			VMCS_LEVEL_1);
		uint64_t merged_mask = mask0 | mask1;

		gcpu_set_cr4_reg_mask_layered(gcpu, VMCS_MERGED, merged_mask);
	}

	/* CR0 shadow */
	{
		uint64_t shadow = ms_merge_cr_shadow(gcpu, IA32_CTRL_CR0);
		gcpu_set_guest_visible_control_reg_layered(gcpu,
			IA32_CTRL_CR0,
			shadow,
			VMCS_MERGED);
	}

	/* CR3 pseudo shadow */
	{
		uint64_t value =
			gcpu_get_control_reg_layered(gcpu,
				IA32_CTRL_CR3,
				VMCS_LEVEL_1);
		gcpu_set_guest_visible_control_reg_layered(gcpu,
			IA32_CTRL_CR3,
			value,
			VMCS_MERGED);
	}

	/* CR4 shadow */
	{
		uint64_t shadow = ms_merge_cr_shadow(gcpu, IA32_CTRL_CR4);
		gcpu_set_guest_visible_control_reg_layered(gcpu,
			IA32_CTRL_CR4,
			shadow,
			VMCS_MERGED);
	}

	/* I/O bitmaps A and B */
	{
		if (merged_controls.bits.activate_io_bitmaps == 1) {
			void *level0_bitmap_A;
			void *level0_bitmap_B;
			void *level1_bitmap_A;
			void *level1_bitmap_B;
			void *merged_bitmap_A;
			void *merged_bitmap_B;

			if (controls0.bits.activate_io_bitmaps == 1) {
				level0_bitmap_A =
					ms_retrieve_ptr_to_additional_memory(
						level0_vmcs,
						VMCS_IO_BITMAP_ADDRESS_A,
						MS_HVA);
				level0_bitmap_B =
					ms_retrieve_ptr_to_additional_memory(
						level0_vmcs,
						VMCS_IO_BITMAP_ADDRESS_B,
						MS_HVA);
			} else {
				level0_bitmap_A = NULL;
				level0_bitmap_B = NULL;
			}

			if (controls1.bits.activate_io_bitmaps == 1) {
				level1_bitmap_A =
					ms_retrieve_ptr_to_additional_memory(
						level1_vmcs,
						VMCS_IO_BITMAP_ADDRESS_A,
						MS_HVA);
				level1_bitmap_B =
					ms_retrieve_ptr_to_additional_memory(
						level1_vmcs,
						VMCS_IO_BITMAP_ADDRESS_B,
						MS_HVA);
			} else {
				level1_bitmap_A = NULL;
				level1_bitmap_B = NULL;
			}

			merged_bitmap_A =
				ms_retrieve_ptr_to_additional_memory(
					merged_vmcs,
					VMCS_IO_BITMAP_ADDRESS_A,
					MS_HPA);
			merged_bitmap_B =
				ms_retrieve_ptr_to_additional_memory(
					merged_vmcs,
					VMCS_IO_BITMAP_ADDRESS_B,
					MS_HPA);

			ms_merge_bitmaps(level0_bitmap_A,
				level1_bitmap_A,
				merged_bitmap_A);
			ms_merge_bitmaps(level0_bitmap_B,
				level1_bitmap_B,
				merged_bitmap_B);
		}
	}

	/* MSR bitmap */
	{
		if (merged_controls.bits.use_msr_bitmaps == 1) {
			void *level0_bitmap;
			void *level1_bitmap;
			void *merged_bitmap;

			level0_bitmap =
				ms_retrieve_ptr_to_additional_memory(
					level0_vmcs,
					VMCS_MSR_BITMAP_ADDRESS,
					MS_HVA);
			level1_bitmap =
				ms_retrieve_ptr_to_additional_memory(
					level1_vmcs,
					VMCS_MSR_BITMAP_ADDRESS,
					MS_HVA);
			merged_bitmap =
				ms_retrieve_ptr_to_additional_memory(
					merged_vmcs,
					VMCS_MSR_BITMAP_ADDRESS,
					MS_HPA);

			ms_merge_bitmaps(level0_bitmap,
				level1_bitmap,
				merged_bitmap);
		}
	}

	/* VMExit MSR-store address and count */
	{
		ia32_vmx_msr_entry_t *level0_list =
			ms_retrieve_ptr_to_additional_memory(level0_vmcs,
				VMCS_EXIT_MSR_STORE_ADDRESS,
				MS_HVA);
		uint32_t level0_list_count =
			(uint32_t)mon_vmcs_read(level0_vmcs,
				VMCS_EXIT_MSR_STORE_COUNT);
		ia32_vmx_msr_entry_t *level1_list =
			ms_retrieve_ptr_to_additional_memory(level1_vmcs,
				VMCS_EXIT_MSR_STORE_ADDRESS,
				MS_HVA);
		uint32_t level1_list_count =
			(uint32_t)mon_vmcs_read(level1_vmcs,
				VMCS_EXIT_MSR_STORE_COUNT);

		if ((level0_list_count + level1_list_count) > 256) {
			/* TODO: proper handling of VMExit MSR-store list when it must be >
			 * 512 entries */
			MON_DEADLOOP();
		}

		ms_merge_msr_list(gcpu, merged_vmcs, level1_list, level0_list,
			level1_list_count, level0_list_count,
			MSR_LIST_COPY_NO_CHANGE,
			vmcs_add_msr_to_vmexit_store_list,
			vmcs_clear_vmexit_store_list,
			vmcs_is_msr_in_vmexit_store_list,
			VMCS_EXIT_MSR_STORE_ADDRESS,
			VMCS_EXIT_MSR_STORE_COUNT);
	}

	/* VMExit MSR-load address and count */
	{
		ia32_vmx_msr_entry_t *level0_list =
			ms_retrieve_ptr_to_additional_memory(level0_vmcs,
				VMCS_EXIT_MSR_LOAD_ADDRESS,
				MS_HVA);
		uint32_t level0_list_count =
			(uint32_t)mon_vmcs_read(level0_vmcs,
				VMCS_EXIT_MSR_LOAD_COUNT);

		if (level0_list_count > 256) {
			/* TODO: proper handling of VMExit MSR-load list when it must be >
			 * 512 entries */
			MON_DEADLOOP();
		}

		ms_merge_msr_list(gcpu, merged_vmcs, level0_list, NULL,
			level0_list_count, 0, MSR_LIST_COPY_NO_CHANGE,
			vmcs_add_msr_to_vmexit_load_list,
			vmcs_clear_vmexit_load_list,
			vmcs_is_msr_in_vmexit_load_list,
			VMCS_EXIT_MSR_LOAD_ADDRESS, VMCS_EXIT_MSR_LOAD_COUNT);
	}

	/* VMEnter MSR-load address and count */
	{
		ia32_vmx_msr_entry_t *level0_list =
			ms_retrieve_ptr_to_additional_memory(level0_vmcs,
				VMCS_ENTER_MSR_LOAD_ADDRESS,
				MS_HVA);
		uint32_t level0_list_count =
			(uint32_t)mon_vmcs_read(level0_vmcs,
				VMCS_ENTER_MSR_LOAD_COUNT);
		ia32_vmx_msr_entry_t *level1_list =
			ms_retrieve_ptr_to_additional_memory(level1_vmcs,
				VMCS_ENTER_MSR_LOAD_ADDRESS,
				MS_HVA);
		uint32_t level1_list_count =
			(uint32_t)mon_vmcs_read(level1_vmcs,
				VMCS_ENTER_MSR_LOAD_COUNT);
		vmentry_controls_t entry_ctrls;
		msr_list_copy_mode_t copy_mode;

		if ((level0_list_count + level1_list_count) > 512) {
			/* TODO: proper handling of VMEnter MSR-load list when it must be >
			 * 512 entries */
			MON_DEADLOOP();
		}

		entry_ctrls.uint32 =
			(uint32_t)gcpu_get_enter_ctrls_layered(gcpu,
				VMCS_MERGED);
		if (entry_ctrls.bits.ia32e_mode_guest) {
			copy_mode =
				MSR_LIST_COPY_AND_SET_64_BIT_MODE_IN_EFER |
				MSR_LIST_COPY_UPDATE_GCPU;
		} else {
			copy_mode =
				MSR_LIST_COPY_AND_SET_32_BIT_MODE_IN_EFER |
				MSR_LIST_COPY_UPDATE_GCPU;
		}

		ms_merge_msr_list(gcpu,
			merged_vmcs,
			level1_list,
			level0_list,
			level1_list_count,
			level0_list_count,
			copy_mode,
			vmcs_add_msr_to_vmenter_load_list,
			vmcs_clear_vmenter_load_list,
			vmcs_is_msr_in_vmenter_load_list,
			VMCS_ENTER_MSR_LOAD_ADDRESS,
			VMCS_ENTER_MSR_LOAD_COUNT);
	}

	/* ------------------HOST_STATE------------------ */

	/* Copy host state from level-0 vmcs */
	ms_copy_host_state(merged_vmcs, level0_vmcs);
}

void ms_split_from_level2(IN guest_cpu_handle_t gcpu)
{
	vmcs_hierarchy_t *hierarchy = gcpu_get_vmcs_hierarchy(gcpu);
	vmcs_object_t *level1_vmcs = vmcs_hierarchy_get_vmcs(hierarchy,
		VMCS_LEVEL_1);
	vmcs_object_t *merged_vmcs = vmcs_hierarchy_get_vmcs(hierarchy,
		VMCS_MERGED);

	/* ---UPDATE MSR LISTS IN LEVEL0 and LEVEL1 VMCSs--- */
	{
		ia32_vmx_msr_entry_t *merged_list =
			ms_retrieve_ptr_to_additional_memory(merged_vmcs,
				VMCS_EXIT_MSR_STORE_ADDRESS,
				MS_HPA);
		uint32_t merged_list_count =
			(uint32_t)mon_vmcs_read(merged_vmcs,
				VMCS_EXIT_MSR_STORE_COUNT);

		ms_split_msr_lists(gcpu, merged_list, merged_list_count);
	}

	/* -----------GUEST_STATE OF LEVEL1 VMCS----------- */
	/* Copy guest state from level-1 vmcs */
	ms_copy_guest_state_to_level1_vmcs(gcpu, FALSE /* do not copy CRs */);

	/* CR3 - actual CR3 is stored as "visible" CR3 */
	{
		uint64_t value =
			gcpu_get_guest_visible_control_reg_layered(gcpu,
				IA32_CTRL_CR3,
				VMCS_MERGED);
		gcpu_set_control_reg_layered(gcpu,
			IA32_CTRL_CR3,
			value,
			VMCS_LEVEL_1);
	}

	/* CR0/CR4 update */
	ms_perform_cr_split(gcpu, IA32_CTRL_CR0);
	ms_perform_cr_split(gcpu, IA32_CTRL_CR4);

	/* -----------DATA FIELDS OF LEVEL1 VMCS----------- */
	ms_copy_data_fields(level1_vmcs, merged_vmcs);

	if (vmcs_field_is_supported(VMCS_PREEMPTION_TIMER)) {
		ms_split_timer_from_level2(vmcs_hierarchy_get_vmcs
				(hierarchy, VMCS_LEVEL_1), level1_vmcs,
			merged_vmcs);
	}
}

void ms_merge_to_level1(IN guest_cpu_handle_t gcpu,
			IN boolean_t was_vmexit_from_level1,
			IN boolean_t merge_only_dirty UNUSED)
{
	/* TODO: merge only dirty */
	vmcs_hierarchy_t *hierarchy = gcpu_get_vmcs_hierarchy(gcpu);
	vmcs_object_t *level0_vmcs = vmcs_hierarchy_get_vmcs(hierarchy,
		VMCS_LEVEL_0);
	vmcs_object_t *level1_vmcs = vmcs_hierarchy_get_vmcs(hierarchy,
		VMCS_LEVEL_1);
	vmcs_object_t *merged_vmcs = vmcs_hierarchy_get_vmcs(hierarchy,
		VMCS_MERGED);

	/* -----------------GUEST_STATE------------------- */

	if (!was_vmexit_from_level1) {
		/* (level-2) --> (level-1) vmexit, copy host area of level-1 vmcs to
		 * guest area of merged vmcs */
		vmexit_controls_t exit_ctrls;

		/* merged exit controls will be identical to "level-0" exit controls */
		exit_ctrls.uint32 =
			(uint32_t)gcpu_get_exit_ctrls_layered(gcpu,
				VMCS_LEVEL_0);

		/* ES segment */
		{
			ia32_selector_t selector;
			ia32_vmx_vmcs_guest_ar_t ar;

			selector.sel16 =
				(uint16_t)mon_vmcs_read(level1_vmcs,
					VMCS_HOST_ES_SELECTOR);

			ar.uint32 = 0;
			ar.bits.segment_type = 1;
			ar.bits.descriptor_privilege_level = 0;
			ar.bits.segment_present = 1;
			ar.bits.default_operation_size =
				exit_ctrls.bits.ia32e_mode_host ? 0 : 1;
			ar.bits.granularity = 1;
			/* unused in case when selector is 0 */
			ar.bits.null = (selector.bits.index == 0) ? 1 : 0;

			gcpu_set_segment_reg_layered(gcpu,
				IA32_SEG_ES,
				selector.sel16,
				0,
				0xffffffff,
				ar.uint32,
				VMCS_MERGED);
		}

		/* CS segment */
		{
			ia32_selector_t selector;
			ia32_vmx_vmcs_guest_ar_t ar;

			selector.sel16 =
				(uint16_t)mon_vmcs_read(level1_vmcs,
					VMCS_HOST_CS_SELECTOR);

			ar.uint32 = 0;
			ar.bits.segment_type = 11;
			ar.bits.descriptor_type = 1;
			ar.bits.descriptor_privilege_level = 0;
			ar.bits.segment_present = 1;
			ar.bits.reserved_1 =
				exit_ctrls.bits.ia32e_mode_host ? 1 : 0;
			ar.bits.default_operation_size =
				exit_ctrls.bits.ia32e_mode_host ? 0 : 1;
			ar.bits.granularity = 1;
			/* usable */
			ar.bits.null = 0;

			gcpu_set_segment_reg_layered(gcpu,
				IA32_SEG_CS,
				selector.sel16,
				0,
				0xffffffff,
				ar.uint32,
				VMCS_MERGED);
		}

		/* SS segment */
		{
			ia32_selector_t selector;
			ia32_vmx_vmcs_guest_ar_t ar;

			selector.sel16 =
				(uint16_t)mon_vmcs_read(level1_vmcs,
					VMCS_HOST_SS_SELECTOR);

			ar.uint32 = 0;
			ar.bits.segment_type = 1;
			ar.bits.descriptor_privilege_level = 0;
			ar.bits.segment_present = 1;
			ar.bits.default_operation_size =
				exit_ctrls.bits.ia32e_mode_host ? 0 : 1;
			/* unusable in case the index is 0 */
			ar.bits.null = (selector.bits.index == 0) ? 1 : 0;

			gcpu_set_segment_reg_layered(gcpu,
				IA32_SEG_SS,
				selector.sel16,
				0,
				0xffffffff,
				ar.uint32,
				VMCS_MERGED);
		}

		/* DS segment */
		{
			ia32_selector_t selector;
			ia32_vmx_vmcs_guest_ar_t ar;

			selector.sel16 =
				(uint16_t)mon_vmcs_read(level1_vmcs,
					VMCS_HOST_DS_SELECTOR);

			ar.uint32 = 0;
			ar.bits.segment_type = 1;
			ar.bits.descriptor_privilege_level = 0;
			ar.bits.segment_present = 1;
			ar.bits.default_operation_size =
				exit_ctrls.bits.ia32e_mode_host ? 0 : 1;
			ar.bits.granularity = 1;
			/* unusable in case the index is 0 */
			ar.bits.null = (selector.bits.index == 0) ? 1 : 0;

			gcpu_set_segment_reg_layered(gcpu,
				IA32_SEG_DS,
				selector.sel16,
				0,
				0xffffffff,
				ar.uint32,
				VMCS_MERGED);
		}

		/* FS segment */
		{
			ia32_selector_t selector;
			uint64_t base = mon_vmcs_read(level1_vmcs,
				VMCS_HOST_FS_BASE);
			ia32_vmx_vmcs_guest_ar_t ar;

			selector.sel16 =
				(uint16_t)mon_vmcs_read(level1_vmcs,
					VMCS_HOST_FS_SELECTOR);

			ar.uint32 = 0;
			ar.bits.segment_type = 1;
			ar.bits.descriptor_privilege_level = 0;
			ar.bits.segment_present = 1;
			ar.bits.default_operation_size =
				exit_ctrls.bits.ia32e_mode_host ? 0 : 1;
			ar.bits.granularity = 1;
			/* unusable in case the index is 0 */
			ar.bits.null = (selector.bits.index == 0) ? 1 : 0;

			gcpu_set_segment_reg_layered(gcpu,
				IA32_SEG_FS,
				selector.sel16,
				base,
				0xffffffff,
				ar.uint32,
				VMCS_MERGED);
		}

		/* GS segment */
		{
			ia32_selector_t selector;
			uint64_t base = mon_vmcs_read(level1_vmcs,
				VMCS_HOST_GS_BASE);
			ia32_vmx_vmcs_guest_ar_t ar;

			selector.sel16 =
				(uint16_t)mon_vmcs_read(level1_vmcs,
					VMCS_HOST_GS_SELECTOR);

			ar.uint32 = 0;
			ar.bits.segment_type = 1;
			ar.bits.descriptor_privilege_level = 0;
			ar.bits.segment_present = 1;
			ar.bits.default_operation_size =
				exit_ctrls.bits.ia32e_mode_host ? 0 : 1;
			ar.bits.granularity = 1;
			/* unusable in case the index is 0 */
			ar.bits.null = (selector.bits.index == 0) ? 1 : 0;

			gcpu_set_segment_reg_layered(gcpu,
				IA32_SEG_GS,
				selector.sel16,
				base,
				0xffffffff,
				ar.uint32,
				VMCS_MERGED);
		}

		/* TR segment */
		{
			ia32_selector_t selector;
			uint64_t base = mon_vmcs_read(level1_vmcs,
				VMCS_HOST_TR_BASE);
			ia32_vmx_vmcs_guest_ar_t ar;

			selector.sel16 =
				(uint16_t)mon_vmcs_read(level1_vmcs,
					VMCS_HOST_TR_SELECTOR);

			ar.uint32 = 0;
			ar.bits.segment_type = 11;
			ar.bits.descriptor_type = 0;
			ar.bits.descriptor_privilege_level = 0;
			ar.bits.segment_present = 1;
			ar.bits.default_operation_size = 0;
			ar.bits.granularity = 0;
			/* usable */
			ar.bits.null = 0;

			gcpu_set_segment_reg_layered(gcpu,
				IA32_SEG_TR,
				selector.sel16,
				base,
				0x67,
				ar.uint32,
				VMCS_MERGED);
		}

		/* LDTR */
		{
			ia32_vmx_vmcs_guest_ar_t ar;
			ar.uint32 = 0;
			/* unusable */
			ar.bits.null = 1;

			gcpu_set_segment_reg_layered(gcpu,
				IA32_SEG_LDTR,
				0,
				0,
				0,
				ar.uint32,
				VMCS_MERGED);
		}

		/* GDTR IDTR */
		{
			uint64_t base;

			base = mon_vmcs_read(level1_vmcs, VMCS_HOST_GDTR_BASE);
			gcpu_set_gdt_reg_layered(gcpu, base, 0xffff,
				VMCS_MERGED);

			base = mon_vmcs_read(level1_vmcs, VMCS_HOST_IDTR_BASE);
			gcpu_set_idt_reg_layered(gcpu, base, 0xffff,
				VMCS_MERGED);
		}

		/* RFLAGS */
		{
			gcpu_set_native_gp_reg_layered(gcpu,
				IA32_REG_RFLAGS,
				0x2,
				VMCS_MERGED);
		}

		/* RSP, RIP */
		{
			uint64_t value;

			value = mon_vmcs_read(level1_vmcs, VMCS_HOST_RIP);
			gcpu_set_native_gp_reg_layered(gcpu,
				IA32_REG_RIP,
				value,
				VMCS_MERGED);

			value = mon_vmcs_read(level1_vmcs, VMCS_HOST_RSP);
			gcpu_set_native_gp_reg_layered(gcpu,
				IA32_REG_RSP,
				value,
				VMCS_MERGED);
		}

		/* SYSENTER_CS, SYSENTER_ESP, SYSENTER_EIP */
		{
			uint64_t value;

			value =
				mon_vmcs_read(level1_vmcs,
					VMCS_HOST_SYSENTER_CS);
			gcpu_set_msr_reg_layered(gcpu,
				IA32_MON_MSR_SYSENTER_CS,
				value,
				VMCS_MERGED);

			value = mon_vmcs_read(level1_vmcs,
				VMCS_HOST_SYSENTER_ESP);
			gcpu_set_msr_reg_layered(gcpu,
				IA32_MON_MSR_SYSENTER_ESP,
				value,
				VMCS_MERGED);

			value = mon_vmcs_read(level1_vmcs,
				VMCS_HOST_SYSENTER_EIP);
			gcpu_set_msr_reg_layered(gcpu,
				IA32_MON_MSR_SYSENTER_EIP,
				value,
				VMCS_MERGED);
		}

		/* dr7 */
		{
			gcpu_set_debug_reg_layered(gcpu,
				IA32_REG_DR7,
				0x400,
				VMCS_MERGED);
		}

		/* IA32_PERF_GLOBAL_CTRL */
		if (vmcs_field_is_supported(VMCS_HOST_IA32_PERF_GLOBAL_CTRL) &&
		    vmcs_field_is_supported(VMCS_GUEST_IA32_PERF_GLOBAL_CTRL)) {
			uint64_t value;

			value = mon_vmcs_read(level1_vmcs,
				VMCS_HOST_IA32_PERF_GLOBAL_CTRL);
			mon_vmcs_write(merged_vmcs,
				VMCS_GUEST_IA32_PERF_GLOBAL_CTRL,
				value);
		}

		/* SMBASE */
		{
			gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_SMBASE, 0,
				VMCS_MERGED);
		}

		/* VMCS link pointer */
		{
			mon_vmcs_write(merged_vmcs,
				VMCS_OSV_CONTROLLING_VMCS_ADDRESS,
				~((uint64_t)0));
		}

		/* CR0, CR3, CR4 */
		{
			uint64_t value;

			value = mon_vmcs_read(level1_vmcs, VMCS_HOST_CR0);
			gcpu_set_control_reg_layered(gcpu, IA32_CTRL_CR0, value,
				VMCS_MERGED);
			gcpu_set_guest_visible_control_reg_layered(gcpu,
				IA32_CTRL_CR0,
				value,
				VMCS_MERGED);

			value = mon_vmcs_read(level1_vmcs, VMCS_HOST_CR3);
			gcpu_set_control_reg_layered(gcpu, IA32_CTRL_CR3, value,
				VMCS_MERGED);
			gcpu_set_guest_visible_control_reg_layered(gcpu,
				IA32_CTRL_CR3,
				value,
				VMCS_MERGED);

			value = mon_vmcs_read(level1_vmcs, VMCS_HOST_CR4);
			gcpu_set_control_reg_layered(gcpu, IA32_CTRL_CR4, value,
				VMCS_MERGED);
			gcpu_set_guest_visible_control_reg_layered(gcpu,
				IA32_CTRL_CR4,
				value,
				VMCS_MERGED);
		}

		/* Interruptibility state */
		{
			ia32_vmx_vmcs_guest_interruptibility_t interruptibility;
			ia32_vmx_exit_reason_t reason;

			interruptibility.uint32 = 0;
			reason.uint32 = (uint32_t)mon_vmcs_read(level1_vmcs,
				VMCS_EXIT_INFO_REASON);
			if (reason.bits.basic_reason ==
			    IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI) {
				ia32_vmx_vmcs_vmexit_info_idt_vectoring_t
					vectoring_info;

				vectoring_info.uint32 = (uint32_t)mon_vmcs_read(
					level1_vmcs,
					VMCS_EXIT_INFO_EXCEPTION_INFO);
				if (vectoring_info.bits.interrupt_type == 2) {
					/* NMI */
					interruptibility.bits.block_nmi = 1;
				}
			}
			gcpu_set_interruptibility_state_layered(gcpu,
				interruptibility.uint32,
				VMCS_MERGED);
		}

		/* Activity state */
		{
			gcpu_set_activity_state_layered(gcpu,
				IA32_VMX_VMCS_GUEST_SLEEP_STATE_ACTIVE,
				VMCS_MERGED);
		}

		/* IA32_DEBUGCTL */
		{
			gcpu_set_msr_reg_layered(gcpu, IA32_MON_MSR_DEBUGCTL, 0,
				VMCS_MERGED);
		}

		/* Pending debug exceptions */
		{
			gcpu_set_pending_debug_exceptions_layered(gcpu,
				0,
				VMCS_MERGED);
		}

		/* Preemption Timer */
		mon_vmcs_write(merged_vmcs,
			VMCS_PREEMPTION_TIMER,
			mon_vmcs_read(level0_vmcs, VMCS_PREEMPTION_TIMER));
	}

	/* -------------------CONTROLS------------------- */

	/* Most part is copied from level-0 */
	{
		uint64_t value;
		uint32_t pf_mask;
		uint32_t pf_match;

		value = gcpu_get_pin_ctrls_layered(gcpu, VMCS_LEVEL_0);
		gcpu_set_pin_ctrls_layered(gcpu, VMCS_MERGED, value);

		value = gcpu_get_exceptions_map_layered(gcpu, VMCS_LEVEL_0);
		gcpu_set_exceptions_map_layered(gcpu, VMCS_MERGED, value);

		value = gcpu_get_processor_ctrls_layered(gcpu, VMCS_LEVEL_0);
		gcpu_set_processor_ctrls_layered(gcpu, VMCS_MERGED, value);

		value = gcpu_get_processor_ctrls2_layered(gcpu, VMCS_LEVEL_0);
		gcpu_set_processor_ctrls2_layered(gcpu, VMCS_MERGED, value);

		value = gcpu_get_enter_ctrls_layered(gcpu, VMCS_LEVEL_0);
		gcpu_set_enter_ctrls_layered(gcpu, VMCS_MERGED,
			(uint32_t)value);
#ifdef DEBUG
		{
			vmentry_controls_t controls;
			controls.uint32 = (uint32_t)value;

			/* VTUNE is not supported */
			MON_ASSERT(controls.bits.load_ia32_perf_global_ctrl ==
				0);
		}
#endif

		value = gcpu_get_exit_ctrls_layered(gcpu, VMCS_LEVEL_0);
		gcpu_set_exit_ctrls_layered(gcpu, VMCS_MERGED, (uint32_t)value);
#ifdef DEBUG
		{
			vmexit_controls_t controls;
			controls.uint32 = (uint32_t)value;

			/* VTUNE is not supported */
			MON_ASSERT(controls.bits.load_ia32_perf_global_ctrl ==
				0);
		}
#endif

		value = gcpu_get_cr0_reg_mask_layered(gcpu, VMCS_LEVEL_0);
		gcpu_set_cr0_reg_mask_layered(gcpu, VMCS_MERGED, value);

		value = gcpu_get_cr4_reg_mask_layered(gcpu, VMCS_LEVEL_0);
		gcpu_set_cr4_reg_mask_layered(gcpu, VMCS_MERGED, value);

		value = mon_vmcs_read(level0_vmcs,
			VMCS_OSV_CONTROLLING_VMCS_ADDRESS);
		mon_vmcs_write(merged_vmcs,
			VMCS_OSV_CONTROLLING_VMCS_ADDRESS,
			value);

		value = mon_vmcs_read(level0_vmcs, VMCS_ENTER_INTERRUPT_INFO);
		mon_vmcs_write(merged_vmcs, VMCS_ENTER_INTERRUPT_INFO, value);

		value = mon_vmcs_read(level0_vmcs,
			VMCS_ENTER_EXCEPTION_ERROR_CODE);
		mon_vmcs_write(merged_vmcs,
			VMCS_ENTER_EXCEPTION_ERROR_CODE,
			value);

		value =
			mon_vmcs_read(level0_vmcs,
				VMCS_ENTER_INSTRUCTION_LENGTH);
		mon_vmcs_write(merged_vmcs, VMCS_ENTER_INSTRUCTION_LENGTH,
			value);

		value = mon_vmcs_read(level0_vmcs, VMCS_TSC_OFFSET);
		mon_vmcs_write(merged_vmcs, VMCS_TSC_OFFSET, value);

		value = mon_vmcs_read(level0_vmcs, VMCS_APIC_ACCESS_ADDRESS);
		mon_vmcs_write(merged_vmcs, VMCS_APIC_ACCESS_ADDRESS, value);

		value = mon_vmcs_read(level0_vmcs, VMCS_VIRTUAL_APIC_ADDRESS);
		mon_vmcs_write(merged_vmcs, VMCS_VIRTUAL_APIC_ADDRESS, value);

		gcpu_get_pf_error_code_mask_and_match_layered(gcpu,
			VMCS_LEVEL_0,
			&pf_mask,
			&pf_match);
		gcpu_set_pf_error_code_mask_and_match_layered(gcpu, VMCS_MERGED,
			pf_mask, pf_match);

		value = mon_vmcs_read(level0_vmcs, VMCS_CR3_TARGET_COUNT);
		mon_vmcs_write(merged_vmcs, VMCS_CR3_TARGET_COUNT, value);

		value = mon_vmcs_read(level0_vmcs, VMCS_CR3_TARGET_VALUE_0);
		mon_vmcs_write(merged_vmcs, VMCS_CR3_TARGET_VALUE_0, value);

		value = mon_vmcs_read(level0_vmcs, VMCS_CR3_TARGET_VALUE_1);
		mon_vmcs_write(merged_vmcs, VMCS_CR3_TARGET_VALUE_1, value);

		value = mon_vmcs_read(level0_vmcs, VMCS_CR3_TARGET_VALUE_2);
		mon_vmcs_write(merged_vmcs, VMCS_CR3_TARGET_VALUE_2, value);

		value = mon_vmcs_read(level0_vmcs, VMCS_CR3_TARGET_VALUE_3);
		mon_vmcs_write(merged_vmcs, VMCS_CR3_TARGET_VALUE_3, value);

		value = mon_vmcs_read(level0_vmcs, VMCS_EXIT_TPR_THRESHOLD);
		mon_vmcs_write(merged_vmcs, VMCS_EXIT_TPR_THRESHOLD, value);

		value = gcpu_get_guest_visible_control_reg_layered(gcpu,
			IA32_CTRL_CR0,
			VMCS_LEVEL_0);
		gcpu_set_guest_visible_control_reg_layered(gcpu,
			IA32_CTRL_CR0,
			value,
			VMCS_MERGED);

		value = gcpu_get_guest_visible_control_reg_layered(gcpu,
			IA32_CTRL_CR4,
			VMCS_LEVEL_0);
		gcpu_set_guest_visible_control_reg_layered(gcpu,
			IA32_CTRL_CR4,
			value,
			VMCS_MERGED);
	}

	/* I/O bitmaps A and B */
	{
		processor_based_vm_execution_controls_t merged_controls;

		merged_controls.uint32 =
			(uint32_t)gcpu_get_processor_ctrls_layered(gcpu,
				VMCS_LEVEL_0);
		if (merged_controls.bits.activate_io_bitmaps == 1) {
			void *level0_bitmap_A;
			void *level0_bitmap_B;
			void *merged_bitmap_A;
			void *merged_bitmap_B;

			level0_bitmap_A =
				ms_retrieve_ptr_to_additional_memory(
					level0_vmcs,
					VMCS_IO_BITMAP_ADDRESS_A,
					MS_HVA);
			level0_bitmap_B =
				ms_retrieve_ptr_to_additional_memory(
					level0_vmcs,
					VMCS_IO_BITMAP_ADDRESS_B,
					MS_HVA);

			MON_ASSERT(level0_bitmap_A != NULL);
			MON_ASSERT(level0_bitmap_B != NULL);

			merged_bitmap_A =
				ms_retrieve_ptr_to_additional_memory(
					merged_vmcs,
					VMCS_IO_BITMAP_ADDRESS_A,
					MS_HPA);
			merged_bitmap_B =
				ms_retrieve_ptr_to_additional_memory(
					merged_vmcs,
					VMCS_IO_BITMAP_ADDRESS_B,
					MS_HPA);

			MON_ASSERT(merged_bitmap_A != NULL);
			MON_ASSERT(merged_bitmap_B != NULL);

			ms_merge_bitmaps(level0_bitmap_A, NULL,
				merged_bitmap_A);
			ms_merge_bitmaps(level0_bitmap_B, NULL,
				merged_bitmap_B);
		}
	}

	/* MSR bitmap */
	{
		processor_based_vm_execution_controls_t merged_controls;

		merged_controls.uint32 =
			(uint32_t)gcpu_get_processor_ctrls_layered(gcpu,
				VMCS_LEVEL_0);

		if (merged_controls.bits.use_msr_bitmaps == 1) {
			void *level0_bitmap;
			void *merged_bitmap;

			level0_bitmap =
				ms_retrieve_ptr_to_additional_memory(
					level0_vmcs,
					VMCS_MSR_BITMAP_ADDRESS,
					MS_HVA);
			merged_bitmap =
				ms_retrieve_ptr_to_additional_memory(
					merged_vmcs,
					VMCS_MSR_BITMAP_ADDRESS,
					MS_HPA);

			ms_merge_bitmaps(level0_bitmap, NULL, merged_bitmap);
		}
	}

	/* VMExit MSR-store address and count */
	{
		ia32_vmx_msr_entry_t *level0_list =
			ms_retrieve_ptr_to_additional_memory(level0_vmcs,
				VMCS_EXIT_MSR_STORE_ADDRESS,
				MS_HVA);
		uint32_t level0_list_count =
			(uint32_t)mon_vmcs_read(level0_vmcs,
				VMCS_EXIT_MSR_STORE_COUNT);

		if (level0_list_count > 256) {
			/* TODO: proper handling */
			MON_DEADLOOP();
		}

		ms_merge_msr_list(gcpu,
			merged_vmcs,
			level0_list,
			NULL,
			level0_list_count,
			0,
			MSR_LIST_COPY_NO_CHANGE,
			vmcs_add_msr_to_vmexit_store_list,
			vmcs_clear_vmexit_store_list,
			vmcs_is_msr_in_vmexit_store_list,
			VMCS_EXIT_MSR_STORE_ADDRESS,
			VMCS_EXIT_MSR_STORE_COUNT);
	}

	/* VMExit MSR-load address and count */
	{
		ia32_vmx_msr_entry_t *level0_list =
			ms_retrieve_ptr_to_additional_memory(level0_vmcs,
				VMCS_EXIT_MSR_LOAD_ADDRESS,
				MS_HVA);
		uint32_t level0_list_count =
			(uint32_t)mon_vmcs_read(level0_vmcs,
				VMCS_EXIT_MSR_LOAD_COUNT);

		if (level0_list_count > 256) {
			/* TODO: proper handling */
			MON_DEADLOOP();
		}

		ms_merge_msr_list(gcpu,
			merged_vmcs,
			level0_list,
			NULL,
			level0_list_count,
			0,
			MSR_LIST_COPY_NO_CHANGE,
			vmcs_add_msr_to_vmexit_load_list,
			vmcs_clear_vmexit_load_list,
			vmcs_is_msr_in_vmexit_load_list,
			VMCS_EXIT_MSR_LOAD_ADDRESS, VMCS_EXIT_MSR_LOAD_COUNT);
	}

	/* VMEnter MSR-load address and count */
	{
		ia32_vmx_msr_entry_t *level0_list =
			ms_retrieve_ptr_to_additional_memory(level0_vmcs,
				VMCS_ENTER_MSR_LOAD_ADDRESS,
				MS_HVA);
		uint32_t level0_list_count =
			(uint32_t)mon_vmcs_read(level0_vmcs,
				VMCS_ENTER_MSR_LOAD_COUNT);
		ia32_vmx_msr_entry_t *level1_list =
			ms_retrieve_ptr_to_additional_memory(level1_vmcs,
				VMCS_EXIT_MSR_LOAD_ADDRESS,
				MS_HVA);
		uint32_t level1_list_count =
			(uint32_t)mon_vmcs_read(level1_vmcs,
				VMCS_EXIT_MSR_LOAD_COUNT);
		vmentry_controls_t entry_ctrls;
		msr_list_copy_mode_t copy_mode;

		if ((level0_list_count + level1_list_count) > 256) {
			/* TODO: proper handling */
			MON_DEADLOOP();
		}

		entry_ctrls.uint32 =
			(uint32_t)gcpu_get_enter_ctrls_layered(gcpu,
				VMCS_MERGED);
		if (entry_ctrls.bits.ia32e_mode_guest) {
			copy_mode = MSR_LIST_COPY_AND_SET_64_BIT_MODE_IN_EFER |
				    MSR_LIST_COPY_UPDATE_GCPU;
		} else {
			copy_mode = MSR_LIST_COPY_AND_SET_32_BIT_MODE_IN_EFER |
				    MSR_LIST_COPY_UPDATE_GCPU;
		}

		ms_merge_msr_list(gcpu,
			merged_vmcs,
			level1_list,
			level0_list,
			level1_list_count,
			level0_list_count,
			copy_mode,
			vmcs_add_msr_to_vmenter_load_list,
			vmcs_clear_vmenter_load_list,
			vmcs_is_msr_in_vmenter_load_list,
			VMCS_ENTER_MSR_LOAD_ADDRESS,
			VMCS_ENTER_MSR_LOAD_COUNT);
	}

	/* ------------------HOST_STATE------------------ */

	/* Copy host state from level-0 vmcs */
	ms_copy_host_state(merged_vmcs, level0_vmcs);
}

/*
 * Merge Algorithm:
 * ---------------
 * If VMCS#1.Timer-Enabled == FALSE ==> copy from VMCS#0
 * else if VMCS#0.Timer-Enabled == FALSE ==> copy from VMCS#1
 * else do real-merge:
 *     Save-Value = 1 Enable=1 Counter = Minimum of 2
 *
 * Split Algorithm:
 * ---------------
 * Control information is not split
 * if Save-Value = 0 Counter not changed
 * else
 *     if (Counter[i] < Counter[1-i]) Counter[i] = Counter[m]
 *     else Counter[i] = Counter[m] + Counter[i] - Counter[1-i]
 *
 * VMEXIT-request Analysis Algorithm: (implemented in other file)
 * ---------------------------------
 * if Save-Value == 0 VMEXIT-requested = TRUE;
 * else if (counter#0 == counter#1)VMEXIT-requested = TRUE;
 * else VMEXIT-requested = FALSE; */
void ms_merge_timer_to_level2(vmcs_object_t *vmcs_0, vmcs_object_t *vmcs_1,
			      vmcs_object_t *vmcs_m)
{
	pin_based_vm_execution_controls_t merged_pin_exec;
	vmexit_controls_t merged_vmexit_ctrls;
	uint32_t merged_counter_value;
	pin_based_vm_execution_controls_t pin_exec[2];
	uint32_t counter_value[2];

	pin_exec[0].uint32 = (uint32_t)mon_vmcs_read(vmcs_0,
		VMCS_CONTROL_VECTOR_PIN_EVENTS);
	pin_exec[1].uint32 = (uint32_t)mon_vmcs_read(vmcs_1,
		VMCS_CONTROL_VECTOR_PIN_EVENTS);
	merged_pin_exec.uint32 = (uint32_t)mon_vmcs_read(vmcs_m,
		VMCS_CONTROL_VECTOR_PIN_EVENTS);
	merged_vmexit_ctrls.uint32 = (uint32_t)mon_vmcs_read(vmcs_m,
		VMCS_EXIT_CONTROL_VECTOR);

	merged_pin_exec.bits.vmx_timer = pin_exec[0].bits.vmx_timer
					 || pin_exec[1].bits.vmx_timer;

	if (0 == merged_pin_exec.bits.vmx_timer) {
		/* VMX Timer disabled */
		merged_vmexit_ctrls.bits.save_vmx_timer = 0;
		merged_counter_value = 0;
	} else {
		vmexit_controls_t vmexit_ctrls;

		/* VMX Timer enabled at least in one VMCS */
		if (0 == pin_exec[1].bits.vmx_timer) {
			/* copy from vmcs#0 */
			vmexit_ctrls.uint32 = (uint32_t)mon_vmcs_read(vmcs_0,
				VMCS_EXIT_CONTROL_VECTOR);
			merged_vmexit_ctrls.bits.save_vmx_timer =
				vmexit_ctrls.bits.save_vmx_timer;
			merged_counter_value = (uint32_t)mon_vmcs_read(vmcs_0,
				VMCS_PREEMPTION_TIMER);
		} else if (0 == pin_exec[0].bits.vmx_timer) {
			/* copy from vmcs#1 */
			vmexit_ctrls.uint32 = (uint32_t)mon_vmcs_read(vmcs_1,
				VMCS_EXIT_CONTROL_VECTOR);
			merged_vmexit_ctrls.bits.save_vmx_timer =
				vmexit_ctrls.bits.save_vmx_timer;
			merged_counter_value =
				(uint32_t)mon_vmcs_read(vmcs_1,
					VMCS_PREEMPTION_TIMER);
		} else {
			/* VMX Timer enabled at least in one VMCS */
			/* so doing real merge here */
			merged_vmexit_ctrls.bits.save_vmx_timer = 1;
			counter_value[0] =
				(uint32_t)mon_vmcs_read(vmcs_0,
					VMCS_PREEMPTION_TIMER);
			counter_value[1] =
				(uint32_t)mon_vmcs_read(vmcs_1,
					VMCS_PREEMPTION_TIMER);
			merged_counter_value = MIN(counter_value[0],
				counter_value[1]);
		}
	}
	mon_vmcs_write(vmcs_m, VMCS_CONTROL_VECTOR_PIN_EVENTS,
		(uint64_t)merged_pin_exec.uint32);
	mon_vmcs_write(vmcs_m, VMCS_EXIT_CONTROL_VECTOR,
		(uint64_t)merged_vmexit_ctrls.uint32);
	mon_vmcs_write(vmcs_m,
		VMCS_PREEMPTION_TIMER,
		(uint64_t)merged_counter_value);
}

void ms_split_timer_from_level2(vmcs_object_t *vmcs_0, vmcs_object_t *vmcs_1,
				vmcs_object_t *vmcs_m)
{
	pin_based_vm_execution_controls_t pin_exec[2];
	vmexit_controls_t vmexit_ctrls[2];
	uint32_t old_counter[2];
	uint32_t new_counter;
	int i;

	pin_exec[0].uint32 = (uint32_t)mon_vmcs_read(vmcs_0,
		VMCS_CONTROL_VECTOR_PIN_EVENTS);
	pin_exec[1].uint32 = (uint32_t)mon_vmcs_read(vmcs_1,
		VMCS_CONTROL_VECTOR_PIN_EVENTS);
	vmexit_ctrls[0].uint32 = (uint32_t)mon_vmcs_read(vmcs_0,
		VMCS_EXIT_CONTROL_VECTOR);
	vmexit_ctrls[1].uint32 = (uint32_t)mon_vmcs_read(vmcs_1,
		VMCS_EXIT_CONTROL_VECTOR);
	old_counter[0] = (uint32_t)mon_vmcs_read(vmcs_0, VMCS_PREEMPTION_TIMER);
	old_counter[1] = (uint32_t)mon_vmcs_read(vmcs_1, VMCS_PREEMPTION_TIMER);

	for (i = 0; i < 2; ++i) {
		if (1 == pin_exec[i].bits.vmx_timer &&
		    1 == vmexit_ctrls[i].bits.save_vmx_timer) {
			if (0 == pin_exec[1 - i].bits.vmx_timer) {
				new_counter = old_counter[i];
			} else {
				if (old_counter[i] <= old_counter[1 - i]) {
					new_counter =
						(uint32_t)mon_vmcs_read(vmcs_m,
							VMCS_PREEMPTION_TIMER);
				} else {
					new_counter =
						(uint32_t)mon_vmcs_read(vmcs_m,
							VMCS_PREEMPTION_TIMER)
						+ (old_counter[i] -
						   old_counter[1 - i]);
				}
			}
			mon_vmcs_write(vmcs_0, VMCS_PREEMPTION_TIMER,
				(uint64_t)new_counter);
		}
	}
}
