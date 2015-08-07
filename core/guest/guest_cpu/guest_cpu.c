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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(GUEST_CPU_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(GUEST_CPU_C, __condition)
#include "guest_cpu_internal.h"
#include "guest_internal.h"
#include "heap.h"
#include "array_iterators.h"
#include "gpm_api.h"
#include "scheduler.h"
#include "vmx_ctrl_msrs.h"
#include "host_memory_manager_api.h"
#include "vmcs_init.h"
#include "cli.h"
#include "pat_manager.h"
#include "page_walker.h"
#include "mon_startup.h"
#include "memory_allocator.h"
#include "host_cpu.h"
#include "vmx_timer.h"
#include "unrestricted_guest.h"


/*****************************************************************************
*
* Guest CPU
*
* Guest CPU may be in 2 different modes:
* 16 mode - run under emulator
* any other mode - run native
*
*****************************************************************************/

/* -------------------------- types ----------------------------------------- */
/* ---------------------------- globals ------------------------------------- */

/* list of all guest cpus */
static guest_cpu_handle_t g_gcpus;

/* this is a shortcut pointer for assembler code */
guest_cpu_save_area_t **g_guest_regs_save_area = NULL;
static uint32_t g_host_cpu_count;

CLI_CODE(static void gcpu_install_show_service(void);)

/* ---------------------------- internal funcs ----------------------------- */
/*
 * Global gcpu iterator
 */
typedef guest_cpu_handle_t global_guest_cpu_iterator_t;

INLINE guest_cpu_handle_t global_gcpu_first(global_guest_cpu_iterator_t *ctx)
{
	*ctx = g_gcpus;
	return g_gcpus;
}

INLINE guest_cpu_handle_t global_gcpu_next(global_guest_cpu_iterator_t *ctx)
{
	guest_cpu_handle_t gcpu;

	if (ctx == NULL || *ctx == NULL) {
		return NULL;
	}
	gcpu = *ctx;
	*ctx = gcpu->next_gcpu;
	return gcpu->next_gcpu;
}

/* cache debug registers
 * only dr0-dr6 should be cached here, dr7 is in VMCS */
void cache_debug_registers(const guest_cpu_t *gcpu)
{
	/* make volatile */
	guest_cpu_t *vgcpu = (guest_cpu_t *)gcpu;

	if (GET_DEBUG_REGS_CACHED_FLAG(vgcpu)) {
		return;
	}

	SET_DEBUG_REGS_CACHED_FLAG(vgcpu);

	vgcpu->save_area.debug.reg[IA32_REG_DR0] = hw_read_dr(0);
	vgcpu->save_area.debug.reg[IA32_REG_DR1] = hw_read_dr(1);
	vgcpu->save_area.debug.reg[IA32_REG_DR2] = hw_read_dr(2);
	vgcpu->save_area.debug.reg[IA32_REG_DR3] = hw_read_dr(3);
	/* dr4 and dr5 are reserved */
	vgcpu->save_area.debug.reg[IA32_REG_DR6] = hw_read_dr(6);
}

/* cache fx state
 * note, that fx state include mmx registers also, that are wrong at this
 * state,
 * because contain MON and not guest values */
void cache_fx_state(const guest_cpu_t *gcpu)
{
	/* make volatile */
	guest_cpu_t *vgcpu = (guest_cpu_t *)gcpu;

	if (GET_FX_STATE_CACHED_FLAG(vgcpu)) {
		return;
	}

	SET_FX_STATE_CACHED_FLAG(vgcpu);

	hw_fxsave(vgcpu->save_area.fxsave_area);
}

/*
 * perform minimal init of vmcs
 *
 * assumes that all uninit fields are 0 by default, except those that
 * are required to be 1 according to
 * Intel(R) 64 and IA-32 Architectures volume 3B,
 * paragraph 22.3.1 "Checks on the Guest State Area" */
static void setup_default_state(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	MON_ASSERT(vmcs);

	/* init control fields */
	guest_cpu_control_setup(gcpu);

	/* set control registers to any supported value */
	gcpu_set_control_reg(gcpu, IA32_CTRL_CR0, 0);
	gcpu_set_control_reg(gcpu, IA32_CTRL_CR4, 0);
	gcpu_set_control_reg(gcpu, IA32_CTRL_CR8, 0);

	/* set all segment selectors except TR and CS to unusable state
	 * CS: Accessed Code NotSystem NonConforming Present 32bit
	 * bit-granularity */
	gcpu_set_segment_reg(gcpu, IA32_SEG_CS, 0, 0, 0, 0x99);
	gcpu_set_segment_reg(gcpu, IA32_SEG_DS, 0, 0, 0,
		EM64T_SEGMENT_IS_UNUSABLE_ATTRUBUTE_VALUE);
	gcpu_set_segment_reg(gcpu, IA32_SEG_SS, 0, 0, 0,
		EM64T_SEGMENT_IS_UNUSABLE_ATTRUBUTE_VALUE);
	gcpu_set_segment_reg(gcpu, IA32_SEG_ES, 0, 0, 0,
		EM64T_SEGMENT_IS_UNUSABLE_ATTRUBUTE_VALUE);
	gcpu_set_segment_reg(gcpu, IA32_SEG_FS, 0, 0, 0,
		EM64T_SEGMENT_IS_UNUSABLE_ATTRUBUTE_VALUE);
	gcpu_set_segment_reg(gcpu, IA32_SEG_GS, 0, 0, 0,
		EM64T_SEGMENT_IS_UNUSABLE_ATTRUBUTE_VALUE);
	gcpu_set_segment_reg(gcpu, IA32_SEG_LDTR, 0, 0, 0,
		EM64T_SEGMENT_IS_UNUSABLE_ATTRUBUTE_VALUE);
	/* TR: 32bit busy TSS System Present bit-granularity */
	gcpu_set_segment_reg(gcpu, IA32_SEG_TR, 0, 0, 0, 0x8B);

	/* FLAGS: reserved bit 1 must be 1, all other - 0 */
	gcpu_set_gp_reg(gcpu, IA32_REG_RFLAGS, 0x2);

	vmcs_init_all_msr_lists(vmcs);
	host_cpu_init_vmexit_store_and_vmenter_load_msr_lists_according_to_vmexit_load_list
		(gcpu);

	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_EFER, 0);
	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_PAT, hw_read_msr(IA32_MSR_PAT));

	MON_ASSERT(mon_vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_ADDRESS) ==
		mon_vmcs_read(vmcs, VMCS_ENTER_MSR_LOAD_ADDRESS));

	/* by default put guest CPU into the Wait-for-SIPI state */
	MON_ASSERT(mon_vmcs_hw_get_vmx_constraints
			()->vm_entry_in_wait_for_sipi_state_supported);

	gcpu_set_activity_state(gcpu,
		IA32_VMX_VMCS_GUEST_SLEEP_STATE_WAIT_FOR_SIPI);

	mon_vmcs_write(vmcs, VMCS_ENTER_INTERRUPT_INFO, 0);
	mon_vmcs_write(vmcs, VMCS_ENTER_EXCEPTION_ERROR_CODE, 0);
	vmcs_set_launch_required(vmcs);
}

/* ---------------------------- APIs ---------------------------------------- */
void gcpu_manager_init(uint16_t host_cpu_count)
{
	MON_ASSERT(host_cpu_count);

	g_host_cpu_count = host_cpu_count;
	g_guest_regs_save_area =
		mon_memory_alloc(
			sizeof(guest_cpu_save_area_t *) * host_cpu_count);
	MON_ASSERT(g_guest_regs_save_area);

	/* init subcomponents */
	vmcs_hw_init();
	vmcs_manager_init();

	CLI_CODE(gcpu_install_show_service();
		)
}

guest_cpu_handle_t gcpu_allocate(virtual_cpu_id_t vcpu, guest_handle_t guest)
{
	guest_cpu_handle_t gcpu = NULL;
	global_guest_cpu_iterator_t ctx;
	mon_status_t status;

	/* ensure that this vcpu yet not allocated */
	for (gcpu = global_gcpu_first(&ctx); gcpu;
	     gcpu = global_gcpu_next(&ctx)) {
		if ((gcpu->vcpu.guest_id == vcpu.guest_id) &&
		    (gcpu->vcpu.guest_cpu_id == vcpu.guest_cpu_id)) {
			MON_LOG(mask_anonymous,
				level_trace,
				"The CPU %d for the Guest %d was already allocated.\n",
				vcpu.guest_cpu_id,
				vcpu.guest_id);
			MON_ASSERT(FALSE);
			return gcpu;
		}
	}

	/* allocate next gcpu */
	gcpu = (guest_cpu_handle_t)mon_memory_alloc(sizeof(guest_cpu_t));
	MON_ASSERT(gcpu);
	mon_zeromem(gcpu, sizeof(guest_cpu_t));

	gcpu->next_gcpu = g_gcpus;
	g_gcpus = gcpu;

	gcpu->vcpu = vcpu;
	gcpu->last_guest_level = GUEST_LEVEL_1_SIMPLE;
	gcpu->next_guest_level = GUEST_LEVEL_1_SIMPLE;
	gcpu->state_flags = 0;
	gcpu->caching_flags = 0;
	/* gcpu->vmcs = vmcs_allocate(); */
	status = vmcs_hierarchy_create(&gcpu->vmcs_hierarchy, gcpu);
	MON_ASSERT(MON_OK == status);

	gcpu->guest_handle = guest;

	gcpu->active_gpm = NULL;

	SET_MODE_NATIVE(gcpu);
	SET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu);
	SET_CACHED_ACTIVITY_STATE(gcpu, IA32_VMX_VMCS_GUEST_SLEEP_STATE_ACTIVE);

	setup_default_state(gcpu);

	/* default "resume" function */
	gcpu->resume_func = gcpu_perform_split_merge;

	gcpu->fvs_cpu_desc.vmentry_eptp = 0;
	gcpu->fvs_cpu_desc.enabled = FALSE;

	return gcpu;
}

/*--------------------------------------------------------------------------
 *
 * Get Guest CPU state by virtual_cpu_id_t
 *
 * Return NULL if no such guest cpu
 *-------------------------------------------------------------------------- */
guest_cpu_handle_t gcpu_state(const virtual_cpu_id_t *vcpu)
{
	guest_cpu_handle_t gcpu = NULL;
	global_guest_cpu_iterator_t ctx;

	for (gcpu = global_gcpu_first(&ctx); gcpu;
	     gcpu = global_gcpu_next(&ctx)) {
		if ((gcpu->vcpu.guest_id == vcpu->guest_id) &&
		    (gcpu->vcpu.guest_cpu_id == vcpu->guest_cpu_id)) {
			/* found guest cpu */
			return gcpu;
		}
	}

	return NULL;
}

/* get VMCS object to work directly */
vmcs_object_t *mon_gcpu_get_vmcs(guest_cpu_handle_t gcpu)
{
	if (gcpu == NULL) {
		return NULL;
	}

	return vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy, VMCS_MERGED);
	/* return gcpu->vmcs; */
}

vmcs_hierarchy_t *gcpu_get_vmcs_hierarchy(guest_cpu_handle_t gcpu)
{
	if (gcpu == NULL) {
		return NULL;
	}

	return &gcpu->vmcs_hierarchy;
}

vmcs_object_t *gcpu_get_vmcs_layered(guest_cpu_handle_t gcpu,
				     vmcs_level_t level)
{
	if (gcpu == NULL) {
		return NULL;
	}
	return vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy, level);
}

boolean_t gcpu_is_vmcs_layered(guest_cpu_handle_t gcpu)
{
	MON_ASSERT(gcpu);

	return vmcs_hierarchy_is_layered(&gcpu->vmcs_hierarchy);
}

boolean_t gcpu_uses_host_page_tables(guest_cpu_handle_t gcpu)
{
	return gcpu->use_host_page_tables;
}

void gcpu_do_use_host_page_tables(guest_cpu_handle_t gcpu, boolean_t use)
{
	gcpu->use_host_page_tables = (uint8_t)use;
}

/*--------------------------------------------------------------------------
 *
 * Get virtual_cpu_id_t by Guest CPU
 *
 *-------------------------------------------------------------------------- */
const virtual_cpu_id_t *mon_guest_vcpu(const guest_cpu_handle_t gcpu)
{
	if (gcpu == NULL) {
		return NULL;
	}

	return &gcpu->vcpu;
}

/*--------------------------------------------------------------------------
 *
 * Get Guest Handle by Guest CPU
 *
 *-------------------------------------------------------------------------- */
guest_handle_t mon_gcpu_guest_handle(const guest_cpu_handle_t gcpu)
{
	if (gcpu == NULL) {
		return NULL;
	}

	return gcpu->guest_handle;
}

boolean_t gcpu_process_interrupt(vector_id_t vector_id)
{
	return FALSE;
}

/*--------------------------------------------------------------------------
 *
 * Initialize guest CPU
 *
 * Should be called only if initial GCPU state is not Wait-For-Sipi
 *-------------------------------------------------------------------------- */
void gcpu_initialize(guest_cpu_handle_t gcpu,
		     const mon_guest_cpu_startup_state_t *initial_state)
{
	uint32_t idx;

	MON_ASSERT(gcpu);

	if (!initial_state) {
		return;
	}

	if (initial_state->size_of_this_struct !=
	    sizeof(mon_guest_cpu_startup_state_t)) {
		/* wrong state */
		MON_LOG(mask_anonymous, level_trace,
			"gcpu_initialize() called with unknown structure\n");
		MON_DEADLOOP();
		return;
	}

	if (initial_state->version_of_this_struct !=
	    MON_GUEST_CPU_STARTUP_STATE_VERSION) {
		/* wrong version */
		MON_LOG(mask_anonymous, level_trace,
			"gcpu_initialize() called with non-compatible "
			"mon_guest_cpu_startup_state_t "
			"structure: given version: %d expected version: %d\n",
			initial_state->version_of_this_struct,
			MON_GUEST_CPU_STARTUP_STATE_VERSION);

		MON_DEADLOOP();
		return;
	}

	vmcs_set_launch_required(mon_gcpu_get_vmcs(gcpu));

	/* init gp registers */
	for (idx = IA32_REG_RAX; idx < IA32_REG_GP_COUNT; ++idx)
		gcpu_set_gp_reg(gcpu, (mon_ia32_gp_registers_t)idx,
			initial_state->gp.reg[idx]);

	/* init xmm registers */
	for (idx = IA32_REG_XMM0; idx < IA32_REG_XMM_COUNT; ++idx)
		gcpu_set_xmm_reg(gcpu, (mon_ia32_xmm_registers_t)idx,
			initial_state->xmm.reg[idx]);

	/* init segment registers */
	for (idx = IA32_SEG_CS; idx < IA32_SEG_COUNT; ++idx) {
		gcpu_set_segment_reg(gcpu, (mon_ia32_segment_registers_t)idx,
			initial_state->seg.segment[idx].selector,
			initial_state->seg.segment[idx].base,
			initial_state->seg.segment[idx].limit,
			initial_state->seg.segment[idx].attributes);
	}

	/* init control registers */
	for (idx = IA32_CTRL_CR0; idx < IA32_CTRL_COUNT; ++idx) {
		/* Bug fix for EVMM-156
		 * Guest CR4.SMXE bit is not supported by xmon.
		 * XMON should clear this bit during/before XMON launch.
		 */
		if (idx != IA32_CTRL_CR4) {
			gcpu_set_control_reg(gcpu, (mon_ia32_control_registers_t)idx,
				initial_state->control.cr[idx]);
			gcpu_set_guest_visible_control_reg(gcpu,
				(mon_ia32_control_registers_t)idx,
				initial_state->control.cr[idx]);
		} else {
			gcpu_set_control_reg(gcpu, (mon_ia32_control_registers_t)idx,
				(initial_state->control.cr[idx])&(~(uint64_t)(CR4_SMXE)));
			gcpu_set_guest_visible_control_reg(gcpu,
				(mon_ia32_control_registers_t)idx,
				(initial_state->control.cr[idx])&(~(uint64_t)(CR4_SMXE)));
		}
	}

	gcpu_set_gdt_reg(gcpu, initial_state->control.gdtr.base,
		initial_state->control.gdtr.limit);
	gcpu_set_idt_reg(gcpu, initial_state->control.idtr.base,
		initial_state->control.idtr.limit);

	/* init selected model-specific registers */
	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_DEBUGCTL,
		initial_state->msr.msr_debugctl);
	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_EFER, initial_state->msr.msr_efer);
	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_PAT, initial_state->msr.msr_pat);
	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_SYSENTER_ESP,
		initial_state->msr.msr_sysenter_esp);
	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_SYSENTER_EIP,
		initial_state->msr.msr_sysenter_eip);
	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_SYSENTER_CS,
		initial_state->msr.msr_sysenter_cs);
	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_SMBASE, initial_state->msr.smbase);

	gcpu_set_pending_debug_exceptions(gcpu,
		initial_state->msr.pending_exceptions);
	gcpu_set_interruptibility_state(gcpu,
		initial_state->msr.interruptibility_state);

	/* set cached value to the same in order not to trigger events */
	gcpu_set_activity_state(gcpu, (ia32_vmx_vmcs_guest_sleep_state_t)
		initial_state->msr.activity_state);
	/* set state in vmenter control fields */
	gcpu_set_vmenter_control(gcpu);

	cache_fx_state(gcpu);
	cache_debug_registers(gcpu);

	SET_MODE_NATIVE(gcpu);
	SET_ALL_MODIFIED(gcpu);
}

/*
 *  Find the GPA corresponding to the GVA, for the indicated CR3
 *  If CR3 is 0, the current CR3 should be used in the page walk.
 */
boolean_t mon_gcpu_gva_to_gpa(guest_cpu_handle_t gcpu,
			      gva_t gva,
			      uint64_t cr3,
			      gpa_t *gpa)
{
	uint64_t gpa_tmp;
	uint64_t pfec_tmp;
	boolean_t res;
	em64t_cr0_t visible_cr0;

	visible_cr0.uint64 =
		gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR0);

	/* GVA = GPA in non-paged mode */
	if (mon_is_unrestricted_guest_supported() && !visible_cr0.bits.pg) {
		*gpa = gva;
		return TRUE;
	}

	if (IS_FLAT_PT_INSTALLED(gcpu)) {
		*gpa = gva;
		return TRUE;
	} else {
		res =
			pw_perform_page_walk(gcpu,
				gva,
				cr3,
				FALSE,
				FALSE,
				FALSE,
				FALSE,
				&gpa_tmp,
				&pfec_tmp);
		if (res == PW_RETVAL_SUCCESS) {
			*gpa = gpa_tmp;
			return TRUE;
		}
	}

	return FALSE;
}

boolean_t gcpu_gva_to_hva(guest_cpu_handle_t gcpu, gva_t gva, hva_t *hva)
{
	guest_handle_t guest_handle;
	gpm_handle_t gpm_handle;
	uint64_t gpa;
	uint64_t hva_tmp;

	if (!mon_gcpu_gva_to_gpa(gcpu, gva, 0, &gpa)) {
		MON_LOG(mask_mon,
			level_error,
			"%s: Failed to convert gva=%P to gpa\n",
			__FUNCTION__,
			gva);
		return FALSE;
	}

	guest_handle = mon_gcpu_guest_handle(gcpu);
	gpm_handle = gcpu_get_current_gpm(guest_handle);

	if (!gpm_gpa_to_hva(gpm_handle, gpa, &hva_tmp)) {
		MON_LOG(mask_mon,
			level_error,
			"%s: Failed to convert gpa=%P to hva\n",
			__FUNCTION__,
			gpa);
		return FALSE;
	}

	*hva = hva_tmp;
	return TRUE;
}

guest_cpu_handle_t gcpu_call_vmexit_function(guest_cpu_handle_t gcpu,
					     uint32_t reason)
{
	if (gcpu->vmexit_func) {
		return gcpu->vmexit_func(gcpu, reason);
	} else {
		return NULL;
	}
}

#define PRINT_GP_REG(__gcpu, __reg)               \
	CLI_PRINT("\t%13s (addr=%P): %P\n", #__reg,   \
	&(__gcpu->save_area.gp.reg[__reg]), \
	__gcpu->save_area.gp.reg[__reg]);

CLI_CODE(
	int gcpu_show_gp_registers(unsigned argc, char *args[])
	{
		guest_id_t guest_id;
		guest_cpu_handle_t gcpu;
		if (argc < 2) {
			return -1;
		}
		guest_id = (guest_id_t)CLI_ATOL(args[1]);
		gcpu = scheduler_get_current_gcpu_for_guest(guest_id);
		if (NULL == gcpu) {
			return -1;
		}
		CLI_PRINT("=============================================\n");
		PRINT_GP_REG(gcpu, IA32_REG_RAX);
		PRINT_GP_REG(gcpu, IA32_REG_RBX);
		PRINT_GP_REG(gcpu, IA32_REG_RCX);
		PRINT_GP_REG(gcpu, IA32_REG_RDX);
		PRINT_GP_REG(gcpu, IA32_REG_RDI);
		PRINT_GP_REG(gcpu, IA32_REG_RSI);
		PRINT_GP_REG(gcpu, IA32_REG_RBP);
		PRINT_GP_REG(gcpu, IA32_REG_R8);
		PRINT_GP_REG(gcpu, IA32_REG_R9);
		PRINT_GP_REG(gcpu, IA32_REG_R10);
		PRINT_GP_REG(gcpu, IA32_REG_R11);
		PRINT_GP_REG(gcpu, IA32_REG_R12);
		PRINT_GP_REG(gcpu, IA32_REG_R13);
		PRINT_GP_REG(gcpu, IA32_REG_R14);
		CLI_PRINT("\n");
		PRINT_GP_REG(gcpu, CR2_SAVE_AREA);
		PRINT_GP_REG(gcpu, CR3_SAVE_AREA);
		PRINT_GP_REG(gcpu, CR8_SAVE_AREA);
		CLI_PRINT("\t%s : %P\n", "Guest visible CR0",
			gcpu_get_guest_visible_control_reg(gcpu,
				IA32_CTRL_CR0));
		CLI_PRINT("\t%s : %P\n", "Guest visible CR4",
			gcpu_get_guest_visible_control_reg(gcpu,
				IA32_CTRL_CR4));
		CLI_PRINT("=============================================\n");
		return 0;
	}
	) /* End Of CLI_CODE */
CLI_CODE(
	void gcpu_install_show_service(void)
	{
		cli_add_command(gcpu_show_gp_registers,
			"debug guest show registers",
			"Print Guest CPU General Purpose Registers on"
			" current CPU",
			"<guest_id>", CLI_ACCESS_LEVEL_USER);
	}
	) /* End Of CLI_CODE */

void gcpu_change_level0_vmexit_msr_load_list(guest_cpu_handle_t gcpu,
					     ia32_vmx_msr_entry_t *msr_list,
					     uint32_t msr_list_count)
{
	uint64_t addr = 0;
	vmcs_object_t *level0_vmcs =
		vmcs_hierarchy_get_vmcs(gcpu_get_vmcs_hierarchy(
				gcpu), VMCS_LEVEL_0);

	if (gcpu_get_guest_level(gcpu) == GUEST_LEVEL_1_SIMPLE) {
		MON_ASSERT(vmcs_hierarchy_get_vmcs
				(gcpu_get_vmcs_hierarchy(
					gcpu), VMCS_MERGED) == level0_vmcs);

		if ((msr_list_count != 0) &&
		    (!mon_hmm_hva_to_hpa((hva_t)msr_list, &addr))) {
			MON_LOG(mask_anonymous,
				level_trace,
				"%s: Failed to convert hva_t to hpa_t\n",
				__FUNCTION__);
			MON_DEADLOOP();
		}
	} else {
		/* When layering HVA is stored */
		addr = (uint64_t)msr_list;
	}

	mon_vmcs_write(level0_vmcs, VMCS_EXIT_MSR_LOAD_ADDRESS, addr);
	mon_vmcs_write(level0_vmcs, VMCS_EXIT_MSR_LOAD_COUNT, msr_list_count);
}
