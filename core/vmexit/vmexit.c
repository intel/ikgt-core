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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMEXIT_C, __condition)
#include "mon_defs.h"
#include "heap.h"
#include "scheduler.h"
#include "vmx_asm.h"
#include "mon_globals.h"
#include "vmcs_actual.h"
#include "guest.h"
#include "em64t_defs.h"
#include "vmexit_msr.h"
#include "vmexit_io.h"
#include "vmcall.h"
#include "vmexit_cpuid.h"
#include "vmexit.h"
#include "mon_dbg.h"
#include "list.h"
#include "lock.h"
#include "memory_allocator.h"
#include "vmexit_analysis.h"
#include "guest_cpu_vmenter_event.h"
#include "host_cpu.h"
#include "guest_cpu_internal.h"
#include "vmenter_checks.h"
#include "mon_callback.h"
#include "fvs.h"
#include "isr.h"
#include "memory_dump.h"
#include "vmexit_dtr_tr.h"

boolean_t legacy_scheduling_enabled = TRUE;

extern boolean_t vmcs_sw_shadow_disable[];

extern vmexit_handling_status_t vmexit_cr_access(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_triple_fault(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_undefined_opcode(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_init_event(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_sipi_event(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_task_switch(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_invlpg(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_invd(guest_cpu_handle_t gcpu);
extern void vmexit_check_keystroke(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_ept_violation(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_ept_misconfiguration(guest_cpu_handle_t
							    gcpu);
extern vmexit_handling_status_t
msr_failed_vmenter_loading_handler(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_mtf(guest_cpu_handle_t gcpu);

extern vmexit_handling_status_t vmexit_vmxon_instruction(guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_vmxoff_instruction(
	guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_vmlaunch_instruction(guest_cpu_handle_t
							    gcpu);
extern vmexit_handling_status_t vmexit_vmresume_instruction(guest_cpu_handle_t
							    gcpu);
extern vmexit_handling_status_t vmexit_vmclear_instruction(
	guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_vmptrld_instruction(
	guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_vmptrst_instruction(
	guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_vmread_instruction(
	guest_cpu_handle_t gcpu);
extern vmexit_handling_status_t vmexit_vmwrite_instruction(
	guest_cpu_handle_t gcpu);

vmexit_handling_status_t vmexit_halt_instruction(guest_cpu_handle_t gcpu);
vmexit_handling_status_t vmexit_xsetbv(guest_cpu_handle_t gcpu);
vmexit_handling_status_t
vmexit_vmentry_failure_due2_machine_check(guest_cpu_handle_t gcpu);
vmexit_handling_status_t vmexit_invalid_vmfunc(guest_cpu_handle_t gcpu);

uint32_t ASM_FUNCTION vmexit_check_ept_violation(void);

extern int CLI_active(void);

static
void vmexit_bottom_up_common_handler(guest_cpu_handle_t gcpu, uint32_t reason);

static
void vmexit_bottom_up_all_mons_skip_instruction(guest_cpu_handle_t gcpu,
						uint32_t reason);

static
void vmexit_top_down_common_handler(guest_cpu_handle_t gcpu, uint32_t reason);

typedef struct {
	guest_id_t	 guest_id;
	char		 padding[6];
	vmexit_handler_t vmexit_handlers[IA32_VMX_EXIT_BASIC_REASON_COUNT];
	uint64_t	 vmexit_counter[IA32_VMX_EXIT_BASIC_REASON_COUNT];
	list_element_t	 list[1];
} guest_vmexit_control_t;

typedef struct {
	list_element_t guest_vmexit_controls[1];
} vmexit_global_state_t;

/* for all guests */
static vmexit_global_state_t vmexit_global_state;

typedef void (*func_vmexit_classification_t) (guest_cpu_handle_t gcpu,
					      uint32_t reason);

static func_vmexit_classification_t
	vmexit_classification_func[IA32_VMX_EXIT_BASIC_REASON_COUNT] = {
	/* 0 IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI */
	vmexit_bottom_up_common_handler,
	/* 1 IA32_VMX_EXIT_BASIC_REASON_HARDWARE_INTERRUPT */
	vmexit_bottom_up_common_handler,
	/* 2 IA32_VMX_EXIT_BASIC_REASON_TRIPLE_FAULT */
	vmexit_top_down_common_handler,
	/* 3 IA32_VMX_EXIT_BASIC_REASON_INIT_EVENT */
	vmexit_bottom_up_common_handler,
	/* 4 IA32_VMX_EXIT_BASIC_REASON_SIPI_EVENT */
	vmexit_bottom_up_common_handler,
	/* 5 IA32_VMX_EXIT_BASIC_REASON_SMI_IO_EVENT */
	vmexit_bottom_up_common_handler,
	/* 6 IA32_VMX_EXIT_BASIC_REASON_SMI_OTHER_EVENT */
	vmexit_bottom_up_common_handler,
	/* 7 IA32_VMX_EXIT_BASIC_REASON_PENDING_INTERRUPT */
	vmexit_top_down_common_handler,
	/* 8 IA32_VMX_EXIT_NMI_WINDOW */
	vmexit_top_down_common_handler,
	/* 9 IA32_VMX_EXIT_BASIC_REASON_TASK_SWITCH */
	vmexit_top_down_common_handler,
	/* 10 IA32_VMX_EXIT_BASIC_REASON_CPUID_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 11 IA32_VMX_EXIT_BASIC_REASON_GETSEC_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 12 IA32_VMX_EXIT_BASIC_REASON_HLT_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 13 IA32_VMX_EXIT_BASIC_REASON_INVD_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 14 IA32_VMX_EXIT_BASIC_REASON_INVLPG_INSTRUCTION */
	vmexit_bottom_up_all_mons_skip_instruction,
	/* 15 IA32_VMX_EXIT_BASIC_REASON_RDPMC_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 16 IA32_VMX_EXIT_BASIC_REASON_RDTSC_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 17 IA32_VMX_EXIT_BASIC_REASON_RSM_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 18 IA32_VMX_EXIT_BASIC_REASON_VMCALL_INSTRUCTION */
	vmexit_bottom_up_common_handler,
	/* 19 IA32_VMX_EXIT_BASIC_REASON_VMCLEAR_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 20 IA32_VMX_EXIT_BASIC_REASON_VMLAUNCH_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 21 IA32_VMX_EXIT_BASIC_REASON_VMPTRLD_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 22 IA32_VMX_EXIT_BASIC_REASON_VMPTRST_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 23 IA32_VMX_EXIT_BASIC_REASON_VMREAD_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 24 IA32_VMX_EXIT_BASIC_REASON_VMRESUME_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 25 IA32_VMX_EXIT_BASIC_REASON_VMWRITE_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 26 IA32_VMX_EXIT_BASIC_REASON_VMXOFF_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 27 IA32_VMX_EXIT_BASIC_REASON_VMXON_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 28 IA32_VMX_EXIT_BASIC_REASON_CR_ACCESS */
	vmexit_top_down_common_handler,
	/* 29 IA32_VMX_EXIT_BASIC_REASON_DR_ACCESS */
	vmexit_top_down_common_handler,
	/* 30 IA32_VMX_EXIT_BASIC_REASON_IO_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 31 IA32_VMX_EXIT_BASIC_REASON_MSR_READ */
	vmexit_top_down_common_handler,
	/* 32 IA32_VMX_EXIT_BASIC_REASON_MSR_WRITE */
	vmexit_top_down_common_handler,
	/* 33 IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_GUEST_STATE */
	vmexit_bottom_up_common_handler,
	/* 34 IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_MSR_LOADING */
	vmexit_bottom_up_common_handler,
	/* 35 IA32_VMX_EXIT_BASIC_REASON_FAILED_VMEXIT */
	vmexit_top_down_common_handler,
	/* 36 IA32_VMX_EXIT_BASIC_REASON_MWAIT_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 37 IA32_VMX_EXIT_BASIC_REASON_MONITOR_TRAP_FLAG */
	vmexit_top_down_common_handler,
	/* 38 IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_38 */
	vmexit_top_down_common_handler,
	/* 39 IA32_VMX_EXIT_BASIC_REASON_MONITOR */
	vmexit_top_down_common_handler,
	/* 40 IA32_VMX_EXIT_BASIC_REASON_PAUSE */
	vmexit_top_down_common_handler,
	/* 41 IA32_VMX_EXIT_BASIC_REASON_FAILURE_DUE_MACHINE_CHECK */
	vmexit_bottom_up_common_handler,
	/* 42 IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_42 */
	vmexit_top_down_common_handler,
	/* 43 IA32_VMX_EXIT_BASIC_REASON_TPR_BELOW_THRESHOLD */
	vmexit_top_down_common_handler,
	/* 44 IA32_VMX_EXIT_BASIC_REASON_APIC_ACCESS */
	vmexit_top_down_common_handler,
	/* 45 IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_45 */
	vmexit_top_down_common_handler,
	/* 46 IA32_VMX_EXIT_BASIC_REASON_GDTR_LDTR_ACCESS */
	vmexit_top_down_common_handler,
	/* 47 IA32_VMX_EXIT_BASIC_REASON_LDTR_TR_ACCESS */
	vmexit_top_down_common_handler,
	/* 48 IA32_VMX_EXIT_BASIC_REASON_EPT_VIOLATION */
	vmexit_bottom_up_common_handler,
	/* 48 IA32_VMX_EXIT_BASIC_REASON_EPT_MISCONFIGURATION */
	vmexit_bottom_up_common_handler,
	/* 50 IA32_VMX_EXIT_BASIC_REASON_INVEPT_INSTRUCTION */
	vmexit_bottom_up_common_handler,
	/* 51 IA32_VMX_EXIT_BASIC_REASON_RDTSCP_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 52 IA32_VMX_EXIT_BASIC_REASON_PREEMPTION_TIMER_EXPIRED */
	vmexit_bottom_up_common_handler,
	/* 53 IA32_VMX_EXIT_BASIC_REASON_INVVPID_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 54 IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_54 */
	vmexit_top_down_common_handler,
	/* 55 IA32_VMX_EXIT_BASIC_REASON_XSETBV_INSTRUCTION */
	vmexit_top_down_common_handler,
	/* 56 IA32_VMX_EXIT_BASIC_REASON_PLACE_HOLDER_1 */
	vmexit_top_down_common_handler,
	/* 57 IA32_VMX_EXIT_BASIC_REASON_PLACE_HOLDER_2 */
	vmexit_top_down_common_handler,
	/* 58 IA32_VMX_EXIT_BASIC_REASON_PLACE_HOLDER_3 */
	vmexit_top_down_common_handler,
	/* 59 IA32_VMX_EXIT_BASIC_REASON_INVALID_VMFUNC */
	vmexit_top_down_common_handler
};

/* T.B.D.*/
#define NMI_DO_PROCESSING()
extern void vmexit_nmi_exception_handlers_install(guest_id_t guest_id);

static void vmexit_handler_invoke(guest_cpu_handle_t gcpu, uint32_t reason);

static guest_vmexit_control_t *vmexit_find_guest_vmexit_control(guest_id_t
								guest_id);

/*----------------------------------Code-----------------------------------*/

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_setup()
*  PURPOSE  : Populate guest table, containing specific VMEXIT handlers with
*           : default handlers
*  ARGUMENTS: guest_id_t num_of_guests
*  RETURNS  : void
*--------------------------------------------------------------------------*/
void vmexit_initialize(void)
{
	guest_handle_t guest;
	guest_econtext_t guest_ctx;

	mon_memset(&vmexit_global_state, 0, sizeof(vmexit_global_state));

	list_init(vmexit_global_state.guest_vmexit_controls);
	io_vmexit_initialize();
	vmcall_intialize();

	for (guest = guest_first(&guest_ctx);
	     guest; guest = guest_next(&guest_ctx))
		vmexit_guest_initialize(guest_get_id(guest));
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_guest_initialize()
*  PURPOSE  : Populate guest table, containing specific VMEXIT handlers with
*           : default handlers
*  ARGUMENTS: guest_id_t guest_id
*  RETURNS  : void
*--------------------------------------------------------------------------*/
void vmexit_guest_initialize(guest_id_t guest_id)
{
	guest_vmexit_control_t *guest_vmexit_control = NULL;
	uint32_t i;

	MON_LOG(mask_mon, level_trace,
		"vmexit_guest_initialize start guest_id=#%d\r\n", guest_id);

	guest_vmexit_control =
		(guest_vmexit_control_t *)mon_malloc(sizeof(
				guest_vmexit_control_t));
	MON_ASSERT(guest_vmexit_control);

	guest_vmexit_control->guest_id = guest_id;

	list_add(vmexit_global_state.guest_vmexit_controls,
		guest_vmexit_control->list);

	/* install default handlers */
	for (i = 0; i < IA32_VMX_EXIT_BASIC_REASON_COUNT; ++i)
		guest_vmexit_control->vmexit_handlers[i] =
			vmexit_handler_default;

	/* commented out handlers installed by means of vmexit_install_handler
	 * guest_vmexit_control->vmexit_handlers[IA32_VMX_EXIT_BASIC_REASON_MSR_READ] =
	 * vmexit_msr_read;
	 * guest_vmexit_control->vmexit_handlers[IA32_VMX_EXIT_BASIC_REASON_MSR_WRITE] =
	 * vmexit_msr_write;
	 * guest_vmexit_control->vmexit_handlers[IA32_VMX_EXIT_BASIC_REASON_VMCALL_INSTRUCTION]
	 * = vmexit_vmcall;
	 * guest_vmexit_control->vmexit_handlers
	 *     [IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI]=
	 *         vmexit_software_interrupt_exception_nmi;
	 * guest_vmexit_control->vmexit_handlers[IA32_VMX_EXIT_NMI_WINDOW] =
	 *     vmexit_nmi_window; */
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_CR_ACCESS] =
		vmexit_cr_access;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_SIPI_EVENT] =
		vmexit_sipi_event;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_INIT_EVENT] =
		vmexit_init_event;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_TRIPLE_FAULT] =
		vmexit_triple_fault;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_HLT_INSTRUCTION]
		= vmexit_halt_instruction;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_TASK_SWITCH] =
		vmexit_task_switch;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_VMCLEAR_INSTRUCTION] =
		vmexit_vmclear_instruction;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_VMLAUNCH_INSTRUCTION] =
		vmexit_vmlaunch_instruction;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_VMPTRLD_INSTRUCTION] =
		vmexit_vmptrld_instruction;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_VMPTRST_INSTRUCTION] =
		vmexit_vmptrst_instruction;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_VMREAD_INSTRUCTION] =
		vmexit_vmread_instruction;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_VMRESUME_INSTRUCTION] =
		vmexit_vmresume_instruction;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_VMWRITE_INSTRUCTION] =
		vmexit_vmwrite_instruction;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_VMXOFF_INSTRUCTION] =
		vmexit_vmxoff_instruction;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_VMXON_INSTRUCTION] =
		vmexit_vmxon_instruction;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_INVD_INSTRUCTION]
		= vmexit_invd;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_INVLPG_INSTRUCTION] = vmexit_invlpg;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_EPT_VIOLATION] =
		vmexit_ept_violation;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_EPT_MISCONFIGURATION] =
		vmexit_ept_misconfiguration;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_INVEPT_INSTRUCTION] =
		vmexit_undefined_opcode;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_MSR_LOADING] =
		msr_failed_vmenter_loading_handler;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_INVVPID_INSTRUCTION] =
		vmexit_undefined_opcode;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_GDTR_LDTR_ACCESS]
		= vmexit_gdtr_idtr_access;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_LDTR_TR_ACCESS] =
		vmexit_ldtr_tr_access;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_DR_ACCESS] =
		vmexit_dr_access;
	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_MONITOR_TRAP_FLAG]
		= vmexit_mtf;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_FAILURE_DUE_MACHINE_CHECK] =
		vmexit_vmentry_failure_due2_machine_check;
	guest_vmexit_control->vmexit_handlers
	[IA32_VMX_EXIT_BASIC_REASON_XSETBV_INSTRUCTION] = vmexit_xsetbv;

	guest_vmexit_control->vmexit_handlers[
		IA32_VMX_EXIT_BASIC_REASON_INVALID_VMFUNC] =
		vmexit_invalid_vmfunc;

	/* install IO VMEXITs */
	io_vmexit_guest_initialize(guest_id);

	/* install NMI and Exceptions VMEXITs */
	vmexit_nmi_exception_handlers_install(guest_id);

	/* init CPUID instruction vmexit handlers */
	vmexit_cpuid_guest_intialize(guest_id);

	/* install VMCALL services */
	vmcall_guest_intialize(guest_id);
	MON_LOG(mask_mon, level_trace,
		"vmexit_guest_initialize end guest_id=#%d\r\n", guest_id);
}

static
void vmexit_bottom_up_all_mons_skip_instruction(guest_cpu_handle_t gcpu,
						uint32_t reason)
{
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
	guest_id_t guest_id = guest_get_id(guest);
	guest_vmexit_control_t *guest_vmexit_control = NULL;
	vmcs_hierarchy_t *vmcs_hierarchy = gcpu_get_vmcs_hierarchy(gcpu);
	vmcs_object_t *level0_vmcs =
		vmcs_hierarchy_get_vmcs(vmcs_hierarchy, VMCS_LEVEL_0);
	vmcs_object_t *merged_vmcs =
		vmcs_hierarchy_get_vmcs(vmcs_hierarchy, VMCS_MERGED);
	guest_level_t guest_level = gcpu_get_guest_level(gcpu);
	boolean_t skip_instruction = TRUE;

	guest_vmexit_control = vmexit_find_guest_vmexit_control(guest_id);
	MON_ASSERT(guest_vmexit_control);

	MON_ASSERT(reason < IA32_VMX_EXIT_BASIC_REASON_COUNT);

	MON_ASSERT(level0_vmcs != NULL);
	hw_interlocked_increment((int32_t *)&
		(guest_vmexit_control->vmexit_counter[reason]));

	if ((guest_level == GUEST_LEVEL_1_SIMPLE) ||
	    (guest_level == GUEST_LEVEL_1_MON) ||
	    (vmexit_analysis_was_control_requested
		     (gcpu, merged_vmcs, level0_vmcs,
		     (ia32_vmx_exit_basic_reason_t)reason))) {
#ifdef DEBUG
		/* Check that in GUEST_LEVEL_1_SIMPLE and GUEST_LEVEL_1_MON modes */
		/* the vmexit was requested in the level-0 controls */
		if (guest_level == GUEST_LEVEL_1_MON) {
			MON_ASSERT(vmexit_analysis_was_control_requested
					(gcpu, merged_vmcs, level0_vmcs,
					(ia32_vmx_exit_basic_reason_t)reason));
		}
#endif
		/* return value is not important */
		guest_vmexit_control->vmexit_handlers[reason] (gcpu);
	}

	if (guest_level == GUEST_LEVEL_2) {
		vmcs_object_t *level1_vmcs =
			vmcs_hierarchy_get_vmcs(vmcs_hierarchy, VMCS_LEVEL_1);

		MON_ASSERT(level1_vmcs != NULL);
		/* Check if layer2 can accept the event, if not inject event to
		 * (level-2) guest */
		if (vmexit_analysis_was_control_requested
			    (gcpu, merged_vmcs, level1_vmcs,
			    (ia32_vmx_exit_basic_reason_t)reason)) {
			gcpu_set_next_guest_level(gcpu, GUEST_LEVEL_1_MON);

			/* instruction will be skipped by level-1 */
			skip_instruction = FALSE;
		}
	}

	if (skip_instruction) {
		gcpu_skip_guest_instruction(gcpu);
	}
}

void vmexit_bottom_up_common_handler(guest_cpu_handle_t gcpu, uint32_t reason)
{
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
	guest_id_t guest_id = guest_get_id(guest);
	guest_vmexit_control_t *guest_vmexit_control = NULL;
	vmexit_handling_status_t vmexit_handling_status = VMEXIT_NOT_HANDLED;
	vmcs_hierarchy_t *vmcs_hierarchy = gcpu_get_vmcs_hierarchy(gcpu);
	vmcs_object_t *level0_vmcs =
		vmcs_hierarchy_get_vmcs(vmcs_hierarchy, VMCS_LEVEL_0);
	vmcs_object_t *merged_vmcs =
		vmcs_hierarchy_get_vmcs(vmcs_hierarchy, VMCS_MERGED);
	guest_level_t guest_level = gcpu_get_guest_level(gcpu);

	guest_vmexit_control = vmexit_find_guest_vmexit_control(guest_id);
	MON_ASSERT(guest_vmexit_control);

	MON_ASSERT(reason < IA32_VMX_EXIT_BASIC_REASON_COUNT);

	MON_ASSERT(level0_vmcs != NULL);
	hw_interlocked_increment((int32_t *)&
		(guest_vmexit_control->vmexit_counter[reason]));

	if ((guest_level == GUEST_LEVEL_1_SIMPLE) ||    /* non -layered vmexit */
	    (guest_level == GUEST_LEVEL_1_MON) ||       /* or vmexit from level 1 */
	    (vmexit_analysis_was_control_requested
		     (gcpu, merged_vmcs, level0_vmcs,
		     (ia32_vmx_exit_basic_reason_t)reason))) {
		ia32_vmx_exit_reason_t exit_reason;

#ifdef DEBUG
		/* Check that in GUEST_LEVEL_1_SIMPLE and GUEST_LEVEL_1_MON modes */
		/* the vmexit was requested in the level-0 controls */
		if (guest_level == GUEST_LEVEL_1_MON) {
			MON_ASSERT(vmexit_analysis_was_control_requested
					(gcpu, merged_vmcs, level0_vmcs,
					reason));
		}
#endif

		vmexit_handling_status =
			guest_vmexit_control->vmexit_handlers[reason] (gcpu);

		/* reason can be changed after the attempt to handle */
		exit_reason.uint32 =
			(uint32_t)mon_vmcs_read(merged_vmcs,
				VMCS_EXIT_INFO_REASON);
		reason = exit_reason.bits.basic_reason;
	}

	if ((vmexit_handling_status != VMEXIT_HANDLED) &&
	    (guest_level == GUEST_LEVEL_2)) {
		vmcs_object_t *level1_vmcs =
			vmcs_hierarchy_get_vmcs(vmcs_hierarchy, VMCS_LEVEL_1);

		MON_ASSERT(level1_vmcs != NULL);

		/* Check if layer2 can accept the event, if not inject event to
		 * (level-2) guest */
		if (vmexit_analysis_was_control_requested
			    (gcpu, merged_vmcs, level1_vmcs,
			    (ia32_vmx_exit_basic_reason_t)reason)) {
			gcpu_set_next_guest_level(gcpu, GUEST_LEVEL_1_MON);
			vmexit_handling_status = VMEXIT_HANDLED;
		}
	}

	if (vmexit_handling_status != VMEXIT_HANDLED) {
		/* Currently it can happen only for exception */
		if (reason !=
		    IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI) {
			MON_LOG(mask_mon,
				level_trace,
				"%s: reason = %d\n",
				__FUNCTION__,
				reason);
		}
		MON_ASSERT(reason ==
			IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI);
		gcpu_vmexit_exception_reflect(gcpu);
	} else {
		/* TODO: Here must be call to resolve gcpu_vmexit_exception_resolve */
	}
}

void vmexit_top_down_common_handler(guest_cpu_handle_t gcpu, uint32_t reason)
{
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
	guest_id_t guest_id = guest_get_id(guest);
	guest_vmexit_control_t *guest_vmexit_control = NULL;
	vmexit_handling_status_t vmexit_handling_status = VMEXIT_NOT_HANDLED;
	vmcs_hierarchy_t *vmcs_hierarchy = gcpu_get_vmcs_hierarchy(gcpu);
	vmcs_object_t *merged_vmcs =
		vmcs_hierarchy_get_vmcs(vmcs_hierarchy, VMCS_MERGED);
	guest_level_t guest_level = gcpu_get_guest_level(gcpu);

	guest_vmexit_control = vmexit_find_guest_vmexit_control(guest_id);
	MON_ASSERT(guest_vmexit_control);

	MON_ASSERT(reason < IA32_VMX_EXIT_BASIC_REASON_COUNT);

	hw_interlocked_increment((int32_t *)&
		(guest_vmexit_control->vmexit_counter[reason]));

	if (guest_level == GUEST_LEVEL_2 && gcpu_is_native_execution(gcpu)) {
		vmcs_object_t *level1_vmcs =
			vmcs_hierarchy_get_vmcs(vmcs_hierarchy, VMCS_LEVEL_1);

		MON_ASSERT(level1_vmcs != NULL);
		/* Check whether it can be handled in Level-1 */
		if (vmexit_analysis_was_control_requested
			    (gcpu, merged_vmcs, level1_vmcs,
			    (ia32_vmx_exit_basic_reason_t)reason)) {
			gcpu_set_next_guest_level(gcpu, GUEST_LEVEL_1_MON);
			vmexit_handling_status = VMEXIT_HANDLED;
		}
	}

	if (vmexit_handling_status != VMEXIT_HANDLED) {
		/* Handle in Level-0 */
		vmexit_handling_status =
			guest_vmexit_control->vmexit_handlers[reason] (gcpu);
	}

	if (vmexit_handling_status != VMEXIT_HANDLED) {
		if (vmexit_handling_status == VMEXIT_HANDLED_RESUME_LEVEL2) {
			gcpu_set_next_guest_level(gcpu, GUEST_LEVEL_2);
		} else {
			MON_LOG(mask_mon, level_trace,
				"%s: Top-Down VMExit (%d) which wasn't handled",
				__FUNCTION__, reason);
			/* Should not get here */
			MON_DEADLOOP();
		}
	}
}

void vmexit_handler_invoke(guest_cpu_handle_t gcpu, uint32_t reason)
{
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
	guest_id_t guest_id = guest_get_id(guest);
	guest_vmexit_control_t *guest_vmexit_control = NULL;

	guest_vmexit_control = vmexit_find_guest_vmexit_control(guest_id);
	MON_ASSERT(guest_vmexit_control);
	if (reason < IA32_VMX_EXIT_BASIC_REASON_COUNT) {
		/* Call top-down or bottom-up common handler; */
		vmexit_classification_func[reason] (gcpu, reason);
	} else {
		MON_LOG(mask_mon,
			level_trace,
			"Warning: Unknown VMEXIT reason(%d)\n",
			reason);
		vmexit_handler_default(gcpu);
	}
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmentry_failure_function
*  PURPOSE  : Called upon VMENTER failure
*  ARGUMENTS: address_t flag - value of processor flags register
*  RETURNS  : void
*  NOTES    : is not VMEXIT
*--------------------------------------------------------------------------*/
void vmentry_failure_function(address_t flags)
{
	guest_cpu_handle_t gcpu = mon_scheduler_current_gcpu();
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	const char *err = NULL;
	vmcs_instruction_error_t code;
	em64t_rflags_t rflags;

#ifndef DEBUG
	ia32_vmx_vmcs_guest_interruptibility_t interruptibility;
#endif

	rflags.uint64 = flags;
	code = vmcs_last_instruction_error_code(vmcs, &err);

	MON_LOG(mask_mon, level_error, "CPU%d: VMENTRY Failed on ",
		hw_cpu_id());
	PRINT_GCPU_IDENTITY(gcpu);
	MON_LOG(mask_mon, level_error,
		" FLAGS=0x%X (zf=%d cf=%d) ErrorCode=0x%X Desc=%s\n", flags,
		rflags.bits.zf, rflags.bits.cf, code, err);
#ifdef CLI_INCLUDE
	vmcs_print_all(vmcs);
#endif

#ifdef DEBUG
	MON_DEADLOOP();
#else
	mon_deadloop_internal(VMEXIT_C, __LINE__, gcpu);
	vmcs_restore_initial(gcpu);

	/* clear interrupt flag */
	rflags.uint64 = gcpu_get_gp_reg(gcpu, IA32_REG_RFLAGS);
	rflags.bits.ifl = 0;
	gcpu_set_gp_reg(gcpu, IA32_REG_RFLAGS, rflags.uint64);

	interruptibility.uint32 = gcpu_get_interruptibility_state(gcpu);
	interruptibility.bits.block_next_instruction = 0;
	gcpu_set_interruptibility_state(gcpu, interruptibility.uint32);

	mon_gcpu_inject_gp0(gcpu);
	gcpu_resume(gcpu);
#endif
}

extern void mon_write_xcr(uint64_t, uint64_t, uint64_t);
extern void mon_read_xcr(uint32_t *, uint32_t *, uint32_t);

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_xsetbv()
*  PURPOSE  : Handler for xsetbv instruction
*  ARGUMENTS: gcpu
*  RETURNS  : void
*--------------------------------------------------------------------------*/
vmexit_handling_status_t vmexit_xsetbv(guest_cpu_handle_t gcpu)
{
	uint32_t xcr0_mask_low, xcr0_mask_high;
	cpuid_params_t cpuid_params;

	cpuid_params.m_rax = 0xd;
	cpuid_params.m_rcx = 0;

	hw_cpuid(&cpuid_params);

	mon_read_xcr(&xcr0_mask_low, &xcr0_mask_high, 0);
	/*
	 * let's check three things first before executing the instruction to make
	 * sure everything is correct, otherwise, inject GP0 to guest instead of
	 * failing in host since guest is responsible for the failure if any
	 * 1. Guest ECX must have a value of zero since only one XCR which is
	 *    XCR0 is supported by HW currently
	 * 2. The reserved bits in XCR0 are not being changed
	 * 3. Bit 0 of XCR0 is not being changed to zero since it must be one.
	 * 4. No attempt to write 0 to bit 1 and 1 to bit 2, i.e. XCR0[2:1]=10.
	 */
	if (((gcpu->save_area.gp.reg[IA32_REG_RCX] << 32) > 0) ||
	    (((~((uint32_t)cpuid_params.m_rax)) & xcr0_mask_low) !=
	     (uint32_t)(~cpuid_params.m_rax &
			gcpu->save_area.gp.reg[IA32_REG_RAX]))
	    || (((~((uint32_t)cpuid_params.m_rdx)) & xcr0_mask_high) !=
		(uint32_t)(~cpuid_params.m_rdx & gcpu->save_area.
			   gp.reg[IA32_REG_RDX]))
	    || ((gcpu->save_area.gp.reg[IA32_REG_RAX] & 1) == 0)
	    || ((gcpu->save_area.gp.reg[IA32_REG_RAX] & 0x6) == 0x4)) {
		mon_gcpu_inject_gp0(gcpu);
		return VMEXIT_HANDLED;
	}

	mon_write_xcr(gcpu->save_area.gp.reg[IA32_REG_RCX],
		gcpu->save_area.gp.reg[IA32_REG_RDX],
		gcpu->save_area.gp.reg[IA32_REG_RAX]);

	gcpu_skip_guest_instruction(gcpu);

	return VMEXIT_HANDLED;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_halt_instruction()
*  PURPOSE  : Handler for halt instruction
*  ARGUMENTS: gcpu
*  RETURNS  : vmexit handling status
*--------------------------------------------------------------------------*/
vmexit_handling_status_t vmexit_halt_instruction(guest_cpu_handle_t gcpu)
{
	if (!report_mon_event
		    (MON_EVENT_HALT_INSTRUCTION,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu), NULL)) {
		MON_LOG(mask_mon, level_trace,
			"Report HALT Instruction VMExit failed.\n");
		MON_DEADLOOP();
	}

	return VMEXIT_HANDLED;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_vmentry_failure_due2_machine_check()
*  PURPOSE  : Handler for vmexit that happens in vmentry due to machine check
*  ARGUMENTS: gcpu
*  RETURNS  : vmexit_handling_status_t
*--------------------------------------------------------------------------*/
vmexit_handling_status_t
vmexit_vmentry_failure_due2_machine_check(guest_cpu_handle_t gcpu)
{
	MON_LOG(mask_mon, level_error,
		"CPU%d: VMENTRY failed due to machine check\r\n", hw_cpu_id());
#ifdef DEBUG
	MON_DEADLOOP();
#else
	/* IA SDM 15.10.4.1 :Reset system for uncorrected machine check errors */
	hw_reset_platform();
#endif
	/* never reach here */
	return VMEXIT_HANDLED;
}


/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_invalid_vmfunc()
*  PURPOSE  : Handler for invalid vmfunc instruction
*  ARGUMENTS: gcpu
*  RETURNS  : vmexit_handling_status_t
*--------------------------------------------------------------------------*/
vmexit_handling_status_t vmexit_invalid_vmfunc(guest_cpu_handle_t gcpu)
{
	report_fast_view_switch_data_t fast_view_switch_data;
	uint64_t r_ecx;

	r_ecx = gcpu_get_native_gp_reg(gcpu, IA32_REG_RCX);
	/* Invalid vmfunc report to handler */

	MON_LOG(mask_anonymous, level_trace,
		"%s: view id=%d.Invalid vmfunc vmexit.\n", __FUNCTION__, r_ecx);

	fast_view_switch_data.reg = r_ecx;
	report_mon_event(MON_EVENT_INVALID_FAST_VIEW_SWITCH,
		(mon_identification_data_t)gcpu,
		(const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		(void *)&fast_view_switch_data);
	return VMEXIT_HANDLED;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_handler_default()
*  PURPOSE  : Handler for unimplemented/not supported VMEXITs
*  ARGUMENTS: IN VMEXIT_EXECUTION_CONTEXT *vmexit - contains guest handles
*  RETURNS  : void
*--------------------------------------------------------------------------*/
vmexit_handling_status_t vmexit_handler_default(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ia32_vmx_exit_reason_t reason;

#if defined DEBUG
	const virtual_cpu_id_t *vcpu_id = mon_guest_vcpu(gcpu);
#else
	em64t_rflags_t rflags;
	ia32_vmx_vmcs_guest_interruptibility_t interruptibility;
#endif

	reason.uint32 = (uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_REASON);
#if defined DEBUG
	MON_ASSERT(vcpu_id);
	MON_LOG(mask_mon, level_error,
		"NOT supported VMEXIT(%d) occurred on CPU(%d) Guest(%d)"
		" GuestCPU(%d)\n", reason.bits.basic_reason,
		hw_cpu_id(), vcpu_id->guest_id, vcpu_id->guest_cpu_id);
#endif

	MON_DEBUG_CODE(if
		(reason.bits.basic_reason ==
		 IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_GUEST_STATE
		 || reason.bits.basic_reason ==
		 IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_MSR_LOADING) {
			vmenter_failure_check_guest_state();
		}
		)
#if defined DEBUG
	MON_DEADLOOP();         /* VTDBG */
#else
	if (reason.bits.basic_reason ==
	    IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_GUEST_STATE
	    || reason.bits.basic_reason ==
	    IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_MSR_LOADING) {
		mon_deadloop_internal(VMEXIT_C, __LINE__, gcpu);
		vmcs_restore_initial(gcpu);

		/* clear interrupt flag */
		rflags.uint64 = gcpu_get_gp_reg(gcpu, IA32_REG_RFLAGS);
		rflags.bits.ifl = 0;
		gcpu_set_gp_reg(gcpu, IA32_REG_RFLAGS, rflags.uint64);

		interruptibility.uint32 = gcpu_get_interruptibility_state(gcpu);
		interruptibility.bits.block_next_instruction = 0;
		gcpu_set_interruptibility_state(gcpu, interruptibility.uint32);

		mon_gcpu_inject_gp0(gcpu);
		gcpu_resume(gcpu);
	} else {
		MON_DEADLOOP();
	}
#endif
	return VMEXIT_NOT_HANDLED;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_install_handler
*  PURPOSE  : Install specific VMEXIT handler
*  ARGUMENTS: guest_id_t        guest_id
*           : vmexit_handler_t  handler
*           : uint32_t          reason
*  RETURNS  : mon_status_t
*--------------------------------------------------------------------------*/
mon_status_t vmexit_install_handler(guest_id_t guest_id,
				    vmexit_handler_t handler,
				    uint32_t reason)
{
	mon_status_t status = MON_OK;
	guest_vmexit_control_t *guest_vmexit_control = NULL;

	guest_vmexit_control = vmexit_find_guest_vmexit_control(guest_id);
	MON_ASSERT(guest_vmexit_control);

	if (reason < IA32_VMX_EXIT_BASIC_REASON_COUNT) {
		guest_vmexit_control->vmexit_handlers[reason] = handler;
	} else {
		MON_LOG(mask_mon,
			level_error,
			"CPU%d: Error: VMEXIT Reason(%d) exceeds supported limit\n",
			hw_cpu_id(),
			reason);
		status = MON_ERROR;
		MON_ASSERT(reason < IA32_VMX_EXIT_BASIC_REASON_COUNT);
	}

	return status;
}

extern uint32_t vmexit_reason(void);
uint64_t gcpu_read_guestrip(void);

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_common_handler()
*  PURPOSE  : Called by vmexit_func() upon each VMEXIT
*  ARGUMENTS: void
*  RETURNS  : void
*--------------------------------------------------------------------------*/
void vmexit_common_handler(void)
{
	guest_cpu_handle_t gcpu;
	guest_cpu_handle_t next_gcpu;
	vmcs_object_t *vmcs;
	ia32_vmx_exit_reason_t reason;
	report_initial_vmexit_check_data_t initial_vmexit_check_data;

	gcpu = mon_scheduler_current_gcpu();
	MON_ASSERT(gcpu);

	/* Disable the VMCS Software Shadow/Cache
	 * This is required since GCPU and VMCS cache has not yet been flushed and
	 * might have stale values from previous VMExit */
	vmcs_sw_shadow_disable[hw_cpu_id()] = TRUE;

	if (gcpu->trigger_log_event
	    && (vmexit_reason() ==
		IA32_VMX_EXIT_BASIC_REASON_MONITOR_TRAP_FLAG)) {
		report_mon_log_event_data_t mon_log_event_data;

		mon_log_event_data.vector = gcpu->trigger_log_event - 1;
		gcpu->trigger_log_event = 0;
		report_mon_event(MON_EVENT_LOG, (mon_identification_data_t)gcpu,
			(const guest_vcpu_t *)mon_guest_vcpu(gcpu),
			(void *)&mon_log_event_data);
	}

	/* OPTIMIZATION: Check if current VMExit is MTF VMExit after MTF was turned
	 * on for EPT violation */
	initial_vmexit_check_data.current_cpu_rip = gcpu_read_guestrip();
	initial_vmexit_check_data.vmexit_reason = vmexit_reason();
	if (report_mon_event
		    (MON_EVENT_INITIAL_VMEXIT_CHECK,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    (void *)&initial_vmexit_check_data)) {
		if (fvs_is_eptp_switching_supported()) {
			fvs_save_resumed_eptp(gcpu);
		}

		nmi_window_update_before_vmresume(mon_gcpu_get_vmcs(gcpu));
		vmentry_func(FALSE);
	}

	/* OPTIMIZATION: This has been placed after the MTF VMExit check since
	 * number of MTF VMExits are more compared to Fast View Switch */
	if (mon_fvs_is_fvs_enabled(gcpu)) {
		fvs_vmexit_handler(gcpu);
	}

	MON_ASSERT(hw_cpu_id() < MON_MAX_CPU_SUPPORTED);

	/* OPTIMIZATION: For EPT violation, do not enable the software VMCS cache */
	if ((vmexit_check_ept_violation() & 7) == 0) {
		vmcs_sw_shadow_disable[hw_cpu_id()] = FALSE;
	}

	/* clear guest cpu cache data. in fact it clears all VMCS caches too. */
	gcpu_vmexit_start(gcpu);

	host_cpu_store_vmexit_gcpu(hw_cpu_id(), gcpu);

	if (CLI_active()) {
		/* Check keystroke */
		vmexit_check_keystroke(gcpu);
	}

	if (mon_fvs_is_fvs_enabled(gcpu)) {
		if (fvs_is_eptp_switching_supported()) {
			report_mon_event(MON_EVENT_UPDATE_ACTIVE_VIEW,
				(mon_identification_data_t)gcpu,
				(const guest_vcpu_t *)mon_guest_vcpu(gcpu),
				NULL);
		}
	}

	/* read VMEXIT reason */
	vmcs = mon_gcpu_get_vmcs(gcpu);
	reason.uint32 = (uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_REASON);

	/* MON_LOG(mask_mon, level_trace,"VMEXIT(%d) occurred on CPU(%d) Guest(%d)
	 * GuestCPU(%d)\n",
	 * reason.bits.basic_reason,
	 * hw_cpu_id(),
	 * mon_guest_vcpu(gcpu)->guest_id,
	 * mon_guest_vcpu(gcpu)->guest_cpu_id ); */

	/* call add-on VMEXIT if installed
	 * if add-on is not interesting in this VMEXIT, it retursn NULL
	 * if legacy_scheduling_enabled == FALSE, scheduling must be done in
	 * gcpu_resume() */

	next_gcpu = gcpu_call_vmexit_function(gcpu, reason.bits.basic_reason);

	if (NULL == next_gcpu) {
		/* call reason-specific VMEXIT handler */
		vmexit_handler_invoke(gcpu, reason.bits.basic_reason);
		if (legacy_scheduling_enabled) {
			/* select guest for execution */
			next_gcpu = scheduler_select_next_gcpu();
		} else {
			/* in layered vmresume */
			next_gcpu = gcpu;
		}
	} else {
		scheduler_schedule_gcpu(next_gcpu);
	}

	MON_ASSERT(next_gcpu);

	/* finally process NMI injection */
	NMI_DO_PROCESSING();

	gcpu_resume(next_gcpu);
}

static
guest_vmexit_control_t *vmexit_find_guest_vmexit_control(guest_id_t guest_id)
{
	list_element_t *iter = NULL;
	guest_vmexit_control_t *guest_vmexit_control = NULL;

	LIST_FOR_EACH(vmexit_global_state.guest_vmexit_controls, iter) {
		guest_vmexit_control = LIST_ENTRY(iter,
			guest_vmexit_control_t,
			list);
		if (guest_vmexit_control->guest_id == guest_id) {
			return guest_vmexit_control;
		}
	}

	return NULL;
}

#define vmexit_hardware_interrupt           vmexit_handler_default
#define vmexit_pending_interrupt            vmexit_handler_default
#define vmexit_invalid_instruction          vmexit_handler_default
#define vmexit_dr_access                    vmexit_handler_default
#define vmexit_io_instruction               vmexit_handler_default
#define vmexit_failed_vmenter_guest_state   vmexit_handler_default
#define vmexit_failed_vmenter_msr_loading   vmexit_handler_default
#define vmexit_failed_vmexit                vmexit_handler_default
#define vmexit_mwait_instruction            vmexit_handler_default
#define vmexit_monitor                      vmexit_handler_default
#define vmexit_pause                        vmexit_handler_default
#define vmexit_machine_check                vmexit_handler_default
#define vmexit_tpr_below_threshold          vmexit_handler_default
#define vmexit_apic_access                  vmexit_handler_default

#define CPUID_XSAVE_SUPPORTED_BIT 26

boolean_t is_cr4_osxsave_supported(void)
{
	cpuid_params_t cpuid_params;

	cpuid_params.m_rax = 1;
	hw_cpuid(&cpuid_params);
	return (boolean_t)BIT_GET64(cpuid_params.m_rcx,
		CPUID_XSAVE_SUPPORTED_BIT);
}
