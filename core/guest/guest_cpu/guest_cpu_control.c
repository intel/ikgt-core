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

#include "guest_cpu_internal.h"
#include "vmcs_api.h"
#include "vmcs_init.h"
#include "heap.h"
#include "vmx_vmcs.h"
#include "hw_utils.h"
#include "vmexit_msr.h"
#include "vmexit_io.h"
#include "vmcall.h"
#include "mon_dbg.h"
#include "policy_manager.h"
#include "mon_api.h"
#include "unrestricted_guest.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(GUEST_CPU_CONTROL_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(GUEST_CPU_CONTROL_C, __condition)

extern mon_paging_policy_t g_pg_policy;
extern void disable_vmcs_load_save_for_msr(msr_id_t msr_index);
extern boolean_t is_cr4_osxsave_supported(void);

/**************************************************************************
 *
 * Main implementatuion idea:
 *   Count requests for each VmExit control bit. Require VmExit if at least
 *   one request is outstanding.
 *
 ***************************************************************************/

/* global static vars that indicate host CPU support for extra controls */
static boolean_t g_init_done = FALSE;
static boolean_t g_processor_ctrls2_supported = FALSE;

/* -------------------------- types --------------------------------------- */
typedef enum {
	EXCEPTIONS_POLICY_CATCH_NOTHING = 0,
	EXCEPTIONS_POLICY_CATCH_ALL,
} exceptions_policy_type_t;

/* ---------------------------- globals ----------------------------------- */

/* set bit for each fixed bit - either 0 or 1 */
#define GET_FIXED_MASK(type, mask, func)   \
	{                                  \
		type fixed1, fixed0;       \
		fixed1 = (func)(0);        \
		fixed0 = (func)((type)-1); \
		(mask) = fixed1 | ~fixed0; \
	}

/* init with minimal value */
#define GET_MINIMAL_VALUE(value, func) ((value) = (func)(0))

/* return fixed0 values */
#define GET_FIXED0(func) (func)(UINT32_ALL_ONES)

#define MAY_BE_SET1(fixed, defaul, bit) (!(fixed.bits.bit) || defaul.bits.bit)

#define APPLY_ZEROES(__value, __zeroes) ((__value) & (__zeroes))
#define APPLY_ONES(__value, __ones) ((__value) | (__ones))
#define APPLY_ZEROES_AND_ONES(__value, __zeroes, __ones) \
	APPLY_ZEROES(APPLY_ONES(__value, __ones), __zeroes)

#define GET_FINAL_SETTINGS(gcpu, field, final_mask)                         \
	(((uint64_t)(final_mask) | \
	  (gcpu)->vmexit_setup.field.minimal_1_settings)   \
	 & (gcpu)->vmexit_setup.field.minimal_0_settings)

/*----------------- Forward Declarations for Local Functions ---------------*/

static void gcpu_exceptions_settings_enforce_on_hw(guest_cpu_handle_t gcpu,
						   uint32_t zeroes,
						   uint32_t ones);
static void gcpu_exceptions_settings_restore_on_hw(guest_cpu_handle_t gcpu);
static void gcpu_proc_ctrls_enforce_on_hw(guest_cpu_handle_t gcpu,
					  uint32_t zeroes,
					  uint32_t ones);
static void gcpu_proc_ctrls_restore_on_hw(guest_cpu_handle_t gcpu);
static void gcpu_cr0_mask_enforce_on_hw(guest_cpu_handle_t gcpu,
					uint64_t zeroes,
					uint64_t ones);
static void gcpu_set_enter_ctrls_for_addons(guest_cpu_handle_t gcpu,
					    uint32_t value,
					    uint32_t bits_untouched);

/* static void gcpu_guest_cpu_mode_enforce_on_hw(guest_cpu_handle_t gcpu); *.
 *  /* static void gcpu_guest_cpu_mode_restore_on_hw(guest_cpu_handle_t gcpu); */

/* ---------------------------- internal funcs ---------------------------- */

static void set_minimal_cr0_reg_mask(gcpu_vmexit_control_field_counters_t *field)
{
	uint64_t fixed;

	GET_FIXED_MASK(uint64_t, fixed, vmcs_hw_make_compliant_cr0);

	field->minimal_1_settings = (fixed | GCPU_CR0_MON_CONTROLLED_BITS);

	if (global_policy_is_cache_dis_virtualized()) {
		field->minimal_1_settings |= CR0_CD;
	}

	field->minimal_0_settings = UINT64_ALL_ONES;
}

static void set_minimal_cr4_reg_mask(gcpu_vmexit_control_field_counters_t *field)
{
	uint64_t fixed;

	GET_FIXED_MASK(uint64_t, fixed, vmcs_hw_make_compliant_cr4);

	if (mon_is_unrestricted_guest_supported()) {
		field->minimal_1_settings = fixed | CR4_SMXE;
	} else {
		field->minimal_1_settings =
			(fixed | GCPU_CR4_MON_CONTROLLED_BITS);
	}
	field->minimal_0_settings = UINT64_ALL_ONES;

	if (is_cr4_osxsave_supported()) {
		field->minimal_1_settings = field->minimal_1_settings |
					    CR4_OSXSAVE;
	}
}

static void set_minimal_pin_ctrls(gcpu_vmexit_control_field_counters_t *field)
{
	pin_based_vm_execution_controls_t pin_ctrl, pin_ctrl_fixed;

	GET_FIXED_MASK(uint32_t, pin_ctrl_fixed.uint32,
		vmcs_hw_make_compliant_pin_based_exec_ctrl);

	GET_MINIMAL_VALUE(pin_ctrl.uint32,
		vmcs_hw_make_compliant_pin_based_exec_ctrl);

	/* do not exit on external interrupts */
	MON_ASSERT(pin_ctrl.bits.external_interrupt == 0);

	/* setup all NMIs to be processed by MON
	 * gcpu receive only virtual NMIs */
	MON_ASSERT(MAY_BE_SET1(pin_ctrl_fixed, pin_ctrl, nmi));
	pin_ctrl.bits.nmi = 1;

	MON_ASSERT(MAY_BE_SET1(pin_ctrl_fixed, pin_ctrl, virtual_nmi));
	pin_ctrl.bits.virtual_nmi = 1;

	field->minimal_1_settings = pin_ctrl.uint32;
	field->minimal_0_settings =
		GET_FIXED0(vmcs_hw_make_compliant_pin_based_exec_ctrl);
}

static void set_minimal_processor_ctrls(gcpu_vmexit_control_field_counters_t *
					field)
{
	processor_based_vm_execution_controls_t proc_ctrl, proc_ctrl_fixed;

	GET_FIXED_MASK(uint32_t, proc_ctrl_fixed.uint32,
		vmcs_hw_make_compliant_processor_based_exec_ctrl);

	GET_MINIMAL_VALUE(proc_ctrl.uint32,
		vmcs_hw_make_compliant_processor_based_exec_ctrl);

	/* do not use TSC offsetting */
	MON_ASSERT(proc_ctrl.bits.use_tsc_offsetting == 0);

	/* do not exit on halt instruction */
	MON_ASSERT(proc_ctrl.bits.hlt == 0);

	/* do not exit on invalidate page */
	MON_ASSERT(proc_ctrl.bits.invlpg == 0);

	/* do not exit on mwait */
	MON_ASSERT(proc_ctrl.bits.mwait == 0);

	/* do not exit on rdpmc instruction */
	MON_ASSERT(proc_ctrl.bits.rdpmc == 0);

	/* do not exit on rdtsc instruction */
	MON_ASSERT(proc_ctrl.bits.rdtsc == 0);

	/* do not exit on CR8 access */
	MON_ASSERT(proc_ctrl.bits.cr8_load == 0);
	MON_ASSERT(proc_ctrl.bits.cr8_store == 0);

	/* do not exit use TPR shadow */
	MON_ASSERT(proc_ctrl.bits.tpr_shadow == 0);

	/* do not exit on debug registers access */
	MON_ASSERT(proc_ctrl.bits.mov_dr == 0);

	/* do not exit on I/O ports access */
	MON_ASSERT(proc_ctrl.bits.unconditional_io == 0);
	MON_ASSERT(proc_ctrl.bits.activate_io_bitmaps == 0);

	/* do not exit on monitor instruction */
	MON_ASSERT(proc_ctrl.bits.monitor == 0);

	/* do not exit on pause instruction */
	MON_ASSERT(proc_ctrl.bits.pause == 0);

	MON_LOG(mask_anonymous, level_trace, "%s:: %d \n", __FUNCTION__,
		g_pg_policy);
	if (g_pg_policy == POL_PG_EPT) {
		proc_ctrl.bits.cr3_load = 0;
		proc_ctrl.bits.cr3_store = 0;
		/* MON_LOG(mask_anonymous, level_trace,"%s:VMEXIT BITS= %X\n",
		 * __FUNCTION__, proc_ctrl.uint32); */
	}

	/* if processor_ctrls2 may be enabled, enable them immediately
	 * to simplify processing */
	MON_ASSERT(g_processor_ctrls2_supported == (0 !=
						    MAY_BE_SET1(proc_ctrl_fixed,
							    proc_ctrl,
							    secondary_controls)));

	if (g_processor_ctrls2_supported) {
		proc_ctrl.bits.secondary_controls = 1;
	}

	field->minimal_1_settings = proc_ctrl.uint32;
	field->minimal_0_settings =
		GET_FIXED0(vmcs_hw_make_compliant_processor_based_exec_ctrl);
}

static void set_minimal_processor_ctrls2(gcpu_vmexit_control_field_counters_t *
					 field)
{
	processor_based_vm_execution_controls2_t proc_ctrl2, proc_ctrl2_fixed;

	if (!g_processor_ctrls2_supported) {
		return;
	}

	GET_FIXED_MASK(uint32_t, proc_ctrl2_fixed.uint32,
		vmcs_hw_make_compliant_processor_based_exec_ctrl2);

	GET_MINIMAL_VALUE(proc_ctrl2.uint32,
		vmcs_hw_make_compliant_processor_based_exec_ctrl2);

	/*
	 * enable rdtscp instruction if CPUID.80000001H.EDX[27] reports it is
	 * supported
	 * if "Enable RDTSCP" is 0, execution of RDTSCP in non-root mode will
	 * trigger #UD
	 * Notes:
	 * 1. Currently, "RDTSC existing" and "use TSC Offsetting" both are ZEROs,
	 * since mon doesn't virtualize TSC
	 * 2. Current setting makes RDTSCP operate as normally, and no vmexits
	 * happen. Besides, mon doesn't use/modify IA32_TSC_AUX.
	 * 3. If we want to add virtual TSC and support, and virtualization of
	 * IA32_TSC_AUX, current settings must be changed per request.
	 */
	if (proc_ctrl2.bits.enable_rdtscp == 0) {
		/* set enable_rdtscp bit if rdtscp is supported */
		if (is_rdtscp_supported()) {
			proc_ctrl2.bits.enable_rdtscp = 1;
		}
	}

	/*
	 * INVPCID. Behavior of the INVPCID instruction is determined first by
	 * the setting of the enable INVPCID ? VM - execution control : ?
	 * If the enable INVPCID ? VM - execution control is 0, INVPCID causes an
	 * invalid-opcode exception (#UD).
	 * If the enable INVPCID ? VM - execution control is 1, treatment is based
	 * on the setting of the INVLPG exiting ? VM - execution control :
	 *   1) If the INVLPG exiting ? VM - execution control is 0, INVPCID
	 *      operates normally. (this setting is selected)
	 *   2) If the INVLPG exiting ? VM - execution control is 1, INVPCID
	 * causes a VM exit. */
	if (proc_ctrl2.bits.enable_invpcid == 0) {
		/* set enable_invpcid bit if INVPCID is supported */
		if (is_invpcid_supported()) {
			proc_ctrl2.bits.enable_invpcid = 1;
		}
	}

	field->minimal_1_settings = proc_ctrl2.uint32;
	field->minimal_0_settings =
		GET_FIXED0(vmcs_hw_make_compliant_processor_based_exec_ctrl2);
}

static void set_minimal_exceptions_map(gcpu_vmexit_control_field_counters_t *
				       field)
{
	ia32_vmcs_exception_bitmap_t exceptions;

	exceptions.uint32 = 0;

	/* Machine Check: let guest IDT handle the MCE unless mon has special
	 * concern exceptions.bits.MC = 1; */

	/* Page Faults
	 * exceptions.bits.PF = 1; not required for EPT. for VTLB/FPT should be
	 * enabled explicitely */

	field->minimal_1_settings = exceptions.uint32;
	field->minimal_0_settings = UINT64_ALL_ONES;
}

static void set_minimal_exit_ctrls(gcpu_vmexit_control_field_counters_t *field)
{
	vmexit_controls_t ctrl, ctrl_fixed;

	GET_FIXED_MASK(uint32_t, ctrl_fixed.uint32,
		vmcs_hw_make_compliant_vm_exit_ctrl);

	GET_MINIMAL_VALUE(ctrl.uint32, vmcs_hw_make_compliant_vm_exit_ctrl);

	/* do not acknowledge interrupts on exit */
	MON_ASSERT(ctrl.bits.acknowledge_interrupt_on_exit == 0);

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, save_cr0_and_cr4));
	ctrl.bits.save_cr0_and_cr4 = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, save_cr3));
	ctrl.bits.save_cr3 = 1;

	if (MAY_BE_SET1(ctrl_fixed, ctrl, save_debug_controls)) {
		ctrl.bits.save_debug_controls = 1;
	}

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, save_segment_registers));
	ctrl.bits.save_segment_registers = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, save_esp_eip_eflags));
	ctrl.bits.save_esp_eip_eflags = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl,
			save_pending_debug_exceptions));
	ctrl.bits.save_pending_debug_exceptions = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl,
			save_interruptibility_information));
	ctrl.bits.save_interruptibility_information = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, save_activity_state));
	ctrl.bits.save_activity_state = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, save_working_vmcs_pointer));
	ctrl.bits.save_working_vmcs_pointer = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_cr0_and_cr4));
	ctrl.bits.load_cr0_and_cr4 = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_cr3));
	ctrl.bits.load_cr3 = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_segment_registers));
	ctrl.bits.load_segment_registers = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_esp_eip));
	ctrl.bits.load_esp_eip = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_esp_eip));
	ctrl.bits.load_esp_eip = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, save_sys_enter_msrs));

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_sys_enter_msrs));

	if (MAY_BE_SET1(ctrl_fixed, ctrl, save_efer)) {
		ctrl.bits.save_efer = 1;
	}

	if (MAY_BE_SET1(ctrl_fixed, ctrl, load_efer)) {
		ctrl.bits.load_efer = 1;
	}

	if (MAY_BE_SET1(ctrl_fixed, ctrl, load_ia32_perf_global_ctrl)) {
		ctrl.bits.load_ia32_perf_global_ctrl = 1;
	}

	if (MAY_BE_SET1(ctrl_fixed, ctrl, save_pat)) {
		ctrl.bits.save_pat = 1;
	}

	if (MAY_BE_SET1(ctrl_fixed, ctrl, load_pat)) {
		ctrl.bits.load_pat = 1;
	}

	field->minimal_1_settings = ctrl.uint32;
	field->minimal_0_settings = GET_FIXED0(
		vmcs_hw_make_compliant_vm_exit_ctrl);
}

static void set_minimal_entry_ctrls(gcpu_vmexit_control_field_counters_t *field)
{
	vmentry_controls_t ctrl, ctrl_fixed;

	GET_FIXED_MASK(uint32_t, ctrl_fixed.uint32,
		vmcs_hw_make_compliant_vm_entry_ctrl);

	GET_MINIMAL_VALUE(ctrl.uint32, vmcs_hw_make_compliant_vm_entry_ctrl);

	/* we are out of SMM */
	MON_ASSERT(ctrl.bits.entry_to_smm == 0);
	MON_ASSERT(ctrl.bits.tear_down_smm_monitor == 0);

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_cr0_and_cr4));
	ctrl.bits.load_cr0_and_cr4 = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_cr3));
	ctrl.bits.load_cr3 = 1;

	if (MAY_BE_SET1(ctrl_fixed, ctrl, load_debug_controls)) {
		ctrl.bits.load_debug_controls = 1;
	}

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_segment_registers));
	ctrl.bits.load_segment_registers = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_esp_eip_eflags));
	ctrl.bits.load_esp_eip_eflags = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl,
			load_pending_debug_exceptions));
	ctrl.bits.load_pending_debug_exceptions = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl,
			load_interruptibility_information));
	ctrl.bits.load_interruptibility_information = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_activity_state));
	ctrl.bits.load_activity_state = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_working_vmcs_pointer));
	ctrl.bits.load_working_vmcs_pointer = 1;

	MON_ASSERT(MAY_BE_SET1(ctrl_fixed, ctrl, load_sys_enter_msrs));

	if (MAY_BE_SET1(ctrl_fixed, ctrl, load_efer)) {
		ctrl.bits.load_efer = 1;
	}

	if (MAY_BE_SET1(ctrl_fixed, ctrl, load_pat)) {
		ctrl.bits.load_pat = 1;
	}

	field->minimal_1_settings = ctrl.uint32;
	field->minimal_0_settings =
		GET_FIXED0(vmcs_hw_make_compliant_vm_entry_ctrl);
}

static void init_minimal_controls(guest_cpu_handle_t gcpu)
{
	/* perform init */
	if (g_init_done == FALSE) {
		g_init_done = TRUE;
		g_processor_ctrls2_supported =
			mon_vmcs_hw_get_vmx_constraints()->
			processor_based_exec_ctrl2_supported;
	}

	set_minimal_cr0_reg_mask(&(gcpu->vmexit_setup.cr0));
	set_minimal_cr4_reg_mask(&(gcpu->vmexit_setup.cr4));
	set_minimal_pin_ctrls(&(gcpu->vmexit_setup.pin_ctrls));
	set_minimal_processor_ctrls(&(gcpu->vmexit_setup.processor_ctrls));
	set_minimal_processor_ctrls2(&(gcpu->vmexit_setup.processor_ctrls2));
	set_minimal_exceptions_map(&(gcpu->vmexit_setup.exceptions_ctrls));
	set_minimal_entry_ctrls(&(gcpu->vmexit_setup.vm_entry_ctrls));
	set_minimal_exit_ctrls(&(gcpu->vmexit_setup.vm_exit_ctrls));
}

/*
 * Get 64bit mask + flags set. For each 1-bit in mask consult flags bit.
 * If flags bit is 1 - increase count, esle - decrease count
 * Return bit set with 1bit for each non-zero counter */
static uint64_t gcpu_update_control_counters(uint64_t flags,
					     uint64_t mask,
					     gcpu_vmexit_control_field_counters_t
					     *counters)
{
	uint32_t idx;

	while (mask) {
		idx = (uint32_t)-1;

		hw_scan_bit_forward64(&idx, mask);

		MON_ASSERT(idx < 64);
		BIT_CLR64(mask, idx);

		if (1 == BIT_GET64(flags, idx)) {
			if (0 == counters->counters[idx]) {
				BIT_SET64(counters->bit_field, idx);
			}

			MON_ASSERT(counters->counters[idx] < 255);
			++(counters->counters[idx]);
		} else {
			MON_ASSERT(counters->counters[idx] > 0);
			--(counters->counters[idx]);

			if (0 == counters->counters[idx]) {
				BIT_CLR64(counters->bit_field, idx);
			}
		}
	}

	return counters->bit_field;
}

INLINE uint64_t calculate_cr0_reg_mask(guest_cpu_handle_t gcpu,
				       uint64_t request, uint64_t bitmask)
{
	uint64_t final_mask;

	final_mask = gcpu_update_control_counters(request, bitmask,
		&(gcpu->vmexit_setup.cr0));
	return GET_FINAL_SETTINGS(gcpu, cr0, final_mask);
}

void gcpu_set_cr0_reg_mask_layered(guest_cpu_handle_t gcpu, vmcs_level_t level,
				   uint64_t value)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	if (mon_vmcs_read(vmcs, VMCS_CR0_MASK) != value) {
		mon_vmcs_write(vmcs, VMCS_CR0_MASK, value);
	}
}

uint64_t gcpu_get_cr0_reg_mask_layered(guest_cpu_handle_t gcpu,
				       vmcs_level_t level)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	return mon_vmcs_read(vmcs, VMCS_CR0_MASK);
}

INLINE uint64_t calculate_cr4_reg_mask(guest_cpu_handle_t gcpu,
				       uint64_t request, uint64_t bitmask)
{
	uint64_t final_mask;

	final_mask = gcpu_update_control_counters(request, bitmask,
		&(gcpu->vmexit_setup.cr4));

	return GET_FINAL_SETTINGS(gcpu, cr4, final_mask);
}

void gcpu_set_cr4_reg_mask_layered(guest_cpu_handle_t gcpu,
				   vmcs_level_t level, uint64_t value)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	if (mon_vmcs_read(vmcs, VMCS_CR4_MASK) != value) {
		mon_vmcs_write(vmcs, VMCS_CR4_MASK, value);
	}
}

uint64_t gcpu_get_cr4_reg_mask_layered(guest_cpu_handle_t gcpu,
				       vmcs_level_t level)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	return mon_vmcs_read(vmcs, VMCS_CR4_MASK);
}

INLINE uint32_t calculate_pin_ctrls(guest_cpu_handle_t gcpu,
				    uint32_t request, uint32_t bitmask)
{
	uint32_t final_mask;

	final_mask =
		(uint32_t)gcpu_update_control_counters(request, bitmask,
			&(gcpu->vmexit_setup.pin_ctrls));

	return (uint32_t)GET_FINAL_SETTINGS(gcpu, pin_ctrls, final_mask);
}

void gcpu_set_pin_ctrls_layered(guest_cpu_handle_t gcpu,
				vmcs_level_t level, uint64_t value)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	if (mon_vmcs_read(vmcs, VMCS_CONTROL_VECTOR_PIN_EVENTS) != value) {
		mon_vmcs_write(vmcs, VMCS_CONTROL_VECTOR_PIN_EVENTS, value);
	}
}

uint64_t gcpu_get_pin_ctrls_layered(guest_cpu_handle_t gcpu, vmcs_level_t level)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	return mon_vmcs_read(vmcs, VMCS_CONTROL_VECTOR_PIN_EVENTS);
}

static uint32_t calculate_processor_ctrls(guest_cpu_handle_t gcpu,
					  uint32_t request, uint32_t bitmask)
{
	uint32_t final_mask;

	final_mask =
		(uint32_t)gcpu_update_control_counters(request, bitmask,
			&(gcpu->
			  vmexit_setup.processor_ctrls));

	return (uint32_t)GET_FINAL_SETTINGS(gcpu, processor_ctrls, final_mask);
}

void gcpu_set_processor_ctrls_layered(guest_cpu_handle_t gcpu,
				      vmcs_level_t level,
				      uint64_t value)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);
	uint64_t proc_control_temp;

	MON_ASSERT(vmcs);

	proc_control_temp = mon_vmcs_read(vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);

	if (proc_control_temp != value) {
		mon_vmcs_write(vmcs, VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS,
			(value & ~0x8000000) | (proc_control_temp & 0x8000000));
	}
}

uint64_t gcpu_get_processor_ctrls_layered(guest_cpu_handle_t gcpu,
					  vmcs_level_t level)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	return mon_vmcs_read(vmcs, VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
}

static uint32_t calculate_processor_ctrls2(guest_cpu_handle_t gcpu,
					   uint32_t request, uint32_t bitmask)
{
	uint32_t final_mask;

	MON_ASSERT(g_processor_ctrls2_supported == TRUE);

	final_mask =
		(uint32_t)gcpu_update_control_counters(request, bitmask,
			&(gcpu->
			  vmexit_setup.processor_ctrls2));

	return (uint32_t)GET_FINAL_SETTINGS(gcpu, processor_ctrls2, final_mask);
}

void gcpu_set_processor_ctrls2_layered(guest_cpu_handle_t gcpu,
				       vmcs_level_t level,
				       uint64_t value)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	MON_ASSERT(g_processor_ctrls2_supported == TRUE);

	if (mon_vmcs_read(vmcs,
		    VMCS_CONTROL2_VECTOR_PROCESSOR_EVENTS) != value) {
		mon_vmcs_write(vmcs,
			VMCS_CONTROL2_VECTOR_PROCESSOR_EVENTS,
			value);
	}
}

uint64_t gcpu_get_processor_ctrls2_layered(guest_cpu_handle_t gcpu,
					   vmcs_level_t level)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	MON_ASSERT(g_processor_ctrls2_supported == TRUE);

	return mon_vmcs_read(vmcs, VMCS_CONTROL2_VECTOR_PROCESSOR_EVENTS);
}

INLINE uint32_t calculate_exceptions_map(guest_cpu_handle_t gcpu,
					 uint32_t request, uint32_t bitmask,
					 exceptions_policy_type_t *pf_policy)
{
	ia32_vmcs_exception_bitmap_t exceptions;

	MON_ASSERT(pf_policy);

	exceptions.uint32 = (uint32_t)gcpu_update_control_counters(request,
		bitmask,
		&(gcpu->vmexit_setup.exceptions_ctrls));

	*pf_policy = (exceptions.bits.pf) ?
		     EXCEPTIONS_POLICY_CATCH_ALL :
		     EXCEPTIONS_POLICY_CATCH_NOTHING;

	return (uint32_t)GET_FINAL_SETTINGS(gcpu, exceptions_ctrls,
		exceptions.uint32);
}

void gcpu_set_exceptions_map_layered(guest_cpu_handle_t gcpu,
				     vmcs_level_t level,
				     uint64_t value)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	if (mon_vmcs_read(vmcs, VMCS_EXCEPTION_BITMAP) != value) {
		mon_vmcs_write(vmcs, VMCS_EXCEPTION_BITMAP, value);
	}
}

void gcpu_get_pf_error_code_mask_and_match_layered(guest_cpu_handle_t gcpu,
						   vmcs_level_t level,
						   uint32_t *pf_mask,
						   uint32_t *pf_match)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	*pf_mask =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_PAGE_FAULT_ERROR_CODE_MASK);
	*pf_match = (uint32_t)mon_vmcs_read(vmcs,
		VMCS_PAGE_FAULT_ERROR_CODE_MATCH);
}

void gcpu_set_pf_error_code_mask_and_match_layered(guest_cpu_handle_t gcpu,
						   vmcs_level_t level,
						   uint32_t pf_mask,
						   uint32_t pf_match)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	mon_vmcs_write(vmcs, VMCS_PAGE_FAULT_ERROR_CODE_MASK, pf_mask);
	mon_vmcs_write(vmcs, VMCS_PAGE_FAULT_ERROR_CODE_MATCH, pf_match);
}

uint64_t gcpu_get_exceptions_map_layered(guest_cpu_handle_t gcpu,
					 vmcs_level_t level)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	return mon_vmcs_read(vmcs, VMCS_EXCEPTION_BITMAP);
}

INLINE uint32_t calculate_exit_ctrls(guest_cpu_handle_t gcpu,
				     uint32_t request, uint32_t bitmask)
{
	uint32_t final_mask;

	final_mask =
		(uint32_t)gcpu_update_control_counters(request, bitmask,
			&(gcpu->
			  vmexit_setup.vm_exit_ctrls));

	return (uint32_t)GET_FINAL_SETTINGS(gcpu, vm_exit_ctrls, final_mask);
}

void gcpu_set_exit_ctrls_layered(guest_cpu_handle_t gcpu,
				 vmcs_level_t level, uint32_t value)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	if (mon_vmcs_read(vmcs, VMCS_EXIT_CONTROL_VECTOR) != value) {
		mon_vmcs_write(vmcs, VMCS_EXIT_CONTROL_VECTOR, value);
	}
}

uint32_t gcpu_get_exit_ctrls_layered(guest_cpu_handle_t gcpu,
				     vmcs_level_t level)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	return (uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_CONTROL_VECTOR);
}

INLINE uint32_t calculate_enter_ctrls(guest_cpu_handle_t gcpu,
				      uint32_t request, uint32_t bitmask)
{
	uint32_t final_mask;

	final_mask =
		(uint32_t)gcpu_update_control_counters(request, bitmask,
			&(gcpu->
			  vmexit_setup.vm_entry_ctrls));

	return (uint32_t)GET_FINAL_SETTINGS(gcpu, vm_entry_ctrls, final_mask);
}

void gcpu_set_enter_ctrls_layered(guest_cpu_handle_t gcpu,
				  vmcs_level_t level, uint32_t value)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	if (mon_vmcs_read(vmcs, VMCS_ENTER_CONTROL_VECTOR) != value) {
		mon_vmcs_write(vmcs, VMCS_ENTER_CONTROL_VECTOR, value);
	}
}

static void gcpu_set_enter_ctrls_for_addons(guest_cpu_handle_t gcpu,
					    uint32_t value,
					    uint32_t bits_untouched)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, VMCS_LEVEL_0);

	MON_ASSERT(vmcs);
	vmcs_update(vmcs, VMCS_ENTER_CONTROL_VECTOR, value, ~bits_untouched);
}

uint32_t gcpu_get_enter_ctrls_layered(guest_cpu_handle_t gcpu,
				      vmcs_level_t level)
{
	vmcs_object_t *vmcs = gcpu_get_vmcs_layered(gcpu, level);

	MON_ASSERT(vmcs);

	return (uint32_t)mon_vmcs_read(vmcs, VMCS_ENTER_CONTROL_VECTOR);
}

static void request_vmexit_on_cr0(guest_cpu_handle_t gcpu,
				  uint64_t bit_request, uint64_t bit_mask)
{
	uint64_t cr0_mask;

	vmcs_object_t *vmcs;
	uint64_t cr0_value, cr0_read_shadow_value;

	cr0_mask = calculate_cr0_reg_mask(gcpu, bit_request, bit_mask);
	gcpu_set_cr0_reg_mask_layered(gcpu, VMCS_LEVEL_0, cr0_mask);

	vmcs = mon_gcpu_get_vmcs(gcpu);
	cr0_value = mon_vmcs_read(vmcs, VMCS_GUEST_CR0);
	cr0_read_shadow_value = mon_vmcs_read(vmcs, VMCS_CR0_READ_SHADOW);

	/* Clear the mask bits that it has been set in cr0 minimal_1_settings,
	 * since these bits are controlled by the host. */
	cr0_mask = cr0_mask & ~(gcpu)->vmexit_setup.cr0.minimal_1_settings;

	/* 1. Keep the original shadow bit corresponding the zero bit in the
	 * cr0_mask.
	 * 2. Update the shadow bit based on the cr0 value correspoinding the
	 * set bit in the cr0_mask. */
	mon_vmcs_write(vmcs, VMCS_CR0_READ_SHADOW,
		(cr0_read_shadow_value & ~cr0_mask)
		| (cr0_value & cr0_mask));
}

static void request_vmexit_on_cr4(guest_cpu_handle_t gcpu,
				  uint64_t bit_request, uint64_t bit_mask)
{
	uint64_t cr4_mask;

	vmcs_object_t *vmcs;
	uint64_t cr4_value, cr4_read_shadow_value;

	cr4_mask = calculate_cr4_reg_mask(gcpu, bit_request, bit_mask);
	gcpu_set_cr4_reg_mask_layered(gcpu, VMCS_LEVEL_0, cr4_mask);

	vmcs = mon_gcpu_get_vmcs(gcpu);
	cr4_value = mon_vmcs_read(vmcs, VMCS_GUEST_CR4);
	cr4_read_shadow_value = mon_vmcs_read(vmcs, VMCS_CR4_READ_SHADOW);

	/* Clear the mask bits that it has been set in cr4 minimal_1_settings,
	 * since these bits are controlled by the host. */
	cr4_mask = cr4_mask & ~(gcpu)->vmexit_setup.cr4.minimal_1_settings;

	/* 1. Keep the original shadow bit corresponding the zero bit in the
	 * cr4_mask.
	 * 2. Update the shadow bit based on the cr4 value correspoinding the
	 * set bit in the cr4_mask. */
	mon_vmcs_write(vmcs, VMCS_CR4_READ_SHADOW,
		(cr4_read_shadow_value & ~cr4_mask)
		| (cr4_value & cr4_mask));
}

static void update_pfs_setup(guest_cpu_handle_t gcpu,
			     exceptions_policy_type_t policy)
{
	ia32_vmcs_exception_bitmap_t exceptions;

	/* setup page faults */

	exceptions.uint32 =
		(uint32_t)gcpu_get_exceptions_map_layered(gcpu, VMCS_LEVEL_0);
	switch (policy) {
	case EXCEPTIONS_POLICY_CATCH_NOTHING:
		/* do not exit on Page Faults at all */
		gcpu_set_pf_error_code_mask_and_match_layered(gcpu,
			VMCS_LEVEL_0,
			0,
			((exceptions.bits.pf) ?
			 ((uint32_t)-1) : 0));
		break;

	case EXCEPTIONS_POLICY_CATCH_ALL:
		/* do exit on all Page Faults */
		gcpu_set_pf_error_code_mask_and_match_layered(gcpu,
			VMCS_LEVEL_0,
			0,
			((exceptions.bits.pf) ?
			 0 : ((uint32_t)-1)));
		break;

	default:
		MON_LOG(mask_anonymous, level_trace,
			"update_pfs_setup: Unknown policy type: %d\n", policy);
		MON_ASSERT(FALSE);
	}
}

static void request_vmexit_on_exceptions(guest_cpu_handle_t gcpu,
					 uint32_t bit_request,
					 uint32_t bit_mask)
{
	uint32_t except_map;
	exceptions_policy_type_t pf_policy;

	except_map =
		calculate_exceptions_map(gcpu, bit_request, bit_mask,
			&pf_policy);
	gcpu_set_exceptions_map_layered(gcpu, VMCS_LEVEL_0, except_map);
	update_pfs_setup(gcpu, pf_policy);
}

static void request_vmexit_on_pin_ctrls(guest_cpu_handle_t gcpu,
					uint32_t bit_request, uint32_t bit_mask)
{
	uint32_t pin_ctrls;

	pin_ctrls = calculate_pin_ctrls(gcpu, bit_request, bit_mask);
	gcpu_set_pin_ctrls_layered(gcpu, VMCS_LEVEL_0, pin_ctrls);
}

static void request_vmexit_on_proc_ctrls(guest_cpu_handle_t gcpu,
					 uint32_t bit_request,
					 uint32_t bit_mask)
{
	uint32_t proc_ctrls;

	proc_ctrls = calculate_processor_ctrls(gcpu, bit_request, bit_mask);
	gcpu_set_processor_ctrls_layered(gcpu, VMCS_LEVEL_0, proc_ctrls);
}

static void request_vmexit_on_proc_ctrls2(guest_cpu_handle_t gcpu,
					  uint32_t bit_request,
					  uint32_t bit_mask)
{
	uint32_t proc_ctrls2;

	if (g_processor_ctrls2_supported) {
		proc_ctrls2 = calculate_processor_ctrls2(gcpu,
			bit_request,
			bit_mask);
		gcpu_set_processor_ctrls2_layered(gcpu,
			VMCS_LEVEL_0,
			proc_ctrls2);
	}
}

static void request_vmexit_on_vm_enter_ctrls(guest_cpu_handle_t gcpu,
					     uint32_t bit_request,
					     uint32_t bit_mask)
{
	uint32_t vm_enter_ctrls;
	vmentry_controls_t dont_touch;

	/* Do not change IA32e Guest mode here. It is changed as part of EFER!! */
	dont_touch.uint32 = 0;
	dont_touch.bits.ia32e_mode_guest = 1;

	vm_enter_ctrls = calculate_enter_ctrls(gcpu, bit_request, bit_mask);
	gcpu_set_enter_ctrls_for_addons(gcpu, vm_enter_ctrls,
		dont_touch.uint32);
}

static void request_vmexit_on_vm_exit_ctrls(guest_cpu_handle_t gcpu,
					    uint32_t bit_request,
					    uint32_t bit_mask)
{
	uint32_t vm_exit_ctrls;

	vm_exit_ctrls = calculate_exit_ctrls(gcpu, bit_request, bit_mask);
	gcpu_set_exit_ctrls_layered(gcpu, VMCS_LEVEL_0, vm_exit_ctrls);
}

static void gcpu_apply_ctrols2(guest_cpu_handle_t gcpu)
{
	request_vmexit_on_proc_ctrls2(gcpu, 0, 0);
}

static void gcpu_apply_all(guest_cpu_handle_t gcpu)
{
	request_vmexit_on_pin_ctrls(gcpu, 0, 0);
	request_vmexit_on_proc_ctrls(gcpu, 0, 0);
	request_vmexit_on_proc_ctrls2(gcpu, 0, 0);
	request_vmexit_on_exceptions(gcpu, 0, 0);
	request_vmexit_on_vm_exit_ctrls(gcpu, 0, 0);
	request_vmexit_on_vm_enter_ctrls(gcpu, 0, 0);
	request_vmexit_on_cr0(gcpu, 0, 0);
	request_vmexit_on_cr4(gcpu, 0, 0);
}

/*
 * Setup minimal controls for Guest CPU
 */
static void gcpu_minimal_controls(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	uint32_t idx;
	const vmcs_hw_constraints_t *vmx_constraints =
		mon_vmcs_hw_get_vmx_constraints();

	MON_ASSERT(vmcs);

	init_minimal_controls(gcpu);
	gcpu_apply_all(gcpu);

	/*
	 * Disable CR3 Target Values by setting the count to 0
	 *
	 *
	 * Disable CR3 Target Values by setting the count to 0 and all the values
	 * to 0xFFFFFFFF
	 */
	mon_vmcs_write(vmcs, VMCS_CR3_TARGET_COUNT, 0);
	for (idx = 0; idx < vmx_constraints->number_of_cr3_target_values; ++idx)
		mon_vmcs_write(vmcs, (vmcs_field_t)VMCS_CR3_TARGET_VALUE(idx),
			UINT64_ALL_ONES);

	/*
	 * Set additional required fields
	 */
	mon_vmcs_write(vmcs, VMCS_GUEST_WORKING_VMCS_PTR, UINT64_ALL_ONES);

	mon_vmcs_write(vmcs, VMCS_GUEST_SYSENTER_CS,
		hw_read_msr(IA32_MSR_SYSENTER_CS));
	mon_vmcs_write(vmcs, VMCS_GUEST_SYSENTER_ESP,
		hw_read_msr(IA32_MSR_SYSENTER_ESP));
	mon_vmcs_write(vmcs, VMCS_GUEST_SYSENTER_EIP,
		hw_read_msr(IA32_MSR_SYSENTER_EIP));
	mon_vmcs_write(vmcs, VMCS_GUEST_IA32_PERF_GLOBAL_CTRL,
		hw_read_msr(IA32_MSR_PERF_GLOBAL_CTRL));
}

/* ---------------------------- APIs ------------------------------------- */

/*------------------------------------------------------------------------
 *
 * Apply default policy to gcpu
 *
 *------------------------------------------------------------------------ */
void guest_cpu_control_setup(guest_cpu_handle_t gcpu)
{
	MON_ASSERT(gcpu);

	lock_initialize(&(gcpu->vmexit_setup.lock));

	gcpu_minimal_controls(gcpu);

	msr_vmexit_activate(gcpu);
	io_vmexit_activate(gcpu);
}

void gcpu_temp_exceptions_setup(guest_cpu_handle_t gcpu,
				gcpu_temp_exceptions_setup_t action)
{
	/* TODO: Rewrite!!! */
	/* TODO: THIS WILL NOT WORK */
	MON_ASSERT(FALSE);

	switch (action) {
	case GCPU_TEMP_EXIT_ON_INTR_UNBLOCK:
	{
		processor_based_vm_execution_controls_t proc_ctrl;

		proc_ctrl.uint32 = 0;
		proc_ctrl.bits.virtual_interrupt = 1;

		request_vmexit_on_proc_ctrls(gcpu, 0, 0);
	}
	break;

	case GCPU_TEMP_NO_EXIT_ON_INTR_UNBLOCK:
	{
		processor_based_vm_execution_controls_t proc_ctrl;

		proc_ctrl.uint32 = 0;
		proc_ctrl.bits.virtual_interrupt = 1;

		request_vmexit_on_proc_ctrls(gcpu, 0, 0);
	}
	break;

	default:
		MON_LOG(mask_anonymous,
			level_trace,
			"Unknown GUEST_TEMP_EXCEPTIONS_SETUP action: %d\n",
			action);
		MON_DEADLOOP();
	}
}

void gcpu_control_setup_only(guest_cpu_handle_t gcpu,
			     const vmexit_control_t *request)
{
	MON_ASSERT(gcpu);
	MON_ASSERT(request);

	lock_acquire(&(gcpu->vmexit_setup.lock));

	if (request->cr0.bit_mask != 0) {
		gcpu_update_control_counters(request->cr0.bit_request,
			request->cr0.bit_mask,
			&(gcpu->vmexit_setup.cr0));
	}

	if (request->cr4.bit_mask != 0) {
		gcpu_update_control_counters(request->cr4.bit_request,
			request->cr4.bit_mask,
			&(gcpu->vmexit_setup.cr4));
	}

	if (request->exceptions.bit_mask != 0) {
		gcpu_update_control_counters(request->exceptions.bit_request,
			request->exceptions.bit_mask,
			&(gcpu->vmexit_setup.exceptions_ctrls));
	}

	if (request->pin_ctrls.bit_mask != 0) {
		gcpu_update_control_counters(request->pin_ctrls.bit_request,
			request->pin_ctrls.bit_mask,
			&(gcpu->vmexit_setup.pin_ctrls));
	}

	if (request->proc_ctrls.bit_mask != 0) {
		gcpu_update_control_counters(request->proc_ctrls.bit_request,
			request->proc_ctrls.bit_mask,
			&(gcpu->vmexit_setup.processor_ctrls));
	}

	if (request->proc_ctrls2.bit_mask != 0) {
		MON_ASSERT(g_processor_ctrls2_supported == TRUE);

		gcpu_update_control_counters(request->proc_ctrls2.bit_request,
			request->proc_ctrls2.bit_mask,
			&(gcpu->vmexit_setup.processor_ctrls2));
	}

	if (request->vm_enter_ctrls.bit_mask != 0) {
		gcpu_update_control_counters(
			request->vm_enter_ctrls.bit_request,
			request->vm_enter_ctrls.bit_mask,
			&(gcpu->vmexit_setup.vm_entry_ctrls));
	}

	if (request->vm_exit_ctrls.bit_mask != 0) {
		gcpu_update_control_counters(request->vm_exit_ctrls.bit_request,
			request->vm_exit_ctrls.bit_mask,
			&(gcpu->vmexit_setup.vm_exit_ctrls));
	}

	lock_release(&(gcpu->vmexit_setup.lock));
}

void gcpu_control_apply_only(guest_cpu_handle_t gcpu)
{
	lock_acquire(&(gcpu->vmexit_setup.lock));
	gcpu_apply_all(gcpu);
	lock_release(&(gcpu->vmexit_setup.lock));
}

void gcpu_control2_apply_only(guest_cpu_handle_t gcpu)
{
	lock_acquire(&(gcpu->vmexit_setup.lock));
	gcpu_apply_ctrols2(gcpu);
	lock_release(&(gcpu->vmexit_setup.lock));
}

boolean_t gcpu_cr3_virtualized(guest_cpu_handle_t gcpu)
{
	processor_based_vm_execution_controls_t proc_ctrl;

	proc_ctrl.uint32 =
		(uint32_t)(gcpu->vmexit_setup.processor_ctrls.bit_field);
	return proc_ctrl.bits.cr3_store && proc_ctrl.bits.cr3_load;
}

/*
 *   Enforce settings on hardware VMCS only
 *   these changes are not reflected in vmcs#0
 */
void gcpu_enforce_settings_on_hardware(guest_cpu_handle_t gcpu,
				       gcpu_temp_exceptions_setup_t action)
{
	switch (action) {
	case GCPU_TEMP_EXCEPTIONS_EXIT_ON_ALL:
		/* enforce all exceptions vmexit */
		gcpu_exceptions_settings_enforce_on_hw(gcpu, UINT32_ALL_ONES,
			UINT32_ALL_ONES);
		break;

	case GCPU_TEMP_EXIT_ON_PF_AND_CR3:
	{
		processor_based_vm_execution_controls_t proc_ctrl;
		ia32_vmcs_exception_bitmap_t exceptions;

		/* enforce all PF vmexits */
		exceptions.uint32 = 0;
		exceptions.bits.pf = 1;
		gcpu_exceptions_settings_enforce_on_hw(gcpu, UINT32_ALL_ONES,
			exceptions.uint32);

		/* enforce CR3 access vmexit */
		proc_ctrl.uint32 = 0;
		proc_ctrl.bits.cr3_load = 1;
		proc_ctrl.bits.cr3_store = 1;
		gcpu_proc_ctrls_enforce_on_hw(gcpu, UINT32_ALL_ONES,
			proc_ctrl.uint32);
	}
	break;

	case GCPU_TEMP_EXCEPTIONS_RESTORE_ALL:
		/* reset to normal exceptions vmexit */
		gcpu_exceptions_settings_restore_on_hw(gcpu);
		break;

	case GCPU_TEMP_RESTORE_PF_AND_CR3:
		/* reset to normal exceptions vmexit */
		gcpu_exceptions_settings_restore_on_hw(gcpu);
		/* reset to normal CR3 vmexits */
		gcpu_proc_ctrls_restore_on_hw(gcpu);
		break;

	case GCPU_TEMP_CR0_NO_EXIT_ON_WP:
		/* do not vmexit when guest changes CR0.WP bit */
		gcpu_cr0_mask_enforce_on_hw(gcpu,
			BITMAP_GET64(UINT64_ALL_ONES, ~CR0_WP), /* clr CR0_WP bit only */
			0);                                     /* not set requirements */
		break;

	case GCPU_TEMP_CR0_RESTORE_WP:
		/* do vmexit when guest changes CR0.WP bit */
		gcpu_cr0_mask_enforce_on_hw(gcpu,
			UINT64_ALL_ONES,                        /* no clr requirements */
			CR0_WP);                                /* set CR0_WP bit only */
		break;

	default:
		MON_LOG(mask_anonymous,
			level_trace,
			"Unknown GUEST_TEMP_EXCEPTIONS_SETUP action: %d\n",
			action);
		MON_DEADLOOP();
	}
}

static void gcpu_exceptions_settings_enforce_on_hw(guest_cpu_handle_t gcpu,
						   uint32_t zeroes,
						   uint32_t ones)
{
	ia32_vmcs_exception_bitmap_t exceptions;

	exceptions.uint32 =
		(uint32_t)gcpu_get_exceptions_map_layered(gcpu, VMCS_MERGED);
	exceptions.uint32 = APPLY_ZEROES_AND_ONES(exceptions.uint32,
		zeroes,
		ones);
	exceptions.uint32 =
		(uint32_t)GET_FINAL_SETTINGS(gcpu,
			exceptions_ctrls,
			exceptions.uint32);
	gcpu_set_exceptions_map_layered(gcpu, VMCS_MERGED, exceptions.uint32);
	update_pfs_setup(gcpu,
		exceptions.bits.pf ? EXCEPTIONS_POLICY_CATCH_ALL :
		EXCEPTIONS_POLICY_CATCH_NOTHING);
}

static void gcpu_exceptions_settings_restore_on_hw(guest_cpu_handle_t gcpu)
{
	if (!gcpu_is_vmcs_layered(gcpu)) {
		ia32_vmcs_exception_bitmap_t exceptions;
		exceptions.uint32 =
			(uint32_t)gcpu->vmexit_setup.exceptions_ctrls.bit_field;
		exceptions.uint32 =
			(uint32_t)GET_FINAL_SETTINGS(gcpu, exceptions_ctrls,
				exceptions.uint32);
		gcpu_set_exceptions_map_layered(gcpu,
			VMCS_MERGED,
			exceptions.uint32);
		update_pfs_setup(gcpu,
			exceptions.bits.pf ? EXCEPTIONS_POLICY_CATCH_ALL :
			EXCEPTIONS_POLICY_CATCH_NOTHING);
	}
}

static void gcpu_proc_ctrls_enforce_on_hw(guest_cpu_handle_t gcpu,
					  uint32_t zeroes, uint32_t ones)
{
	uint32_t proc_ctrls =
		(uint32_t)gcpu_get_processor_ctrls_layered(gcpu, VMCS_MERGED);

	proc_ctrls = APPLY_ZEROES_AND_ONES(proc_ctrls, zeroes, ones);
	proc_ctrls = (uint32_t)GET_FINAL_SETTINGS(gcpu,
		processor_ctrls,
		proc_ctrls);
	gcpu_set_processor_ctrls_layered(gcpu, VMCS_MERGED, proc_ctrls);
}

static void gcpu_proc_ctrls_restore_on_hw(guest_cpu_handle_t gcpu)
{
	if (!gcpu_is_vmcs_layered(gcpu)) {
		uint32_t proc_ctrls =
			(uint32_t)gcpu->vmexit_setup.processor_ctrls.bit_field;
		proc_ctrls =
			(uint32_t)GET_FINAL_SETTINGS(gcpu,
				processor_ctrls,
				proc_ctrls);
		gcpu_set_processor_ctrls_layered(gcpu, VMCS_MERGED, proc_ctrls);
	}
}

static void gcpu_cr0_mask_enforce_on_hw(guest_cpu_handle_t gcpu,
					uint64_t zeroes, uint64_t ones)
{
	uint64_t cr0_mask = gcpu_get_cr0_reg_mask_layered(gcpu, VMCS_MERGED);

	cr0_mask = APPLY_ZEROES_AND_ONES(cr0_mask, zeroes, ones);
	cr0_mask = GET_FINAL_SETTINGS(gcpu, cr0, cr0_mask);
	gcpu_set_cr0_reg_mask_layered(gcpu, VMCS_MERGED, cr0_mask);
}

extern uint64_t ept_get_eptp(guest_cpu_handle_t gcpu);
extern boolean_t ept_set_eptp(guest_cpu_handle_t gcpu,
			      uint64_t ept_root_table_hpa,
			      uint32_t gaw);
extern guest_cpu_handle_t scheduler_get_current_gcpu_for_guest(
	guest_id_t guest_id);

boolean_t mon_get_vmcs_control_state(guest_cpu_handle_t gcpu,
				     mon_control_state_t control_state_id,
				     mon_controls_t *value)
{
	vmcs_object_t *vmcs;
	vmcs_field_t vmcs_field_id;

	MON_ASSERT(gcpu);

	vmcs = mon_gcpu_get_vmcs(gcpu);
	MON_ASSERT(vmcs);

	if (!value
	    || (uint32_t)control_state_id > (uint32_t)NUM_OF_MON_CONTROL_STATE -
	    1) {
		return FALSE;
	}

	/* vmcs_field_t and mon_control_state_t are not identically mapped. */
	if (control_state_id < MON_CR3_TARGET_VALUE_0) {
		vmcs_field_id = (vmcs_field_t)control_state_id;
	} else {
		vmcs_field_id =
			(vmcs_field_t)(VMCS_CR3_TARGET_VALUE_0 +
				       (control_state_id -
					MON_CR3_TARGET_VALUE_0));
	}

	switch (vmcs_field_id) {
	case VMCS_CONTROL_VECTOR_PIN_EVENTS:
		value->value = gcpu_get_pin_ctrls_layered(gcpu, VMCS_MERGED);
		break;
	case VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS:
		value->value = gcpu_get_processor_ctrls_layered(gcpu,
			VMCS_MERGED);
		break;
	case VMCS_CONTROL2_VECTOR_PROCESSOR_EVENTS:
		value->value = gcpu_get_processor_ctrls2_layered(gcpu,
			VMCS_MERGED);
		break;
	case VMCS_EXCEPTION_BITMAP:
		value->value =
			gcpu_get_exceptions_map_layered(gcpu, VMCS_MERGED);
		break;
	case VMCS_PAGE_FAULT_ERROR_CODE_MASK:
	case VMCS_PAGE_FAULT_ERROR_CODE_MATCH:
		gcpu_get_pf_error_code_mask_and_match_layered(gcpu, VMCS_MERGED,
			(uint32_t *)&
			(value->mask_value.mask),
			(uint32_t *)&
			(value->
			 mask_value.value));
		break;
	case VMCS_CR0_MASK:
		value->value = gcpu_get_cr0_reg_mask_layered(gcpu, VMCS_MERGED);
		break;
	case VMCS_CR4_MASK:
		value->value = gcpu_get_cr4_reg_mask_layered(gcpu, VMCS_MERGED);
		break;
	case VMCS_EXIT_CONTROL_VECTOR:
		value->value = gcpu_get_exit_ctrls_layered(gcpu, VMCS_MERGED);
		break;
	case VMCS_EPTP_ADDRESS:
		value->value = ept_get_eptp(gcpu);
		break;

	default:
		value->value = mon_vmcs_read(vmcs, vmcs_field_id);
		break;
	}

	return TRUE;
}

boolean_t mon_set_vmcs_control_state(guest_cpu_handle_t gcpu,
				     mon_control_state_t control_state_id,
				     mon_controls_t *value)
{
	vmcs_object_t *vmcs;
	vmcs_field_t vmcs_field_id;

	MON_ASSERT(gcpu);

	vmcs = mon_gcpu_get_vmcs(gcpu);
	MON_ASSERT(vmcs);

	if (!value
	    || (uint32_t)control_state_id > (uint32_t)NUM_OF_MON_CONTROL_STATE -
	    1) {
		return FALSE;
	}

	/* vmcs_field_t and mon_control_state_t are not identically mapped. */
	if (control_state_id < MON_CR3_TARGET_VALUE_0) {
		vmcs_field_id = (vmcs_field_t)control_state_id;
	} else {
		vmcs_field_id =
			(vmcs_field_t)(VMCS_CR3_TARGET_VALUE_0 +
				       (control_state_id -
					MON_CR3_TARGET_VALUE_0));
	}

	switch (vmcs_field_id) {
	case VMCS_CONTROL_VECTOR_PIN_EVENTS:
		request_vmexit_on_pin_ctrls(gcpu,
			(uint32_t)(value->mask_value.value),
			(uint32_t)(value->mask_value.mask));
		break;
	case VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS:
		if (value->mask_value.mask) {
			request_vmexit_on_proc_ctrls(gcpu,
				(uint32_t)(value->mask_value.value),
				(uint32_t)(value->mask_value.mask));
		} else {
			mon_vmcs_write(vmcs,
				VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS,
				value->mask_value.value);
		}
		break;
	case VMCS_CONTROL2_VECTOR_PROCESSOR_EVENTS:
		request_vmexit_on_proc_ctrls2(gcpu,
			(uint32_t)(value->mask_value.value),
			(uint32_t)(value->mask_value.mask));
		break;
	case VMCS_EXCEPTION_BITMAP:
		request_vmexit_on_exceptions(gcpu,
			(uint32_t)(value->mask_value.value),
			(uint32_t)(value->mask_value.mask));
		break;
	case VMCS_CR0_MASK:
		if (value->mask_value.mask
		    || ((!value->mask_value.mask) &&
			(!value->mask_value.value))) {
			request_vmexit_on_cr0(gcpu,
				(uint32_t)(value->mask_value.value),
				(uint32_t)(value->mask_value.mask));
		} else {
			mon_vmcs_write(vmcs,
				VMCS_CR0_MASK,
				value->mask_value.value);
		}
		break;
	case VMCS_CR4_MASK:
		if (value->mask_value.mask
		    || ((!value->mask_value.mask) &&
			(!value->mask_value.value))) {
			request_vmexit_on_cr4(gcpu,
				(uint32_t)(value->mask_value.value),
				(uint32_t)(value->mask_value.mask));
		} else {
			mon_vmcs_write(vmcs,
				VMCS_CR4_MASK,
				value->mask_value.value);
		}
		break;
	case VMCS_EXIT_CONTROL_VECTOR:
		gcpu_set_exit_ctrls_layered(gcpu, VMCS_MERGED,
			(uint32_t)(value->value));
		break;
	case VMCS_MSR_BITMAP_ADDRESS:
		mon_vmcs_write(vmcs, VMCS_MSR_BITMAP_ADDRESS, value->value);
		break;
	case VMCS_EPTP_INDEX:
		mon_vmcs_write(vmcs, VMCS_EPTP_INDEX, value->value);
		break;
	case VMCS_EPTP_ADDRESS:
		return ept_set_eptp(gcpu, value->ept_value.ept_root_table_hpa,
			(uint32_t)(value->ept_value.gaw));
	case VMCS_PAGE_FAULT_ERROR_CODE_MATCH:
		/* add new func in mon */
		/* TBD */
		return FALSE;
	case VMCS_VPID:
	case VMCS_PAGE_FAULT_ERROR_CODE_MASK:
	case VMCS_ENTER_CONTROL_VECTOR:
	case VMCS_ENTER_INTERRUPT_INFO:
	case VMCS_ENTER_EXCEPTION_ERROR_CODE:
	case VMCS_ENTER_INSTRUCTION_LENGTH:
		/* Not supported. Will support later if required. TBD. */
		return FALSE;

	default:
		/* Not supported or read-only. */
		return FALSE;
	}

	return TRUE;
}
