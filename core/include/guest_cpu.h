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

#ifndef _GUEST_CPU_H_
#define _GUEST_CPU_H_

#include "mon_arch_defs.h"
#include "vmx_vmcs.h"
#include "mon_objects.h"
#include "vmexit.h"
#include "mon_startup.h"
#include "vmcs_hierarchy.h"
#include "mon_dbg.h"

#define PRINT_GCPU_IDENTITY(__gcpu)                                            \
	MON_DEBUG_CODE(                                                        \
	{                                                                      \
		const virtual_cpu_id_t *__vcpuid = mon_guest_vcpu(__gcpu);     \
		MON_LOG(mask_anonymous, level_trace, \
			"CPU(%d) Guest(%d) GuestCPU(%d)", \
			hw_cpu_id(),                                                           \
			__vcpuid->guest_id,                                                    \
			__vcpuid->guest_cpu_id);                                               \
	}                                                                                                                                                       \
	)

/**************************************************************************
*
* Define guest-related global structures
*
**************************************************************************/

/*
 * guest cpu state
 *
 * define single guest virtual cpu
 */
typedef struct {
	guest_id_t	guest_id;
	cpu_id_t	guest_cpu_id;
} virtual_cpu_id_t;

typedef enum {
	GUEST_LEVEL_1_SIMPLE,
	GUEST_LEVEL_1_MON,
	GUEST_LEVEL_2
} guest_level_t;

uint64_t gcpu_get_native_gp_reg_layered(const guest_cpu_handle_t gcpu,
					mon_ia32_gp_registers_t reg,
					vmcs_level_t level);
void gcpu_set_native_gp_reg_layered(guest_cpu_handle_t gcpu,
				    mon_ia32_gp_registers_t reg,
				    uint64_t value,
				    vmcs_level_t level);
uint64_t gcpu_get_gp_reg_layered(const guest_cpu_handle_t gcpu,
				 mon_ia32_gp_registers_t reg,
				 vmcs_level_t level);
void gcpu_set_all_gp_regs_internal(const guest_cpu_handle_t gcpu,
				   uint64_t *gp_reg);
void gcpu_set_gp_reg_layered(guest_cpu_handle_t gcpu,
			     mon_ia32_gp_registers_t reg,
			     uint64_t value,
			     vmcs_level_t level);
void gcpu_get_segment_reg_layered(const guest_cpu_handle_t gcpu,
				  mon_ia32_segment_registers_t reg,
				  uint16_t *selector,
				  uint64_t *base,
				  uint32_t *limit,
				  uint32_t *attributes,
				  vmcs_level_t level);
void gcpu_set_segment_reg_layered(guest_cpu_handle_t gcpu,
				  mon_ia32_segment_registers_t reg,
				  uint16_t selector,
				  uint64_t base,
				  uint32_t limit,
				  uint32_t attributes,
				  vmcs_level_t level);
uint64_t gcpu_get_control_reg_layered(const guest_cpu_handle_t gcpu,
				      mon_ia32_control_registers_t reg,
				      vmcs_level_t level);
void gcpu_set_control_reg_layered(guest_cpu_handle_t gcpu,
				  mon_ia32_control_registers_t reg,
				  uint64_t value,
				  vmcs_level_t level);
uint64_t gcpu_get_guest_visible_control_reg_layered(
	const guest_cpu_handle_t gcpu,
	mon_ia32_control_registers_t reg,
	vmcs_level_t level);
void gcpu_set_guest_visible_control_reg_layered(const guest_cpu_handle_t gcpu,
						mon_ia32_control_registers_t reg,
						uint64_t value,
						vmcs_level_t level);
void gcpu_set_cr0_reg_mask_layered(guest_cpu_handle_t gcpu,
				   vmcs_level_t level,
				   uint64_t value);
uint64_t gcpu_get_cr0_reg_mask_layered(guest_cpu_handle_t gcpu,
				       vmcs_level_t level);
void gcpu_set_cr4_reg_mask_layered(guest_cpu_handle_t gcpu,
				   vmcs_level_t level,
				   uint64_t value);
uint64_t gcpu_get_cr4_reg_mask_layered(guest_cpu_handle_t gcpu,
				       vmcs_level_t level);
void gcpu_set_pin_ctrls_layered(guest_cpu_handle_t gcpu,
				vmcs_level_t level,
				uint64_t value);
uint64_t gcpu_get_pin_ctrls_layered(guest_cpu_handle_t gcpu,
				    vmcs_level_t level);
void gcpu_set_processor_ctrls_layered(guest_cpu_handle_t gcpu,
				      vmcs_level_t level,
				      uint64_t value);
uint64_t gcpu_get_processor_ctrls_layered(guest_cpu_handle_t gcpu,
					  vmcs_level_t level);
void gcpu_set_processor_ctrls2_layered(guest_cpu_handle_t gcpu,
				       vmcs_level_t level,
				       uint64_t value);
uint64_t gcpu_get_processor_ctrls2_layered(guest_cpu_handle_t gcpu,
					   vmcs_level_t level);
void gcpu_set_exceptions_map_layered(guest_cpu_handle_t gcpu,
				     vmcs_level_t level,
				     uint64_t value);
uint64_t gcpu_get_exceptions_map_layered(guest_cpu_handle_t gcpu,
					 vmcs_level_t level);
void gcpu_set_exit_ctrls_layered(guest_cpu_handle_t gcpu,
				 vmcs_level_t level,
				 uint32_t value);
uint32_t gcpu_get_exit_ctrls_layered(guest_cpu_handle_t gcpu,
				     vmcs_level_t level);
void gcpu_set_enter_ctrls_layered(guest_cpu_handle_t gcpu,
				  vmcs_level_t level,
				  uint32_t value);
uint32_t gcpu_get_enter_ctrls_layered(guest_cpu_handle_t gcpu,
				      vmcs_level_t level);
void gcpu_get_pf_error_code_mask_and_match_layered(guest_cpu_handle_t gcpu,
						   vmcs_level_t level,
						   uint32_t *pf_mask,
						   uint32_t *pf_match);
void gcpu_set_pf_error_code_mask_and_match_layered(guest_cpu_handle_t gcpu,
						   vmcs_level_t level,
						   uint32_t pf_mask,
						   uint32_t pf_match);

void gcpu_get_gdt_reg_layered(const guest_cpu_handle_t gcpu,
			      uint64_t *base,
			      uint32_t *limit,
			      vmcs_level_t level);
void gcpu_set_gdt_reg_layered(const guest_cpu_handle_t gcpu,
			      uint64_t base,
			      uint32_t limit,
			      vmcs_level_t level);
void gcpu_get_idt_reg_layered(const guest_cpu_handle_t gcpu,
			      uint64_t *base,
			      uint32_t *limit,
			      vmcs_level_t level);
void gcpu_set_idt_reg_layered(const guest_cpu_handle_t gcpu,
			      uint64_t base,
			      uint32_t limit,
			      vmcs_level_t level);
uint64_t gcpu_get_debug_reg_layered(const guest_cpu_handle_t gcpu,
				    mon_ia32_debug_registers_t reg,
				    vmcs_level_t level);
void gcpu_set_debug_reg_layered(const guest_cpu_handle_t gcpu,
				mon_ia32_debug_registers_t reg,
				uint64_t value,
				vmcs_level_t level);
uint64_t gcpu_get_msr_reg_internal_layered(const guest_cpu_handle_t gcpu,
					   mon_ia32_model_specific_registers_t reg,
					   vmcs_level_t level);
uint64_t gcpu_get_msr_reg_layered(const guest_cpu_handle_t gcpu,
				  mon_ia32_model_specific_registers_t reg,
				  vmcs_level_t level);
void gcpu_set_msr_reg_layered(guest_cpu_handle_t gcpu,
			      mon_ia32_model_specific_registers_t reg,
			      uint64_t value,
			      vmcs_level_t level);

void gcpu_set_msr_reg_by_index_layered(guest_cpu_handle_t gcpu,
				       uint32_t msr_index,
				       uint64_t value,
				       vmcs_level_t level);

uint64_t gcpu_get_msr_reg_by_index_layered(guest_cpu_handle_t gcpu,
					   uint32_t msr_index,
					   vmcs_level_t level);

/*--------------------------------------------------------------------------
 *
 * Get Guest CPU state by virtual_cpu_id_t
 *
 * Return NULL if no such guest cpu
 *-------------------------------------------------------------------------- */
guest_cpu_handle_t gcpu_state(const virtual_cpu_id_t *vcpu);

/*--------------------------------------------------------------------------
 *
 * Get virtual_cpu_id_t by Guest CPU
 *
 *-------------------------------------------------------------------------- */
const virtual_cpu_id_t *mon_guest_vcpu(const guest_cpu_handle_t gcpu);

/*--------------------------------------------------------------------------
 *
 * Get Guest Handle by Guest CPU
 *
 *-------------------------------------------------------------------------- */
guest_handle_t mon_gcpu_guest_handle(const guest_cpu_handle_t gcpu);

/*--------------------------------------------------------------------------
 *
 * Context switching
 *
 *-------------------------------------------------------------------------- */

/* perform full state save before switching to another guest */
void gcpu_swap_out(guest_cpu_handle_t gcpu);

/* perform state restore after switching from another guest */
void gcpu_swap_in(const guest_cpu_handle_t gcpu);

/*--------------------------------------------------------------------------
 *
 * Change execution mode - switch to native execution mode
 *
 * This function should be called by appropriate VMCALL handler to end
 * non-native execution mode.
 * Current usage: terminate guest emulation
 *
 * Note: arguments arg1, arg2 and arg3 are not used. Added because this
 * function is registered as VMCALL handler
 *-------------------------------------------------------------------------- */
mon_status_t gcpu_return_to_native_execution(guest_cpu_handle_t gcpu,
					     address_t *,
					     address_t *,
					     address_t *);

/*--------------------------------------------------------------------------
 *
 * return TRUE if running in native (non-emulator) mode
 *
 *-------------------------------------------------------------------------- */
boolean_t gcpu_is_native_execution(guest_cpu_handle_t gcpu);

/*--------------------------------------------------------------------------
 *
 * switch to emulator. Should be used only on non-implemented events,
 * like hardware task switch.
 *
 *-------------------------------------------------------------------------- */
void gcpu_run_emulator(const guest_cpu_handle_t gcpu);

/*--------------------------------------------------------------------------
 *
 * Initialize gcpu environment for each VMEXIT
 * Must be the first gcpu call in each VMEXIT
 *
 *-------------------------------------------------------------------------- */
void gcpu_vmexit_start(const guest_cpu_handle_t gcpu);

/*--------------------------------------------------------------------------
 *
 * Resume execution.
 * never returns.
 *
 *-------------------------------------------------------------------------- */
void gcpu_resume(guest_cpu_handle_t gcpu);

/*--------------------------------------------------------------------------
 *
 * Perform single step.
 *
 *-------------------------------------------------------------------------- */
boolean_t gcpu_perform_single_step(const guest_cpu_handle_t gcpu);

/*--------------------------------------------------------------------------
 *
 * Initialize guest CPU
 *
 * Should be called only if initial GCPU state is not Wait-For-Sipi
 *-------------------------------------------------------------------------- */
void gcpu_initialize(guest_cpu_handle_t gcpu,
		     const mon_guest_cpu_startup_state_t *initial_state);

uint32_t gcpu_get_interruptibility_state_layered(const guest_cpu_handle_t gcpu,
						 vmcs_level_t level);

void gcpu_set_interruptibility_state_layered(const guest_cpu_handle_t gcpu,
					     uint32_t value,
					     vmcs_level_t level);

INLINE uint32_t gcpu_get_interruptibility_state(const guest_cpu_handle_t gcpu)
{
	return gcpu_get_interruptibility_state_layered(gcpu, VMCS_MERGED);
}

INLINE
void gcpu_set_interruptibility_state(const guest_cpu_handle_t gcpu,
				     uint32_t value)
{
	gcpu_set_interruptibility_state_layered(gcpu, value, VMCS_MERGED);
}

ia32_vmx_vmcs_guest_sleep_state_t
gcpu_get_activity_state_layered(const guest_cpu_handle_t gcpu,
				vmcs_level_t level);

void gcpu_set_activity_state_layered(guest_cpu_handle_t gcpu,
				     ia32_vmx_vmcs_guest_sleep_state_t value,
				     vmcs_level_t level);

INLINE ia32_vmx_vmcs_guest_sleep_state_t
gcpu_get_activity_state(const guest_cpu_handle_t gcpu)
{
	return gcpu_get_activity_state_layered(gcpu, VMCS_MERGED);
}

INLINE void gcpu_set_activity_state(guest_cpu_handle_t gcpu,
				    ia32_vmx_vmcs_guest_sleep_state_t value)
{
	gcpu_set_activity_state_layered(gcpu, value, VMCS_MERGED);
}

uint64_t gcpu_get_pending_debug_exceptions_layered(
	const guest_cpu_handle_t gcpu,
	vmcs_level_t level);

void gcpu_set_pending_debug_exceptions_layered(const guest_cpu_handle_t gcpu,
					       uint64_t value,
					       vmcs_level_t level);

void gcpu_set_vmenter_control_layered(const guest_cpu_handle_t gcpu,
				      vmcs_level_t level);

INLINE void gcpu_set_pending_debug_exceptions(const guest_cpu_handle_t gcpu,
					      uint64_t value)
{
	gcpu_set_pending_debug_exceptions_layered(gcpu, value, VMCS_MERGED);
}

INLINE void gcpu_set_vmenter_control(const guest_cpu_handle_t gcpu)
{
	gcpu_set_vmenter_control_layered(gcpu, VMCS_MERGED);
}

/*--------------------------------------------------------------------------
 * Guest CPU vmexits control
 *
 * request vmexits for given guest CPU
 *
 * Receives 2 bitmasks:
 * For each 1bit in mask check the corresponding request bit. If request bit
 * is 1 - request the vmexit on this bit change, else - remove the
 * previous request for this bit.
 *-------------------------------------------------------------------------- */

/* setup vmexit requests without applying - for guest.c */
void gcpu_control_setup_only(guest_cpu_handle_t gcpu,
			     const vmexit_control_t *request);

/* applies what was requested before */
void gcpu_control_apply_only(guest_cpu_handle_t gcpu);
void gcpu_control2_apply_only(guest_cpu_handle_t gcpu);

/* shortcut for single-gcpu change if gcpu is active on current host cpu */
INLINE void gcpu_control_setup(guest_cpu_handle_t gcpu,
			       const vmexit_control_t *request)
{
	gcpu_control_setup_only(gcpu, request);
	gcpu_control_apply_only(gcpu);
}

INLINE void gcpu_control2_setup(guest_cpu_handle_t gcpu,
				const vmexit_control_t *request)
{
	gcpu_control_setup_only(gcpu, request);
	gcpu_control2_apply_only(gcpu);
}

/* get VMCS object to work directly */
vmcs_object_t *mon_gcpu_get_vmcs(guest_cpu_handle_t gcpu);

vmcs_hierarchy_t *gcpu_get_vmcs_hierarchy(guest_cpu_handle_t gcpu);

vmcs_object_t *gcpu_get_vmcs_layered(guest_cpu_handle_t gcpu,
				     vmcs_level_t level);

boolean_t gcpu_is_vmcs_layered(guest_cpu_handle_t gcpu);

boolean_t gcpu_uses_host_page_tables(guest_cpu_handle_t gcpu);

/* check if emulator runs as a guest, and if so do emulator processing
 * returns TRUE if interrupt was processed */
boolean_t gcpu_process_interrupt(vector_id_t vector_id);

/* convert GVA to GPA (wrapper for page walker) */
boolean_t mon_gcpu_gva_to_gpa(guest_cpu_handle_t gcpu,
			      gva_t gva,
			      uint64_t cr3,
			      gpa_t *gpa);

/* convert GVA to HVA */
boolean_t gcpu_gva_to_hva(guest_cpu_handle_t gcpu, gva_t gva, hva_t *hva);

/* Private API for guest.c */
void gcpu_manager_init(uint16_t host_cpu_count);
guest_cpu_handle_t gcpu_allocate(virtual_cpu_id_t vcpu, guest_handle_t guest);
void mon_gcpu_physical_memory_modified(guest_cpu_handle_t gcpu);

/*
 * MSRs to be autoswapped at each vmexit/vmentry
 */

/* guest MSRs that are saved automatically at vmexit and loaded at vmentry */
typedef struct {
	ia32_vmx_msr_entry_t efer;
} mon_autoswap_msrs_t;

#define MON_AUTOSWAP_MSRS_COUNT (sizeof(mon_autoswap_msrs_t) / \
				 sizeof(ia32_vmx_msr_entry_t))

guest_level_t gcpu_get_guest_level(guest_cpu_handle_t gcpu);

void gcpu_set_next_guest_level(guest_cpu_handle_t gcpu, guest_level_t level);

void gcpu_set_xmm_reg(guest_cpu_handle_t gcpu,
		      mon_ia32_xmm_registers_t reg,
		      uint128_t value);

/*
 *   Below are placed wrappers for access functions for default case
 *   i.e. applied to merged VMCS
 */

/*--------------------------------------------------------------------------
 *
 * get/set native GP regardless of exection mode (emulator/native/etc)
 * if guest is not running natively (ex. under emulator) this will return/set
 * emulator registers and not real guest registers
 *-------------------------------------------------------------------------- */
INLINE uint64_t gcpu_get_native_gp_reg(const guest_cpu_handle_t gcpu,
				       mon_ia32_gp_registers_t reg)
{
	return gcpu_get_native_gp_reg_layered(gcpu, reg, VMCS_MERGED);
}

INLINE void gcpu_set_native_gp_reg(guest_cpu_handle_t gcpu,
				   mon_ia32_gp_registers_t reg, uint64_t value)
{
	gcpu_set_native_gp_reg_layered(gcpu, reg, value, VMCS_MERGED);
}

/*--------------------------------------------------------------------------
 *
 * Get/Set register value
 *
 *-------------------------------------------------------------------------- */
INLINE uint64_t gcpu_get_gp_reg(const guest_cpu_handle_t gcpu,
				mon_ia32_gp_registers_t reg)
{
	return gcpu_get_gp_reg_layered(gcpu, reg, VMCS_MERGED);
}

INLINE void gcpu_set_all_gp_regs(guest_cpu_handle_t gcpu, uint64_t *gp_reg)
{
	gcpu_set_all_gp_regs_internal(gcpu, gp_reg);
}

INLINE void gcpu_set_gp_reg(guest_cpu_handle_t gcpu,
			    mon_ia32_gp_registers_t reg,
			    uint64_t value)
{
	gcpu_set_gp_reg_layered(gcpu, reg, value, VMCS_MERGED);
}

/* all result pointers are optional */
INLINE void gcpu_get_segment_reg(const guest_cpu_handle_t gcpu,
				 mon_ia32_segment_registers_t reg,
				 uint16_t *selector, uint64_t *base,
				 uint32_t *limit, uint32_t *attributes)
{
	gcpu_get_segment_reg_layered(gcpu,
		reg,
		selector,
		base,
		limit,
		attributes,
		VMCS_MERGED);
}

INLINE void gcpu_set_segment_reg(guest_cpu_handle_t gcpu,
				 mon_ia32_segment_registers_t reg,
				 uint16_t selector, uint64_t base,
				 uint32_t limit, uint32_t attributes)
{
	gcpu_set_segment_reg_layered(gcpu,
		reg,
		selector,
		base,
		limit,
		attributes,
		VMCS_MERGED);
}

INLINE uint64_t gcpu_get_control_reg(const guest_cpu_handle_t gcpu,
				     mon_ia32_control_registers_t reg)
{
	return gcpu_get_control_reg_layered(gcpu, reg, VMCS_MERGED);
}

INLINE void gcpu_set_control_reg(guest_cpu_handle_t gcpu,
				 mon_ia32_control_registers_t reg,
				 uint64_t value)
{
	gcpu_set_control_reg_layered(gcpu, reg, value, VMCS_MERGED);
}

/* special case of CR registers - some bits of CR0 and CR4 may be overridden by
 * MON, so that guest will see not real values
 * all other registers return the same value as gcpu_get_control_reg()
 * valid for CR0, CR3, CR4 */
INLINE uint64_t gcpu_get_guest_visible_control_reg(
	const guest_cpu_handle_t gcpu,
	mon_ia32_control_registers_t reg)
{
	return gcpu_get_guest_visible_control_reg_layered(gcpu, reg,
		VMCS_MERGED);
}

/* valid only for CR0, CR3 and CR4
 * Contains faked values for the bits that have 1 in the mask. Those bits are
 * returned to the guest upon reading the register instead real bits */
INLINE void gcpu_set_guest_visible_control_reg(const guest_cpu_handle_t gcpu,
					       mon_ia32_control_registers_t reg,
					       uint64_t value)
{
	gcpu_set_guest_visible_control_reg_layered(gcpu, reg, value,
		VMCS_MERGED);
}

/* all result pointers are optional */
INLINE void gcpu_get_gdt_reg(const guest_cpu_handle_t gcpu,
			     uint64_t *base, uint32_t *limit)
{
	gcpu_get_gdt_reg_layered(gcpu, base, limit, VMCS_MERGED);
}

INLINE void gcpu_set_gdt_reg(const guest_cpu_handle_t gcpu,
			     uint64_t base, uint32_t limit)
{
	gcpu_set_gdt_reg_layered(gcpu, base, limit, VMCS_MERGED);
}

void gcpu_skip_guest_instruction(guest_cpu_handle_t gcpu);

/* all result pointers are optional */
INLINE void gcpu_get_idt_reg(const guest_cpu_handle_t gcpu,
			     uint64_t *base, uint32_t *limit)
{
	gcpu_get_idt_reg_layered(gcpu, base, limit, VMCS_MERGED);
}

INLINE void gcpu_set_idt_reg(const guest_cpu_handle_t gcpu,
			     uint64_t base, uint32_t limit)
{
	gcpu_set_idt_reg_layered(gcpu, base, limit, VMCS_MERGED);
}

INLINE uint64_t gcpu_get_debug_reg(const guest_cpu_handle_t gcpu,
				   mon_ia32_debug_registers_t reg)
{
	return gcpu_get_debug_reg_layered(gcpu, reg, VMCS_MERGED);
}

INLINE void gcpu_set_debug_reg(const guest_cpu_handle_t gcpu,
			       mon_ia32_debug_registers_t reg,
			       uint64_t value)
{
	gcpu_set_debug_reg_layered(gcpu, reg, value, VMCS_MERGED);
}

INLINE uint64_t gcpu_get_msr_reg(const guest_cpu_handle_t gcpu,
				 mon_ia32_model_specific_registers_t reg)
{
	return gcpu_get_msr_reg_layered(gcpu, reg, VMCS_MERGED);
}

INLINE void gcpu_set_msr_reg(guest_cpu_handle_t gcpu,
			     mon_ia32_model_specific_registers_t reg,
			     uint64_t value)
{
	gcpu_set_msr_reg_layered(gcpu, reg, value, VMCS_MERGED);
}

INLINE void mon_gcpu_set_msr_reg_by_index(guest_cpu_handle_t gcpu,
					  uint32_t msr_index, uint64_t value)
{
	gcpu_set_msr_reg_by_index_layered(gcpu, msr_index, value, VMCS_MERGED);
}

INLINE uint64_t gcpu_get_msr_reg_by_index(guest_cpu_handle_t gcpu,
					  uint32_t msr_index)
{
	return gcpu_get_msr_reg_by_index_layered(gcpu, msr_index, VMCS_MERGED);
}

typedef guest_cpu_handle_t (*func_gcpu_resume_t) (guest_cpu_handle_t);

typedef guest_cpu_handle_t (*func_gcpu_vmexit_t) (guest_cpu_handle_t gcpu,
						  uint32_t reason);

guest_cpu_handle_t gcpu_call_vmexit_function(guest_cpu_handle_t gcpu,
					     uint32_t reason);

guest_cpu_handle_t gcpu_perform_split_merge(guest_cpu_handle_t gcpu);

/* call only if gcpu_perform_split_merge() is not used!!! */
typedef struct {
	uint64_t	visible_cr0;
	uint64_t	visible_cr3;
	uint64_t	visible_cr4;
	uint64_t	efer;
} merge_orig_values_t;

void gcpu_raise_proper_events_after_level_change(guest_cpu_handle_t gcpu,
						 merge_orig_values_t *optional);

boolean_t gcpu_get_32_bit_pdpt(guest_cpu_handle_t gcpu, void *pdpt_ptr);

void gcpu_change_level0_vmexit_msr_load_list(guest_cpu_handle_t gcpu,
					     ia32_vmx_msr_entry_t *msr_list,
					     uint32_t msr_list_count);

boolean_t gcpu_is_mode_native(guest_cpu_handle_t gcpu);

void gcpu_load_segment_reg_from_gdt(guest_cpu_handle_t guest_cpu,
				    uint64_t gdt_base,
				    uint16_t selector,
				    mon_ia32_segment_registers_t reg_id);

void *gcpu_get_vmdb(guest_cpu_handle_t gcpu);
void  gcpu_set_vmdb(guest_cpu_handle_t gcpu, void *vmdb);
void *gcpu_get_timer(guest_cpu_handle_t gcpu);
void  gcpu_assign_timer(guest_cpu_handle_t gcpu, void *timer);

#endif   /* _GUEST_CPU_H_ */
