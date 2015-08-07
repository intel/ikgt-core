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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(GUEST_CPU_ACCESS_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(GUEST_CPU_ACCESS_C, __condition)
#include "guest_cpu_internal.h"
#include "heap.h"
#include "array_iterators.h"
#include "gpm_api.h"
#include "scheduler.h"
#include "vmx_ctrl_msrs.h"
#include "mon_dbg.h"
#include "vmcs_init.h"
#include "page_walker.h"
#include "gpm_api.h"
#include "guest.h"
#include "msr_defs.h"
#include "host_memory_manager_api.h"
#include "unrestricted_guest.h"
#include "mon_callback.h"


/* -------------------------- types --------------------------------------- */
typedef struct {
	vmcs_field_t sel, base, limit, ar;
} segment_2_vmcs_t;

/* encoding table for segments */
const segment_2_vmcs_t g_segment_2_vmcs[IA32_SEG_COUNT] = {
	/* IA32_SEG_CS */   { VMCS_GUEST_CS_SELECTOR,
			      VMCS_GUEST_CS_BASE,
			      VMCS_GUEST_CS_LIMIT,
			      VMCS_GUEST_CS_AR },
	/* IA32_SEG_DS */   { VMCS_GUEST_DS_SELECTOR,
			      VMCS_GUEST_DS_BASE,
			      VMCS_GUEST_DS_LIMIT,
			      VMCS_GUEST_DS_AR },
	/* IA32_SEG_SS */   { VMCS_GUEST_SS_SELECTOR,
			      VMCS_GUEST_SS_BASE,
			      VMCS_GUEST_SS_LIMIT,
			      VMCS_GUEST_SS_AR },
	/* IA32_SEG_ES */   { VMCS_GUEST_ES_SELECTOR,
			      VMCS_GUEST_ES_BASE,
			      VMCS_GUEST_ES_LIMIT,
			      VMCS_GUEST_ES_AR },
	/* IA32_SEG_FS */   { VMCS_GUEST_FS_SELECTOR,
			      VMCS_GUEST_FS_BASE,
			      VMCS_GUEST_FS_LIMIT,
			      VMCS_GUEST_FS_AR },
	/* IA32_SEG_GS */   { VMCS_GUEST_GS_SELECTOR,
			      VMCS_GUEST_GS_BASE,
			      VMCS_GUEST_GS_LIMIT,
			      VMCS_GUEST_GS_AR },
	/* IA32_SEG_LDTR */ { VMCS_GUEST_LDTR_SELECTOR,
			      VMCS_GUEST_LDTR_BASE,
			      VMCS_GUEST_LDTR_LIMIT,
			      VMCS_GUEST_LDTR_AR },
	/* IA32_SEG_TR */   { VMCS_GUEST_TR_SELECTOR,
			      VMCS_GUEST_TR_BASE,
			      VMCS_GUEST_TR_LIMIT,
			      VMCS_GUEST_TR_AR }
};

/* encoding table for msrs */
vmcs_field_t g_msr_2_vmcs[] = {
	VMCS_GUEST_DEBUG_CONTROL,
	VMCS_GUEST_EFER,
	VMCS_GUEST_PAT,
	VMCS_GUEST_SYSENTER_ESP,
	VMCS_GUEST_SYSENTER_EIP,
	VMCS_GUEST_SYSENTER_CS,
	VMCS_GUEST_SMBASE,
	VMCS_GUEST_IA32_PERF_GLOBAL_CTRL,
	VMCS_GUEST_FS_BASE,
	VMCS_GUEST_GS_BASE
};

const uint32_t g_msr_2_index[] = {
	IA32_MSR_DEBUGCTL,
	IA32_MSR_EFER,
	IA32_MSR_PAT,
	IA32_MSR_SYSENTER_ESP,
	IA32_MSR_SYSENTER_EIP,
	IA32_MSR_SYSENTER_CS,
	IA32_INVALID_MSR_INDEX,
	IA32_MSR_PERF_GLOBAL_CTRL,
	IA32_MSR_FS_BASE,
	IA32_MSR_GS_BASE
};

/* ---------------------------- internal funcs ---------------------------- */
/* ---------------------------- APIs -------------------------------------- */

/*-------------------------------------------------------------------------
 *
 * Getters/setters
 *
 *------------------------------------------------------------------------- */
boolean_t gcpu_is_native_execution(guest_cpu_handle_t gcpu)
{
	return IS_MODE_NATIVE(gcpu);
}

uint64_t gcpu_get_native_gp_reg_layered(const guest_cpu_handle_t gcpu,
					mon_ia32_gp_registers_t reg,
					vmcs_level_t level)
{
	MON_ASSERT(reg < IA32_REG_GP_COUNT);

	switch (reg) {
	case IA32_REG_RSP:
		return mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
				vmcs_hierarchy, level),
			VMCS_GUEST_RSP);

	case IA32_REG_RIP:
		return mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
				vmcs_hierarchy, level),
			VMCS_GUEST_RIP);

	case IA32_REG_RFLAGS:
		return mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
				vmcs_hierarchy, level),
			VMCS_GUEST_RFLAGS);

	default:
		return gcpu->save_area.gp.reg[reg];
	}
}

void gcpu_set_native_gp_reg_layered(guest_cpu_handle_t gcpu,
				    mon_ia32_gp_registers_t reg,
				    uint64_t value, vmcs_level_t level)
{
	MON_ASSERT(reg < IA32_REG_GP_COUNT);

	switch (reg) {
	case IA32_REG_RSP:
		mon_vmcs_write(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
				level),
			VMCS_GUEST_RSP, value);
		return;

	case IA32_REG_RIP:
		mon_vmcs_write(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
				level),
			VMCS_GUEST_RIP, value);
		return;

	case IA32_REG_RFLAGS:
		mon_vmcs_write(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
				level),
			VMCS_GUEST_RFLAGS, value);
		return;

	default:
		gcpu->save_area.gp.reg[reg] = value;
		return;
	}
}

uint64_t gcpu_get_gp_reg_layered(const guest_cpu_handle_t gcpu,
				 mon_ia32_gp_registers_t reg,
				 vmcs_level_t level)
{
	MON_ASSERT(gcpu);

	return gcpu_get_native_gp_reg_layered(gcpu, reg, level);
}

void gcpu_set_all_gp_regs_internal(const guest_cpu_handle_t gcpu,
				   uint64_t *gp_reg)
{
	gcpu->save_area.gp.reg[IA32_REG_RAX] = *gp_reg;
	gcpu->save_area.gp.reg[IA32_REG_RBX] = *(gp_reg + 1);
	gcpu->save_area.gp.reg[IA32_REG_RCX] = *(gp_reg + 2);
	gcpu->save_area.gp.reg[IA32_REG_RDX] = *(gp_reg + 3);
	gcpu->save_area.gp.reg[IA32_REG_RBP] = *(gp_reg + 5);
	gcpu->save_area.gp.reg[IA32_REG_RSI] = *(gp_reg + 6);
	gcpu->save_area.gp.reg[IA32_REG_RDI] = *(gp_reg + 7);
	gcpu->save_area.gp.reg[IA32_REG_R8] = *(gp_reg + 8);
	gcpu->save_area.gp.reg[IA32_REG_R9] = *(gp_reg + 9);
	gcpu->save_area.gp.reg[IA32_REG_R10] = *(gp_reg + 10);
	gcpu->save_area.gp.reg[IA32_REG_R11] = *(gp_reg + 11);
	gcpu->save_area.gp.reg[IA32_REG_R12] = *(gp_reg + 12);
	gcpu->save_area.gp.reg[IA32_REG_R13] = *(gp_reg + 13);
	gcpu->save_area.gp.reg[IA32_REG_R14] = *(gp_reg + 14);
	gcpu->save_area.gp.reg[IA32_REG_R15] = *(gp_reg + 15);
}

void gcpu_set_gp_reg_layered(guest_cpu_handle_t gcpu,
			     mon_ia32_gp_registers_t reg,
			     uint64_t value,
			     vmcs_level_t level)
{
	MON_ASSERT(gcpu);

	gcpu_set_native_gp_reg_layered(gcpu, reg, value, level);
}

void gcpu_set_xmm_reg(guest_cpu_handle_t gcpu, mon_ia32_xmm_registers_t reg,
		      uint128_t value)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));
	MON_ASSERT(reg < IA32_REG_XMM_COUNT);
	gcpu->save_area.xmm.reg[reg] = value;
}

void gcpu_get_segment_reg_layered(const guest_cpu_handle_t gcpu,
				  mon_ia32_segment_registers_t reg,
				  uint16_t *selector, uint64_t *base,
				  uint32_t *limit, uint32_t *attributes,
				  vmcs_level_t level)
{
	const segment_2_vmcs_t *seg2vmcs;
	vmcs_object_t *vmcs;

	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));
	MON_ASSERT(reg < IA32_SEG_COUNT);

	vmcs = vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy, level);

	seg2vmcs = &g_segment_2_vmcs[reg];

	if (selector) {
		*selector = (uint16_t)mon_vmcs_read(vmcs, seg2vmcs->sel);
	}

	if (base) {
		*base = mon_vmcs_read(vmcs, seg2vmcs->base);
	}

	if (limit) {
		*limit = (uint32_t)mon_vmcs_read(vmcs, seg2vmcs->limit);
	}

	if (attributes) {
		*attributes = (uint32_t)mon_vmcs_read(vmcs, seg2vmcs->ar);
	}
}

void gcpu_set_segment_reg_layered(guest_cpu_handle_t gcpu,
				  mon_ia32_segment_registers_t reg,
				  uint16_t selector, uint64_t base,
				  uint32_t limit, uint32_t attributes,
				  vmcs_level_t level)
{
	const segment_2_vmcs_t *seg2vmcs;
	vmcs_object_t *vmcs;

	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));
	MON_ASSERT(reg < IA32_SEG_COUNT);

	vmcs = vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy, level);

	seg2vmcs = &g_segment_2_vmcs[reg];

	mon_vmcs_write(vmcs, seg2vmcs->sel, selector);
	mon_vmcs_write(vmcs, seg2vmcs->base, base);
	mon_vmcs_write(vmcs, seg2vmcs->limit, limit);
	mon_vmcs_write(vmcs, seg2vmcs->ar, attributes);
}

uint64_t gcpu_get_control_reg_layered(const guest_cpu_handle_t gcpu,
				      mon_ia32_control_registers_t reg,
				      vmcs_level_t level)
{
	MON_ASSERT(gcpu);

	MON_ASSERT(reg < IA32_CTRL_COUNT);

	switch (reg) {
	case IA32_CTRL_CR0:
		return mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
				vmcs_hierarchy, level),
			VMCS_GUEST_CR0);

	case IA32_CTRL_CR2:
		return gcpu->save_area.gp.reg[CR2_SAVE_AREA];

	case IA32_CTRL_CR3:
		return mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
				vmcs_hierarchy, level),
			VMCS_GUEST_CR3);

	case IA32_CTRL_CR4:
		return mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
				vmcs_hierarchy, level),
			VMCS_GUEST_CR4);

	case IA32_CTRL_CR8:
		return gcpu->save_area.gp.reg[CR8_SAVE_AREA];

	default:
		MON_LOG(mask_anonymous,
			level_trace,
			"unknown control register\n");
		MON_DEADLOOP();
	}

	/* if we here - something is wrong */
	return 0;
}

void gcpu_set_control_reg_layered(guest_cpu_handle_t gcpu,
				  mon_ia32_control_registers_t reg,
				  uint64_t value, vmcs_level_t level)
{
	vmcs_object_t *vmcs;

	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));
	MON_ASSERT(reg < IA32_CTRL_COUNT);

	vmcs = gcpu_get_vmcs_layered(gcpu, level);

	switch (reg) {
	case IA32_CTRL_CR0:
		if (vmcs == mon_gcpu_get_vmcs(gcpu)) {
			value = vmcs_hw_make_compliant_cr0(value);
		}
		mon_vmcs_write(vmcs, VMCS_GUEST_CR0, value);
		break;

	case IA32_CTRL_CR2:
		gcpu->save_area.gp.reg[CR2_SAVE_AREA] = value;
		break;

	case IA32_CTRL_CR3:
		mon_vmcs_write(vmcs, VMCS_GUEST_CR3, value);
		break;

	case IA32_CTRL_CR4:
		if (vmcs == mon_gcpu_get_vmcs(gcpu)) {
			value = vmcs_hw_make_compliant_cr4(value);
		}
		mon_vmcs_write(vmcs, VMCS_GUEST_CR4, value);
		break;

	case IA32_CTRL_CR8:
		value = vmcs_hw_make_compliant_cr8(value);
		gcpu->save_area.gp.reg[CR8_SAVE_AREA] = value;
		break;

	default:
		MON_LOG(mask_anonymous,
			level_trace,
			"unknown control register\n");
		MON_DEADLOOP();
	}
}

/* special case of CR registers - some bits of CR0 and CR4 may be overridden by
 * MON, so that guest will see not real values all other registers return the
 * same value as gcpu_get_control_reg()
 * valid for CR0, CR3, CR4 */
uint64_t gcpu_get_guest_visible_control_reg_layered(
	const guest_cpu_handle_t gcpu,
	mon_ia32_control_registers_t reg,
	vmcs_level_t level)
{
	uint64_t mask;
	uint64_t shadow;
	uint64_t real_value;
	vmcs_object_t *vmcs = vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
		level);

	MON_ASSERT(gcpu);

	if (reg == IA32_CTRL_CR3) {
		real_value = gcpu->save_area.gp.reg[CR3_SAVE_AREA];

		if (INVALID_CR3_SAVED_VALUE == real_value) {
			real_value =
				gcpu_get_control_reg_layered(gcpu,
					IA32_CTRL_CR3,
					level);
		}

		return real_value;
	}

	real_value = gcpu_get_control_reg_layered(gcpu, reg, level);

	if (reg == IA32_CTRL_CR0) {
		mask = mon_vmcs_read(vmcs, VMCS_CR0_MASK);
		shadow = mon_vmcs_read(vmcs, VMCS_CR0_READ_SHADOW);
	} else if (reg == IA32_CTRL_CR4) {
		mask = mon_vmcs_read(vmcs, VMCS_CR4_MASK);
		shadow = mon_vmcs_read(vmcs, VMCS_CR4_READ_SHADOW);
	} else {
		return real_value;
	}

	return (real_value & ~mask) | (shadow & mask);
}

/* valid only for CR0, CR3 and CR4 */
void gcpu_set_guest_visible_control_reg_layered(const guest_cpu_handle_t gcpu,
						mon_ia32_control_registers_t reg,
						uint64_t value,
						vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	if (reg == IA32_CTRL_CR3) {
		MON_ASSERT(level == VMCS_MERGED);
		gcpu->save_area.gp.reg[CR3_SAVE_AREA] = value;
	} else if (reg == IA32_CTRL_CR0) {
		SET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu);
		mon_vmcs_write(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
				level),
			VMCS_CR0_READ_SHADOW, value);
	} else if (reg == IA32_CTRL_CR4) {
		mon_vmcs_write(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
				level),
			VMCS_CR4_READ_SHADOW, value);
	} else {
		/* pass thru */
		gcpu_set_control_reg_layered(gcpu, reg, value, level);
	}
}

void gcpu_get_gdt_reg_layered(const guest_cpu_handle_t gcpu, uint64_t *base,
			      uint32_t *limit, vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	if (base) {
		*base =
			mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
					vmcs_hierarchy,
					level), VMCS_GUEST_GDTR_BASE);
	}

	if (limit) {
		*limit = (uint32_t)
			 mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
				vmcs_hierarchy,
				level), VMCS_GUEST_GDTR_LIMIT);
	}
}

void gcpu_set_gdt_reg_layered(const guest_cpu_handle_t gcpu, uint64_t base,
			      uint32_t limit, vmcs_level_t level)
{
	vmcs_object_t *vmcs = vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
		level);

	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	mon_vmcs_write(vmcs, VMCS_GUEST_GDTR_BASE, base);
	mon_vmcs_write(vmcs, VMCS_GUEST_GDTR_LIMIT, limit);
}

void gcpu_get_idt_reg_layered(const guest_cpu_handle_t gcpu, uint64_t *base,
			      uint32_t *limit, vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	if (base) {
		*base =
			mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
					vmcs_hierarchy, level),
				VMCS_GUEST_IDTR_BASE);
	}

	if (limit) {
		*limit = (uint32_t)
			 mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
				vmcs_hierarchy,
				level), VMCS_GUEST_IDTR_LIMIT);
	}
}

void gcpu_set_idt_reg_layered(const guest_cpu_handle_t gcpu, uint64_t base,
			      uint32_t limit, vmcs_level_t level)
{
	vmcs_object_t *vmcs = vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
		level);

	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	mon_vmcs_write(vmcs, VMCS_GUEST_IDTR_BASE, base);
	mon_vmcs_write(vmcs, VMCS_GUEST_IDTR_LIMIT, limit);
}

uint64_t gcpu_get_debug_reg_layered(const guest_cpu_handle_t gcpu,
				    mon_ia32_debug_registers_t reg,
				    vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));
	MON_ASSERT(reg < IA32_REG_DEBUG_COUNT);

	if (reg == IA32_REG_DR7) {
		return mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->
				vmcs_hierarchy,
				level), VMCS_GUEST_DR7);
	} else {
		if (!GET_DEBUG_REGS_CACHED_FLAG(gcpu)) {
			cache_debug_registers(gcpu);
		}
		return gcpu->save_area.debug.reg[reg];
	}
}

void gcpu_set_debug_reg_layered(const guest_cpu_handle_t gcpu,
				mon_ia32_debug_registers_t reg,
				uint64_t value, vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));
	MON_ASSERT(reg < IA32_REG_DEBUG_COUNT);

	if (reg == IA32_REG_DR7) {
		mon_vmcs_write(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
				level),
			VMCS_GUEST_DR7, value);
	} else {
		if (!GET_DEBUG_REGS_CACHED_FLAG(gcpu)) {
			cache_debug_registers(gcpu);
		}
		gcpu->save_area.debug.reg[reg] = value;
		SET_DEBUG_REGS_MODIFIED_FLAG(gcpu);
	}
}

uint32_t gcpu_get_interruptibility_state_layered(const guest_cpu_handle_t gcpu,
						 vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	return (uint32_t)
	       mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
			level),
		VMCS_GUEST_INTERRUPTIBILITY);
}

void gcpu_set_interruptibility_state_layered(const guest_cpu_handle_t gcpu,
					     uint32_t value, vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	mon_vmcs_write(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy, level),
		VMCS_GUEST_INTERRUPTIBILITY, value);
}

ia32_vmx_vmcs_guest_sleep_state_t
gcpu_get_activity_state_layered(const guest_cpu_handle_t gcpu,
				vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	return mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
			level), VMCS_GUEST_SLEEP_STATE);
}

void gcpu_set_activity_state_layered(guest_cpu_handle_t gcpu,
				     ia32_vmx_vmcs_guest_sleep_state_t value,
				     vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	mon_vmcs_write(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy, level),
		VMCS_GUEST_SLEEP_STATE, value);

	if ((value != GET_CACHED_ACTIVITY_STATE(gcpu)) &&
	    (!gcpu_is_vmcs_layered(gcpu) || (VMCS_MERGED == level))) {
		SET_ACTIVITY_STATE_CHANGED_FLAG(gcpu);
		SET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu);
	}
}

uint64_t gcpu_get_pending_debug_exceptions_layered(
	const guest_cpu_handle_t gcpu,
	vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	return mon_vmcs_read(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
			level), VMCS_GUEST_PEND_DBE);
}

void gcpu_set_pending_debug_exceptions_layered(const guest_cpu_handle_t gcpu,
					       uint64_t value,
					       vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	mon_vmcs_write(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy, level),
		VMCS_GUEST_PEND_DBE, value);
}

void gcpu_set_vmenter_control_layered(const guest_cpu_handle_t gcpu,
				      vmcs_level_t level)
{
	vmentry_controls_t entry_ctrl_mask;
	uint64_t value;

	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	/* IA Manual 3B Appendix G.6 - On processors that support UG VM exits store
	 * the value of IA32_EFER.LMA into the IA-32e mode guest VM-entry control
	 */
	value = gcpu_get_msr_reg(gcpu, IA32_MON_MSR_EFER);
	entry_ctrl_mask.uint32 = 0;
	entry_ctrl_mask.bits.ia32e_mode_guest = 1;
	vmcs_update(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy, level),
		VMCS_ENTER_CONTROL_VECTOR,
		(value & EFER_LMA) ? UINT64_ALL_ONES : 0,
		(uint64_t)entry_ctrl_mask.uint32);
}

static
boolean_t gcpu_get_msr_value_from_list(IN uint32_t msr_index,
				       IN ia32_vmx_msr_entry_t *list,
				       IN uint32_t count, OUT uint64_t *value)
{
	if (msr_index == IA32_INVALID_MSR_INDEX) {
		return FALSE;
	}

	if (NULL == list) {
		return FALSE;
	}

	for (; count > 0; count--) {
		if (list[count - 1].msr_index == msr_index) {
			*value = list[count - 1].msr_data;
			return TRUE;
		}
	}
	return FALSE;
}

static
boolean_t gcpu_set_msr_value_in_list(IN uint32_t msr_index, IN uint64_t value,
				     IN ia32_vmx_msr_entry_t *list,
				     IN uint32_t count)
{
	if (msr_index == IA32_INVALID_MSR_INDEX) {
		return FALSE;
	}

	if (list == NULL) {
		return FALSE;
	}

	for (; count > 0; count--) {
		if (list[count - 1].msr_index == msr_index) {
			list[count - 1].msr_data = value;
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * The input reg of index value of MSR must be less than the number of element
 * in g_msr_2_vmcs and g_msr_2_index.
 */
uint64_t gcpu_get_msr_reg_internal_layered(const guest_cpu_handle_t gcpu,
					   mon_ia32_model_specific_registers_t reg,
					   vmcs_level_t level)
{
	vmcs_object_t *vmcs = vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
		level);
	uint64_t vmexit_store_msr_list_addr;
	uint32_t vmexit_store_msr_list_count;
	ia32_vmx_msr_entry_t *vmexit_store_msr_list_ptr = NULL;
	uint64_t value = 0;
	vmexit_controls_t may1_vm_exit_ctrl =
		mon_vmcs_hw_get_vmx_constraints()->may1_vm_exit_ctrl;

	MON_ASSERT(gcpu);
	MON_DEBUG_CODE(MON_ASSERT(reg < NELEMENTS(g_msr_2_vmcs) &&
			reg < NELEMENTS(g_msr_2_index)));

	if (((g_msr_2_vmcs[reg] == VMCS_GUEST_DEBUG_CONTROL)
	     && (may1_vm_exit_ctrl.bits.save_debug_controls == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_SYSENTER_ESP)
		&& (may1_vm_exit_ctrl.bits.save_sys_enter_msrs == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_SYSENTER_EIP)
		&& (may1_vm_exit_ctrl.bits.save_sys_enter_msrs == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_SYSENTER_CS)
		&& (may1_vm_exit_ctrl.bits.save_sys_enter_msrs == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_EFER)
		&& (may1_vm_exit_ctrl.bits.save_efer == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_PAT)
		&& (may1_vm_exit_ctrl.bits.save_pat == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_IA32_PERF_GLOBAL_CTRL)
		&& (may1_vm_exit_ctrl.bits.load_ia32_perf_global_ctrl == 1))) {
		return mon_vmcs_read(vmcs, g_msr_2_vmcs[reg]);
	}

	vmexit_store_msr_list_addr = mon_vmcs_read(vmcs,
		VMCS_EXIT_MSR_STORE_ADDRESS);
	vmexit_store_msr_list_count =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_COUNT);

	MON_ASSERT(0 == vmexit_store_msr_list_count ||
		ALIGN_BACKWARD((uint64_t)vmexit_store_msr_list_addr,
			sizeof(ia32_vmx_msr_entry_t)) ==
		(uint64_t)vmexit_store_msr_list_addr);

	if ((level == VMCS_MERGED) ||
	    (gcpu_get_guest_level(gcpu) == GUEST_LEVEL_1_SIMPLE)) {
		if ((vmexit_store_msr_list_count != 0)
		    && (!mon_hmm_hpa_to_hva(vmexit_store_msr_list_addr,
				(hva_t *)&vmexit_store_msr_list_ptr))) {
			MON_LOG(mask_anonymous,
				level_trace,
				"%s: Failed to translate hpa_t to hva_t\n",
				__FUNCTION__);
			MON_DEADLOOP();
		}
	} else {
		vmexit_store_msr_list_ptr =
			(ia32_vmx_msr_entry_t *)vmexit_store_msr_list_addr;
	}

	if (gcpu_get_msr_value_from_list(g_msr_2_index[reg],
		    vmexit_store_msr_list_ptr,
		    vmexit_store_msr_list_count, &value)) {
		return value;
	}

	/* Will never reach here */
	MON_ASSERT(0);
	return 0;
}

uint64_t gcpu_get_msr_reg_layered(const guest_cpu_handle_t gcpu,
				  mon_ia32_model_specific_registers_t reg,
				  vmcs_level_t level)
{
	MON_ASSERT(gcpu && IS_MODE_NATIVE(gcpu));

	return gcpu_get_msr_reg_internal_layered(gcpu, reg, level);
}

uint64_t gcpu_get_msr_reg_by_index_layered(guest_cpu_handle_t gcpu,
					   uint32_t msr_index,
					   vmcs_level_t level)
{
	uint32_t i;
	uint64_t value = 0;
	boolean_t found = FALSE;
	vmcs_object_t *vmcs = vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
		level);
	uint64_t vmexit_store_msr_list_addr;
	uint64_t vmenter_load_msr_list_addr;
	uint32_t vmenter_load_msr_list_count;
	ia32_vmx_msr_entry_t *vmenter_load_msr_list_ptr = NULL;

	MON_ASSERT(gcpu);

	for (i = 0; i < NELEMENTS(g_msr_2_index); i++) {
		if (g_msr_2_index[i] == msr_index) {
			value =
				gcpu_get_msr_reg_layered(gcpu,
					(mon_ia32_model_specific_registers_t)i,
					level);
			found = TRUE;
			break;
		}
	}

	if (!found) {
		vmenter_load_msr_list_addr =
			mon_vmcs_read(vmcs, VMCS_ENTER_MSR_LOAD_ADDRESS);
		vmenter_load_msr_list_count =
			(uint32_t)mon_vmcs_read(vmcs,
				VMCS_ENTER_MSR_LOAD_COUNT);

		MON_ASSERT(0 == vmenter_load_msr_list_count ||
			ALIGN_BACKWARD((uint64_t)vmenter_load_msr_list_addr,
				sizeof(ia32_vmx_msr_entry_t)) ==
			(uint64_t)vmenter_load_msr_list_addr);

		if ((level == VMCS_MERGED) ||
		    (gcpu_get_guest_level(gcpu) == GUEST_LEVEL_1_SIMPLE)) {
			if ((vmenter_load_msr_list_count != 0)
			    && (!mon_hmm_hpa_to_hva(vmenter_load_msr_list_addr,
					(hva_t *)&vmenter_load_msr_list_ptr))) {
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Failed to translate hpa_t %P to hva_t (gcpu = %P ;"
					" vmcs = %P; msr_index = 0x%X)\n",
					__FUNCTION__,
					vmenter_load_msr_list_addr,
					gcpu,
					vmcs,
					msr_index);
				MON_DEADLOOP();
			}
		} else {
			vmenter_load_msr_list_ptr =
				(ia32_vmx_msr_entry_t *)
				vmenter_load_msr_list_addr;
		}

		found = gcpu_get_msr_value_from_list(msr_index,
			vmenter_load_msr_list_ptr,
			vmenter_load_msr_list_count,
			&value);

		if (!found) {
			vmexit_store_msr_list_addr =
				mon_vmcs_read(vmcs,
					VMCS_EXIT_MSR_STORE_ADDRESS);
			MON_ASSERT(0 ==
				mon_vmcs_read(vmcs, VMCS_ENTER_MSR_LOAD_COUNT)
				|| ALIGN_BACKWARD((uint64_t)
					vmexit_store_msr_list_addr,
					sizeof(ia32_vmx_msr_entry_t)) ==
				(uint64_t)vmexit_store_msr_list_addr);

			if (vmexit_store_msr_list_addr !=
			    vmenter_load_msr_list_addr) {
				ia32_vmx_msr_entry_t *
				vmexit_store_load_msr_list_ptr = NULL;
				uint32_t vmexit_store_msr_list_count =
					(uint32_t)mon_vmcs_read(vmcs,
						VMCS_ENTER_MSR_LOAD_COUNT);

				if ((level == VMCS_MERGED) ||
				    (gcpu_get_guest_level(gcpu) ==
				     GUEST_LEVEL_1_SIMPLE)) {
					if ((vmexit_store_msr_list_count !=
					     0) &&
					    (!mon_hmm_hpa_to_hva(
						     vmexit_store_msr_list_addr,
						     (hva_t *)&
						     vmexit_store_load_msr_list_ptr))) {
						MON_LOG(mask_anonymous,
							level_trace,
							"%s: Failed to translate hpa_t %P to hva_t\n",
							__FUNCTION__,
							vmexit_store_msr_list_addr);
						MON_DEADLOOP();
					}
				} else {
					vmexit_store_load_msr_list_ptr =
						(ia32_vmx_msr_entry_t *)
						vmexit_store_msr_list_addr;
				}

				found =
					gcpu_get_msr_value_from_list(msr_index,
						vmexit_store_load_msr_list_ptr,
						vmexit_store_msr_list_count,
						&value);
			}
		}
		if (!found) {
			if (msr_index == IA32_MSR_GS_BASE) {
				return value = mon_vmcs_read(vmcs,
					VMCS_GUEST_GS_BASE);
			} else if (msr_index == IA32_MSR_FS_BASE) {
				return value = mon_vmcs_read(vmcs,
					VMCS_GUEST_FS_BASE);
			}

			value = hw_read_msr(msr_index);
		}
	}

	return value;
}

void gcpu_set_msr_reg_by_index_layered(guest_cpu_handle_t gcpu,
				       uint32_t msr_index,
				       uint64_t value,
				       vmcs_level_t level)
{
	uint32_t i;
	boolean_t found = FALSE;
	vmcs_object_t *vmcs = vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
		level);
	uint64_t vmexit_store_msr_list_addr;
	uint64_t vmenter_load_msr_list_addr;
	uint32_t vmenter_load_msr_list_count;
	ia32_vmx_msr_entry_t *vmenter_load_msr_list_ptr = NULL;

	MON_ASSERT(gcpu);

	for (i = 0; i < NELEMENTS(g_msr_2_index); i++) {
		if (g_msr_2_index[i] == msr_index) {
			gcpu_set_msr_reg_layered(gcpu,
				(mon_ia32_model_specific_registers_t)i,
				value, level);
			found = TRUE;
			break;
		}
	}

	if (!found) {
		vmenter_load_msr_list_addr =
			mon_vmcs_read(vmcs, VMCS_ENTER_MSR_LOAD_ADDRESS);
		vmenter_load_msr_list_count =
			(uint32_t)mon_vmcs_read(vmcs,
				VMCS_ENTER_MSR_LOAD_COUNT);

		MON_ASSERT(0 == vmenter_load_msr_list_count ||
			ALIGN_BACKWARD((uint64_t)vmenter_load_msr_list_addr,
				sizeof(ia32_vmx_msr_entry_t)) ==
			(uint64_t)vmenter_load_msr_list_addr);

		if ((level == VMCS_MERGED) ||
		    (gcpu_get_guest_level(gcpu) == GUEST_LEVEL_1_SIMPLE)) {
			if ((vmenter_load_msr_list_count != 0)
			    && (!mon_hmm_hpa_to_hva(vmenter_load_msr_list_addr,
					(hva_t *)&vmenter_load_msr_list_ptr))) {
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Failed to translate hpa_t %P to hva_t (gcpu = %P ;"
					" vmcs = %P; msr_index = 0x%X)\n",
					__FUNCTION__,
					vmenter_load_msr_list_addr,
					gcpu,
					vmcs,
					msr_index);
				MON_DEADLOOP();
			}
		} else {
			vmenter_load_msr_list_ptr =
				(ia32_vmx_msr_entry_t *)
				vmenter_load_msr_list_addr;
		}

		gcpu_set_msr_value_in_list(msr_index,
			value,
			vmenter_load_msr_list_ptr,
			vmenter_load_msr_list_count);

		vmexit_store_msr_list_addr =
			mon_vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_ADDRESS);
		MON_ASSERT(0 == mon_vmcs_read(vmcs, VMCS_ENTER_MSR_LOAD_COUNT)
			|| ALIGN_BACKWARD((uint64_t)vmexit_store_msr_list_addr,
				sizeof(ia32_vmx_msr_entry_t)) ==
			(uint64_t)vmexit_store_msr_list_addr);

		if (vmexit_store_msr_list_addr != vmenter_load_msr_list_addr) {
			ia32_vmx_msr_entry_t *vmexit_store_load_msr_list_ptr =
				NULL;
			uint32_t vmexit_store_msr_list_count =
				(uint32_t)mon_vmcs_read(vmcs,
					VMCS_ENTER_MSR_LOAD_COUNT);

			if ((level == VMCS_MERGED) ||
			    (gcpu_get_guest_level(gcpu) ==
			     GUEST_LEVEL_1_SIMPLE)) {
				if ((vmexit_store_msr_list_count != 0)
				    && (!mon_hmm_hpa_to_hva
						(vmexit_store_msr_list_addr,
						(hva_t *)&
						vmexit_store_load_msr_list_ptr))) {
					MON_LOG(mask_anonymous,
						level_trace,
						"%s: Failed to translate hpa_t %P to hva_t\n",
						__FUNCTION__,
						vmexit_store_msr_list_addr);
					MON_DEADLOOP();
				}
			} else {
				vmexit_store_load_msr_list_ptr =
					(ia32_vmx_msr_entry_t *)
					vmexit_store_msr_list_addr;
			}

			gcpu_set_msr_value_in_list(msr_index, value,
				vmexit_store_load_msr_list_ptr,
				vmexit_store_msr_list_count);
		}
	}
	if (!found) {
		hw_write_msr(msr_index, value);
	}
}

/*
 * The input reg of index value of MSR must be less than the number of element
 * in g_msr_2_vmcs and g_msr_2_index.
 */
void gcpu_set_msr_reg_layered(guest_cpu_handle_t gcpu,
			      mon_ia32_model_specific_registers_t reg,
			      uint64_t value, vmcs_level_t level)
{
	vmcs_object_t *vmcs = vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
		level);
	uint64_t vmexit_store_msr_list_addr;
	uint64_t vmenter_load_msr_list_addr;
	uint32_t vmenter_load_msr_list_count;
	ia32_vmx_msr_entry_t *vmenter_load_msr_list_ptr = NULL;
	vmexit_controls_t may1_vm_exit_ctrl =
		mon_vmcs_hw_get_vmx_constraints()->may1_vm_exit_ctrl;

	MON_ASSERT(gcpu);
	MON_DEBUG_CODE(MON_ASSERT(reg < NELEMENTS(g_msr_2_vmcs) &&
			reg < NELEMENTS(g_msr_2_index)));

	if (reg == IA32_MON_MSR_EFER) {
		SET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu);

		if (level != VMCS_LEVEL_1) {
			/*
			 * If EFER is changed, update
			 * VMCS_ENTER_CONTROL_VECTOR.ia32e_mode_guest
			 * accordingly. It is not done for Lvl-1 in order not to hide
			 * possible 3rd party bugs.
			 */
			vmentry_controls_t entry_ctrl_mask;
			entry_ctrl_mask.uint32 = 0;
			entry_ctrl_mask.bits.ia32e_mode_guest = 1;

			/* Update IA32e and LMA based on LME (since PG is always 1) */
			if (!IS_MODE_UNRESTRICTED_GUEST(gcpu)) {
				vmcs_update(vmcs,
					VMCS_ENTER_CONTROL_VECTOR,
					(value & EFER_LME) ? UINT64_ALL_ONES : 0,
					(uint64_t)entry_ctrl_mask.uint32);
				/* IA Manual 3B: 27.9.4 If the load IA32_EFER VM-entry
				 * control is 1, the value of the LME and LMA bits in the
				 * IA32_EFER field in the guest-state area must be the value of
				 * the IA-32e-mode guest VM-exit control.
				 * Otherwise, the VM entry fails. */
				if (value & EFER_LME) {
					value |= EFER_LMA;
				} else {
					value &= ~EFER_LMA;
				}
			}
		}
	}

	if (((g_msr_2_vmcs[reg] == VMCS_GUEST_DEBUG_CONTROL)
	     && (may1_vm_exit_ctrl.bits.save_debug_controls == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_SYSENTER_ESP)
		&& (may1_vm_exit_ctrl.bits.save_sys_enter_msrs == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_SYSENTER_EIP)
		&& (may1_vm_exit_ctrl.bits.save_sys_enter_msrs == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_SYSENTER_CS)
		&& (may1_vm_exit_ctrl.bits.save_sys_enter_msrs == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_EFER)
		&& (may1_vm_exit_ctrl.bits.save_efer == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_PAT)
		&& (may1_vm_exit_ctrl.bits.save_pat == 1))
	    || ((g_msr_2_vmcs[reg] == VMCS_GUEST_IA32_PERF_GLOBAL_CTRL)
		&& (may1_vm_exit_ctrl.bits.load_ia32_perf_global_ctrl == 1))) {
		mon_vmcs_write(vmcs, g_msr_2_vmcs[reg], value);
		return;
	}

	vmenter_load_msr_list_addr = mon_vmcs_read(vmcs,
		VMCS_ENTER_MSR_LOAD_ADDRESS);
	vmenter_load_msr_list_count =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_ENTER_MSR_LOAD_COUNT);

	MON_ASSERT(0 == vmenter_load_msr_list_count ||
		ALIGN_BACKWARD((uint64_t)vmenter_load_msr_list_addr,
			sizeof(ia32_vmx_msr_entry_t)) ==
		(uint64_t)vmenter_load_msr_list_addr);

	if ((level == VMCS_MERGED) ||
	    (gcpu_get_guest_level(gcpu) == GUEST_LEVEL_1_SIMPLE)) {
		if ((vmenter_load_msr_list_count != 0)
		    && (!mon_hmm_hpa_to_hva(vmenter_load_msr_list_addr,
				(hva_t *)&vmenter_load_msr_list_ptr))) {
			MON_LOG(mask_anonymous,
				level_trace,
				"%s: Failed to translate hpa_t %P to hva_t (gcpu = %P ;"
				" vmcs = %P; reg = %d)\n",
				__FUNCTION__,
				vmenter_load_msr_list_addr,
				gcpu,
				vmcs,
				reg);
			MON_DEADLOOP();
		}
	} else {
		vmenter_load_msr_list_ptr =
			(ia32_vmx_msr_entry_t *)vmenter_load_msr_list_addr;
	}

	if (!gcpu_set_msr_value_in_list(g_msr_2_index[reg],
		    value,
		    vmenter_load_msr_list_ptr,
		    vmenter_load_msr_list_count)) {
		if (g_msr_2_vmcs[reg] != VMCS_FIELD_COUNT) {
			mon_vmcs_write(vmcs, g_msr_2_vmcs[reg], value);
		}
	}

	vmexit_store_msr_list_addr = mon_vmcs_read(vmcs,
		VMCS_EXIT_MSR_STORE_ADDRESS);
	MON_ASSERT(0 == mon_vmcs_read(vmcs, VMCS_ENTER_MSR_LOAD_COUNT) ||
		ALIGN_BACKWARD((uint64_t)vmexit_store_msr_list_addr,
			sizeof(ia32_vmx_msr_entry_t)) ==
		(uint64_t)vmexit_store_msr_list_addr);

	if (vmexit_store_msr_list_addr != vmenter_load_msr_list_addr) {
		ia32_vmx_msr_entry_t *vmexit_store_load_msr_list_ptr = NULL;
		uint32_t vmexit_store_msr_list_count =
			(uint32_t)mon_vmcs_read(vmcs,
				VMCS_ENTER_MSR_LOAD_COUNT);

		if ((level == VMCS_MERGED) ||
		    (gcpu_get_guest_level(gcpu) == GUEST_LEVEL_1_SIMPLE)) {
			if ((vmexit_store_msr_list_count != 0)
			    && (!mon_hmm_hpa_to_hva(vmexit_store_msr_list_addr,
					(hva_t *)&vmexit_store_load_msr_list_ptr))) {
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Failed to translate hpa_t %P to hva_t\n",
					__FUNCTION__,
					vmexit_store_msr_list_addr);
				MON_DEADLOOP();
			}
		} else {
			vmexit_store_load_msr_list_ptr =
				(ia32_vmx_msr_entry_t *)
				vmexit_store_msr_list_addr;
		}

		gcpu_set_msr_value_in_list(g_msr_2_index[reg], value,
			vmexit_store_load_msr_list_ptr,
			vmexit_store_msr_list_count);
	}
}

void gcpu_skip_guest_instruction(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs =
		vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy, VMCS_MERGED);
	uint64_t inst_length = mon_vmcs_read(vmcs,
		VMCS_EXIT_INFO_INSTRUCTION_LENGTH);
	uint64_t rip = mon_vmcs_read(vmcs, VMCS_GUEST_RIP);

	mon_vmcs_write(vmcs, VMCS_GUEST_RIP, rip + inst_length);
	report_mon_event(MON_EVENT_SINGLE_STEPPING_CHECK,
		(mon_identification_data_t)gcpu,
		(const guest_vcpu_t *)mon_guest_vcpu(gcpu), NULL);
}

guest_level_t gcpu_get_guest_level(guest_cpu_handle_t gcpu)
{
	return (guest_level_t)gcpu->last_guest_level;
}

void gcpu_set_next_guest_level(guest_cpu_handle_t gcpu,
			       guest_level_t guest_level)
{
	gcpu->next_guest_level = (uint8_t)guest_level;
}

static
uint64_t gcpu_read_pdpt_entry_from_memory(void *pdpte_ptr)
{
	volatile uint64_t *pdpte = (volatile uint64_t *)pdpte_ptr;
	uint64_t value1 = *pdpte;
	uint64_t value2 = *pdpte;

	while (value1 != value2) {
		value1 = value2;
		value2 = *pdpte;
	}

	return value1;
}

boolean_t gcpu_get_32_bit_pdpt(guest_cpu_handle_t gcpu, void *pdpt_ptr)
{
	/* TODO: read PDPTE from VMCS in v2 */
	uint64_t *pdpt_out = (uint64_t *)pdpt_ptr;
	uint64_t cr3 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR3);
	guest_handle_t guest_handle = mon_gcpu_guest_handle(gcpu);
	gpm_handle_t gpm_handle = gcpu_get_current_gpm(guest_handle);
	gpa_t pdpt_gpa;
	hva_t pdpt_hva;
	uint32_t i;

	if (!IS_MODE_UNRESTRICTED_GUEST(gcpu)) {
		MON_ASSERT(gcpu_get_guest_visible_control_reg(gcpu,
				IA32_CTRL_CR0) &
			CR0_PG);
		MON_ASSERT(gcpu_get_guest_visible_control_reg(gcpu,
				IA32_CTRL_CR0) &
			CR0_PE);
	}
	MON_ASSERT(gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR4) &
		CR4_PAE);

	pdpt_gpa = ALIGN_BACKWARD(cr3,
		(uint64_t)(PW_SIZE_OF_PAE_ENTRY *
			   PW_NUM_OF_PDPT_ENTRIES_IN_32_BIT_MODE));
	if (!gpm_gpa_to_hva(gpm_handle, pdpt_gpa, &pdpt_hva)) {
		MON_LOG(mask_anonymous,
			level_trace,
			"%s: Failed to retrieve pointer to guest PDPT\n",
			__FUNCTION__);
		return FALSE;
	}

	for (i = 0; i < PW_NUM_OF_PDPT_ENTRIES_IN_32_BIT_MODE; i++) {
		uint64_t *pdpte =
			(uint64_t *)(pdpt_hva + (PW_SIZE_OF_PAE_ENTRY * i));
		pdpt_out[i] = gcpu_read_pdpt_entry_from_memory(pdpte);
	}
	return TRUE;
}

void gcpu_load_segment_reg_from_gdt(guest_cpu_handle_t guest_cpu,
				    uint64_t gdt_base, uint16_t selector,
				    mon_ia32_segment_registers_t reg_id)
{
	address_t base;
	uint32_t limit;
	uint32_t attributes;
	mon_status_t status;

	status = hw_gdt_parse_entry((uint8_t *)gdt_base, selector,
		&base, &limit, &attributes);
	MON_ASSERT(status == MON_OK);
	gcpu_set_segment_reg(guest_cpu,
		reg_id,
		selector,
		base,
		limit,
		attributes);
}

void *gcpu_get_vmdb(guest_cpu_handle_t gcpu)
{
	return gcpu->vmdb;
}

void gcpu_set_vmdb(guest_cpu_handle_t gcpu, void *vmdb)
{
	gcpu->vmdb = vmdb;
}

void *gcpu_get_timer(guest_cpu_handle_t gcpu)
{
	return gcpu->timer;
}

void gcpu_assign_timer(guest_cpu_handle_t gcpu, void *timer)
{
	gcpu->timer = timer;
}

/* / ARBTYE format */
typedef union {
	uint32_t as_uint32;
	struct {
		uint32_t	type:4;               /* bits 3:0 */
		uint32_t	s_bit:1;              /* bit 4 */
		uint32_t	dpl:2;                /* bit2 6:5 */
		uint32_t	p_bit:1;              /* bit 7 */
		uint32_t	reserved_11_8:4;      /* bits 11:8 */
		uint32_t	avl_bit:1;            /* bit 12 */
		uint32_t	l_bit:1;              /* bit 13 */
		uint32_t	db_bit:1;             /* bit 14 */
		uint32_t	g_bit:1;              /* bit 15 */
		uint32_t	null_bit:1;           /* bit 16 */
		uint32_t	reserved_31_17:15;    /* bits 31:17 */
	} bits;
} arch_arbyte_t;

typedef struct {
	uint16_t	selector;
	uint64_t	base;
	uint32_t	limit;
	arch_arbyte_t	arbyte;
} seg_reg64_t;


static
void arch_make_segreg_real_mode_compliant(seg_reg64_t *p_segreg,
					  mon_ia32_segment_registers_t reg_id)
{
	boolean_t g_must_be_zero = FALSE;
	boolean_t g_must_be_one = FALSE;

	/* if LDTR points to NULL entry, mark as unusable */
	if (p_segreg->selector < 8 && IA32_SEG_LDTR == reg_id) {
		p_segreg->arbyte.bits.null_bit = 1;
	}

	/* TR and CS must be usable !!! */
	if (IA32_SEG_TR == reg_id || IA32_SEG_CS == reg_id) {
		p_segreg->arbyte.bits.null_bit = 0;
	}

	if (1 == p_segreg->arbyte.bits.null_bit) {
		return;
	}

	/* Assume we run in Ring-0 */
	BITMAP_CLR(p_segreg->selector, 7);      /* Clear TI-flag and RPL */

	p_segreg->arbyte.as_uint32 &= 0xF0FF;   /* clear all reserved */
	p_segreg->arbyte.bits.dpl = 0;          /* Assume we run in Ring-0 */
	p_segreg->arbyte.bits.p_bit = 1;

	/*
	 * Set Granularity bit If any bit in the limit field in the range 11:0 is
	 * 0, G must be 0. If any bit in the limit field in the range 31:20 is 1, G
	 * must be 1. */
	if (0xFFF != (p_segreg->limit & 0xFFF)) {
		g_must_be_zero = TRUE;
	}
	if (0 != (p_segreg->limit & 0xFFF00000)) {
		g_must_be_one = TRUE;
	}

	MON_ASSERT(FALSE == g_must_be_zero || FALSE == g_must_be_one);

	if (g_must_be_one) {
		p_segreg->arbyte.bits.g_bit = 1;
	} else {
		p_segreg->arbyte.bits.g_bit = 0;
	}

	switch (reg_id) {
	case IA32_SEG_CS:
		p_segreg->arbyte.bits.type = 0xB;       /* Execute/Read, accessed */
		p_segreg->arbyte.bits.s_bit = 1;
		p_segreg->arbyte.bits.l_bit = 0;        /* 32-bit mode */
		break;

	case IA32_SEG_SS:
	case IA32_SEG_DS:
	case IA32_SEG_ES:
	case IA32_SEG_FS:
	case IA32_SEG_GS:
		p_segreg->arbyte.bits.type |= 3; /* Read/Write, accessed */
		p_segreg->arbyte.bits.s_bit = 1;
		break;

	case IA32_SEG_LDTR:
		BIT_CLR(p_segreg->selector, 2); /* TI-flag must be cleared */
		p_segreg->arbyte.bits.s_bit = 0;
		p_segreg->arbyte.bits.type = 2;
		break;

	case IA32_SEG_TR:
		BIT_CLR(p_segreg->selector, 2); /* TI-flag must be cleared */
		p_segreg->arbyte.bits.s_bit = 0;
		p_segreg->arbyte.bits.type = 11;
		break;

	default:
		break;
	}
}

void make_segreg_hw_real_mode_compliant(guest_cpu_handle_t gcpu,
					uint16_t selector,
					uint64_t base,
					uint32_t limit,
					uint32_t attr,
					mon_ia32_segment_registers_t reg_id)
{
	seg_reg64_t segreg;

	segreg.selector = selector;
	segreg.base = base;
	segreg.limit = limit;
	segreg.arbyte.as_uint32 = attr;

	arch_make_segreg_real_mode_compliant(&segreg, reg_id);

	gcpu_set_segment_reg(gcpu,
		reg_id,
		segreg.selector,
		segreg.base, segreg.limit, segreg.arbyte.as_uint32);
}

