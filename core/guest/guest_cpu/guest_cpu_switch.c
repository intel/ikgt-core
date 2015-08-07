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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(GUEST_CPU_SWITCH_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(GUEST_CPU_SWITCH_C, __condition)
#include "guest_cpu_internal.h"
#include "vmx_ctrl_msrs.h"
#include "gpm_api.h"
#include "guest.h"
#include "vmx_asm.h"
#include "ipc.h"
#include "mon_dbg.h"
#include "mon_events_data.h"
#include "vmcs_merge_split.h"
#include "vmcs_api.h"
#include "vmexit_cr_access.h"
#include "pat_manager.h"
#include "vmx_nmi.h"
#include "host_cpu.h"
#include "vmdb.h"
#include "vmcs_init.h"
#include "unrestricted_guest.h"
#include "fvs.h"
#include "ept.h"

extern boolean_t is_ib_registered(void);


/* -------------------------- types ----------------------------------------- */

/*
 * Decide on important events
 */
typedef enum {
	GCPU_RESUME_EMULATOR_ACTION_DO_NOTHING = 0,
	GCPU_RESUME_EMULATOR_ACTION_START_EMULATOR
} gcpu_resume_emulator_action_t;

typedef enum {
	GCPU_RESUME_FLAT_PT_ACTION_DO_NOTHING = 0,
	GCPU_RESUME_FLAT_PT_ACTION_INSTALL_32_BIT_PT,
	GCPU_RESUME_FLAT_PT_ACTION_INSTALL_64_BIT_PT,
	GCPU_RESUME_FLAT_PT_ACTION_REMOVE
} gcpu_resume_flat_pt_action_t;

typedef struct {
	gcpu_resume_emulator_action_t	emulator;
	gcpu_resume_flat_pt_action_t	flat_pt;
} gcpu_resume_action_t;

typedef enum {                  /* bit values */
	VMCS_HW_ENFORCE_EMULATOR = 1,
	VMCS_HW_ENFORCE_FLAT_PT = 2,
	VMCS_HW_ENFORCE_CACHE_DISABLED = 4,
} vmcs_hw_enforcement_id_t;

extern boolean_t vmcs_sw_shadow_disable[];

static
mon_status_t gcpu_set_hw_enforcement(guest_cpu_handle_t gcpu,
				     vmcs_hw_enforcement_id_t enforcement);
static
mon_status_t gcpu_remove_hw_enforcement(guest_cpu_handle_t gcpu,
					vmcs_hw_enforcement_id_t
					enforcement);
static
void gcpu_apply_hw_enforcements(guest_cpu_handle_t gcpu);
#define gcpu_hw_enforcement_is_active(gcpu, enforcement) \
	(((gcpu)->hw_enforcements & enforcement) != 0)

/* ---------------------------- globals ------------------------------------- */
/* ---------------------------- internal funcs ----------------------------- */

static
void gcpu_cache_disabled_support(const guest_cpu_handle_t gcpu,
				 boolean_t CD_value_requested)
{
	if (1 == CD_value_requested) { /* cache disabled - WSM does not support this! */
		if (!gcpu_hw_enforcement_is_active
			    (gcpu, VMCS_HW_ENFORCE_CACHE_DISABLED)) {
			MON_LOG(mask_anonymous, level_trace,
				"Guest %d:%d trying to set cd = 1\n",
				mon_guest_vcpu(gcpu)->guest_id,
				mon_guest_vcpu(gcpu)->guest_cpu_id);
			gcpu_set_hw_enforcement(gcpu,
				VMCS_HW_ENFORCE_CACHE_DISABLED);
		}
	} else {
		if (gcpu_hw_enforcement_is_active(gcpu,
			    VMCS_HW_ENFORCE_CACHE_DISABLED)) {
			MON_LOG(mask_anonymous, level_trace,
				"Guest %d:%d removing cd = 0 enforcement\n",
				mon_guest_vcpu(gcpu)->guest_id,
				mon_guest_vcpu(gcpu)->guest_cpu_id);
			gcpu_remove_hw_enforcement(gcpu,
				VMCS_HW_ENFORCE_CACHE_DISABLED);
		}
	}
}

/*
 * Receives cr0 and efer guest-visible values
 * returns TRUE is something should be done + description of what should be
 * done */
static
boolean_t gcpu_decide_on_resume_actions(const guest_cpu_handle_t gcpu,
					uint64_t cr0_value,
					uint64_t efer_value,
					gcpu_resume_action_t *action)
{
	em64t_cr0_t cr0;
	ia32_efer_t efer;
	boolean_t do_something = FALSE;
	boolean_t pe, pg, cd, lme;

	MON_ASSERT(gcpu);
	MON_ASSERT(action);

	if (IS_MODE_EMULATOR(gcpu)) {
		/* if we under emulator, emulator will take care for everything */
		return FALSE;
	}

	action->emulator = GCPU_RESUME_EMULATOR_ACTION_DO_NOTHING;
	action->flat_pt = GCPU_RESUME_FLAT_PT_ACTION_DO_NOTHING;

	/* now we in NATIVE mode only */

	if (IS_STATE_INACTIVE(GET_CACHED_ACTIVITY_STATE(gcpu))) {
		/* if we are in the wait-for-SIPI mode - do nothing */
		return FALSE;
	}

	cr0.uint64 = cr0_value;
	efer.uint64 = efer_value;

	pe = (cr0.bits.pe == 1);
	pg = (cr0.bits.pg == 1);
	cd = (cr0.bits.cd == 1);
	lme = (efer.bits.lme == 1);

	if (cd && global_policy_is_cache_dis_virtualized()) {
		em64t_cr0_t real_cr0;

		MON_DEBUG_CODE(const virtual_cpu_id_t *vcpu =
				mon_guest_vcpu(gcpu);
			MON_LOG(mask_anonymous, level_trace,
				"Guest %d:%d trying to set cd = 1\n",
				(int)vcpu->guest_id, (int)vcpu->guest_cpu_id);
			);

		/* CD = 1 is not allowed. */
		real_cr0.uint64 = gcpu_get_control_reg(gcpu, IA32_CTRL_CR0);
		real_cr0.bits.cd = 0;
		gcpu_set_control_reg(gcpu, IA32_CTRL_CR0, real_cr0.uint64);
	}

	/* Run emulator explicitly */
	if (GET_EXPLICIT_EMULATOR_REQUEST_FLAG(gcpu)) {
		if (IS_MODE_UNRESTRICTED_GUEST(gcpu)) {
			mon_unrestricted_guest_disable(gcpu);
		}
		/* if we start emulator, emulator will take care for everything */
		action->emulator = GCPU_RESUME_EMULATOR_ACTION_START_EMULATOR;
		return TRUE;
	}
	/* We have UG on all the time, except during Task Switch */
	if (mon_is_unrestricted_guest_supported()) {
		if (!IS_MODE_UNRESTRICTED_GUEST(gcpu)) {
			if (mon_ept_is_ept_enabled(gcpu) || (pg == FALSE)) {
				mon_unrestricted_guest_enable(gcpu);
			}
		}
	}
	if (pe == FALSE) {
		if (!mon_is_unrestricted_guest_supported()) {
			/* if we start emulator, emulator will take care for everything */
			action->emulator =
				GCPU_RESUME_EMULATOR_ACTION_START_EMULATOR;
			do_something = TRUE;
		}
		return do_something;
	}

	/* now PE is 1 */
	if (!mon_is_unrestricted_guest_supported()) {
		if (pg == FALSE) {
			/* paging is off -> we need flat page tables. */
			if ((lme == FALSE) &&
			    (!GET_FLAT_PAGES_TABLES_32_FLAG(gcpu))) {
				do_something = TRUE;
				action->flat_pt =
					GCPU_RESUME_FLAT_PT_ACTION_INSTALL_32_BIT_PT;
			}

			/* special case - Paging is OFF but Long Mode Enable (LME) is ON
			 * -> switch from 32bit to 64 bit page tables even is 32bit exist */
			if ((lme == TRUE) &&
			    (!GET_FLAT_PAGES_TABLES_64_FLAG(gcpu))) {
				do_something = TRUE;
				action->flat_pt =
					GCPU_RESUME_FLAT_PT_ACTION_INSTALL_64_BIT_PT;
			}
		}
		/* Paging is ON */
		else {
			if (IS_FLAT_PT_INSTALLED(gcpu)) {
				do_something = TRUE;
				action->flat_pt =
					GCPU_RESUME_FLAT_PT_ACTION_REMOVE;
			}
		}
	}

	if (global_policy_is_cache_dis_virtualized()) {
		gcpu_cache_disabled_support(gcpu, cd);
	}

	return do_something;
}

/*
 * Working with flat page tables
 */

/* called each time before resume if flat page tables are active */
static
void gcpu_enforce_flat_memory_setup(guest_cpu_t *gcpu)
{
	em64t_cr4_t cr4;
	em64t_cr0_t cr0;

	MON_ASSERT(IS_FLAT_PT_INSTALLED(gcpu));
	MON_ASSERT(gcpu->active_flat_pt_hpa);

	gcpu_set_control_reg(gcpu, IA32_CTRL_CR3, gcpu->active_flat_pt_hpa);

	cr4.uint64 = gcpu_get_control_reg(gcpu, IA32_CTRL_CR4);
	cr0.uint64 = gcpu_get_control_reg(gcpu, IA32_CTRL_CR0);

	/* set required bits
	 * note: CR4.PAE ... are listed in the GCPU_CR4_MON_CONTROLLED_BITS
	 * so their real values will not be visible by guest */
	if (!(cr4.bits.pae && cr4.bits.pse)) {
		cr4.bits.pae = 1;
		cr4.bits.pse = 1;
		gcpu_set_control_reg(gcpu, IA32_CTRL_CR4, cr4.uint64);
	}

	/* note: CR0.PG ... are listed in the GCPU_CR0_MON_CONTROLLED_BITS
	 * so their real values will not be visible by guest */
	if (!cr0.bits.pg) {
		cr0.bits.pg = 1;
		gcpu_set_control_reg(gcpu, IA32_CTRL_CR0, cr0.uint64);
	}
}

static
void gcpu_install_flat_memory(guest_cpu_t *gcpu,
			      gcpu_resume_flat_pt_action_t pt_type)
{
	boolean_t gpm_flat_page_tables_ok = FALSE;

	if (IS_FLAT_PT_INSTALLED(gcpu)) {
		fpt_destroy_flat_page_tables(gcpu->active_flat_pt_handle);
	} else {
		/* first time install - save current user CR3 */
		if (INVALID_CR3_SAVED_VALUE ==
		    gcpu->save_area.gp.reg[CR3_SAVE_AREA]) {
			gcpu->save_area.gp.reg[CR3_SAVE_AREA] =
				gcpu_get_control_reg(gcpu, IA32_CTRL_CR3);
		}
	}

	if (GCPU_RESUME_FLAT_PT_ACTION_INSTALL_32_BIT_PT == pt_type) {
		uint32_t cr3_hpa;

		/* MON_LOG(mask_anonymous,
		 * level_trace,"gcpu_install_32bit_flat_memory()\n"); */

		gpm_flat_page_tables_ok =
			fpt_create_32_bit_flat_page_tables(gcpu,
				&(gcpu->active_flat_pt_handle),
				&cr3_hpa);
		gcpu->active_flat_pt_hpa = cr3_hpa;

		CLR_FLAT_PAGES_TABLES_64_FLAG(gcpu);
		SET_FLAT_PAGES_TABLES_32_FLAG(gcpu);
	} else if (GCPU_RESUME_FLAT_PT_ACTION_INSTALL_64_BIT_PT == pt_type) {
		/* MON_LOG(mask_anonymous,
		 * level_trace,"gcpu_install_64bit_flat_memory()\n"); */

		gpm_flat_page_tables_ok =
			fpt_create_64_bit_flat_page_tables(gcpu,
				&(gcpu->active_flat_pt_handle),
				&(gcpu->active_flat_pt_hpa));
		CLR_FLAT_PAGES_TABLES_32_FLAG(gcpu);
		SET_FLAT_PAGES_TABLES_64_FLAG(gcpu);
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"Unknown Flat Page Tables type: %d\n", pt_type);
		MON_DEADLOOP();
	}

	MON_ASSERT(gpm_flat_page_tables_ok);

	gcpu_set_hw_enforcement(gcpu, VMCS_HW_ENFORCE_FLAT_PT);
}

static
void gcpu_destroy_flat_memory(guest_cpu_t *gcpu)
{
	em64t_cr4_t user_cr4;
	raise_event_retval_t event_retval;

	/* MON_LOG(mask_anonymous, level_trace,"gcpu_destroy_flat_memory()\n"); */
	if (IS_FLAT_PT_INSTALLED(gcpu)) {
		fpt_destroy_flat_page_tables(gcpu->active_flat_pt_handle);
		gcpu->active_flat_pt_hpa = 0;
	}

	/* now we should restore the original PAE and PSE bits
	 * actually we should ask MON-based application about this by
	 * issuing appropriate event */
	user_cr4.uint64 =
		gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR4);
	gcpu_set_control_reg(gcpu, IA32_CTRL_CR4, user_cr4.uint64);
	event_retval = cr_raise_write_events(gcpu,
		IA32_CTRL_CR4,
		user_cr4.uint64);
	MON_ASSERT(event_retval != EVENT_NOT_HANDLED);

	gcpu_set_control_reg(gcpu,
		IA32_CTRL_CR3,
		gcpu->save_area.gp.reg[CR3_SAVE_AREA]);
	event_retval =
		cr_raise_write_events(gcpu,
			IA32_CTRL_CR3,
			gcpu->save_area.gp.reg[CR3_SAVE_AREA]);
	MON_ASSERT(event_retval != EVENT_NOT_HANDLED);

	gcpu_remove_hw_enforcement(gcpu, VMCS_HW_ENFORCE_FLAT_PT);

	CLR_FLAT_PAGES_TABLES_32_FLAG(gcpu);
	CLR_FLAT_PAGES_TABLES_64_FLAG(gcpu);
}

void mon_gcpu_physical_memory_modified(guest_cpu_handle_t gcpu)
{
	boolean_t gpm_flat_page_tables_ok = FALSE;

	/* this function is called after somebody modified guest physical memory
	 * renew flat page tables if required */
	if (!IS_FLAT_PT_INSTALLED(gcpu)) {
		return;
	}

	fpt_destroy_flat_page_tables(gcpu->active_flat_pt_handle);

	if (GET_FLAT_PAGES_TABLES_32_FLAG(gcpu)) {
		uint32_t cr3_hpa;

		gpm_flat_page_tables_ok = fpt_create_32_bit_flat_page_tables(
			gcpu,
			&(gcpu->active_flat_pt_handle),
			&cr3_hpa);
		gcpu->active_flat_pt_hpa = cr3_hpa;
	} else if (GET_FLAT_PAGES_TABLES_64_FLAG(gcpu)) {
		gpm_flat_page_tables_ok =
			fpt_create_64_bit_flat_page_tables(gcpu,
				&(gcpu->active_flat_pt_handle),
				&(gcpu->active_flat_pt_hpa));
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"Unknown Flat Page Tables type during FPT update"
			" after GPM modification\n");
		MON_DEADLOOP();
	}

	MON_ASSERT(gpm_flat_page_tables_ok);
}

/*
 * Perform pre-resume actions
 */
static
void gcpu_perform_resume_actions(guest_cpu_t *gcpu,
				 const gcpu_resume_action_t *action)
{
	MON_ASSERT(gcpu);
	MON_ASSERT(IS_MODE_NATIVE(gcpu));
	MON_ASSERT(action);

	switch (action->flat_pt) {
	case GCPU_RESUME_FLAT_PT_ACTION_INSTALL_32_BIT_PT:
		gcpu_install_flat_memory(gcpu,
			GCPU_RESUME_FLAT_PT_ACTION_INSTALL_32_BIT_PT);
		break;

	case GCPU_RESUME_FLAT_PT_ACTION_INSTALL_64_BIT_PT:
		gcpu_install_flat_memory(gcpu,
			GCPU_RESUME_FLAT_PT_ACTION_INSTALL_64_BIT_PT);
		break;

	case GCPU_RESUME_FLAT_PT_ACTION_REMOVE:
		gcpu_destroy_flat_memory(gcpu);
		break;

	case GCPU_RESUME_FLAT_PT_ACTION_DO_NOTHING:
		break;

	default:
		MON_LOG(mask_anonymous, level_trace,
			"Unknown GCPU pre-resume flat_pt action value: %d\n",
			action->flat_pt);
		MON_DEADLOOP();
	}
}

/* ---------------------------- APIs --------------------------------------- */

/*---------------------------------------------------------------------------
 *
 * Context switching
 *
 *-------------------------------------------------------------------------- */

/* perform full state save before switching to another guest */
void gcpu_swap_out(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	/* save state that is not saved by default */
	if (!GET_DEBUG_REGS_CACHED_FLAG(gcpu)) {
		cache_debug_registers(gcpu);
	}

	if (!GET_FX_STATE_CACHED_FLAG(gcpu)) {
		cache_fx_state(gcpu);
	}

	vmcs_deactivate(vmcs);
}

/* perform state restore after switching from another guest */
void gcpu_swap_in(const guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	/* make global assembler save area for this host CPU point to new guest */
	g_guest_regs_save_area[hw_cpu_id()] = &(gcpu->save_area);

	vmcs_activate(vmcs);

	SET_ALL_MODIFIED(gcpu);
}

/*--------------------------------------------------------------------------
 *
 * Initialize gcpu environment for each VMEXIT
 * Must be the first gcpu call in each VMEXIT
 *
 *-------------------------------------------------------------------------- */
void gcpu_vmexit_start(const guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	/* save current */
	gcpu->save_area.gp.reg[CR2_SAVE_AREA] = hw_read_cr2();
	/* CR3 should not be saved because guest asccess CR3 always causes VmExit
	 * and should be cached by CR3-access handler */
	gcpu->save_area.gp.reg[CR8_SAVE_AREA] = hw_read_cr8();

	if (!vmcs_sw_shadow_disable[hw_cpu_id()]) {
		CLR_ALL_CACHED(gcpu);
		vmcs_clear_cache(vmcs);
	}
	/* if CR3 is not virtualized, update
	 * internal storage with user-visible guest value */
	if (IS_MODE_NATIVE(gcpu) &&
	    !IS_FLAT_PT_INSTALLED(gcpu) && !gcpu_cr3_virtualized(gcpu)) {
		gcpu_set_guest_visible_control_reg(gcpu,
			IA32_CTRL_CR3,
			INVALID_CR3_SAVED_VALUE);
	}
}

void gcpu_raise_proper_events_after_level_change(guest_cpu_handle_t gcpu,
						 merge_orig_values_t *optional)
{
	uint64_t value;
	raise_event_retval_t update_event;
	event_gcpu_guest_msr_write_data_t msr_update_data;

	value = gcpu_get_guest_visible_control_reg_layered(gcpu,
		IA32_CTRL_CR0,
		VMCS_MERGED);
	if (optional && optional->visible_cr0 == value) {
		update_event =
			cr_raise_write_events(gcpu, IA32_CTRL_CR0, value);
		MON_ASSERT(update_event != EVENT_NOT_HANDLED); /* Mustn't be GPF0 */
	}

	value = gcpu_get_guest_visible_control_reg_layered(gcpu,
		IA32_CTRL_CR4,
		VMCS_MERGED);
	if (optional && optional->visible_cr4 == value) {
		update_event =
			cr_raise_write_events(gcpu, IA32_CTRL_CR4, value);
		MON_ASSERT(update_event != EVENT_NOT_HANDLED); /* Mustn't be GPF0 */
	}

	value = gcpu_get_msr_reg_layered(gcpu, IA32_MON_MSR_EFER, VMCS_MERGED);
	if (optional && optional->efer == value) {
		msr_update_data.msr_index = IA32_MSR_EFER;
		msr_update_data.new_guest_visible_value = value;
		update_event = event_raise(EVENT_GCPU_AFTER_EFER_MSR_WRITE,
			gcpu,
			&msr_update_data);
	}

	if (optional && optional->visible_cr3 == value) {
		value = gcpu_get_guest_visible_control_reg_layered(gcpu,
			IA32_CTRL_CR3,
			VMCS_MERGED);
		update_event =
			cr_raise_write_events(gcpu, IA32_CTRL_CR3, value);
		/* Mustn't be GPF0 */
		MON_ASSERT(update_event != EVENT_NOT_HANDLED);
	}

	/* PAT update will be tracked later in resume */
}

guest_cpu_handle_t gcpu_perform_split_merge(guest_cpu_handle_t gcpu)
{
	vmcs_hierarchy_t *hierarchy = &(gcpu->vmcs_hierarchy);
	vmcs_object_t *level0_vmcs;
	vmcs_object_t *level1_vmcs;

	if ((gcpu->last_guest_level == GUEST_LEVEL_1_SIMPLE) &&
	    (gcpu->last_guest_level == gcpu->next_guest_level)) {
		MON_ASSERT(mon_vmcs_read
				(vmcs_hierarchy_get_vmcs(hierarchy,
					VMCS_LEVEL_0),
				VMCS_EXIT_MSR_STORE_ADDRESS) ==
			mon_vmcs_read(vmcs_hierarchy_get_vmcs(hierarchy,
					VMCS_LEVEL_0),
				VMCS_ENTER_MSR_LOAD_ADDRESS));
		MON_ASSERT(vmcs_hierarchy_get_vmcs(hierarchy, VMCS_LEVEL_0) ==
			vmcs_hierarchy_get_vmcs(hierarchy, VMCS_MERGED));
		return gcpu;
	}

	level0_vmcs = vmcs_hierarchy_get_vmcs(hierarchy, VMCS_LEVEL_0);
	level1_vmcs = vmcs_hierarchy_get_vmcs(hierarchy, VMCS_LEVEL_1);

	if (gcpu->last_guest_level != gcpu->next_guest_level) {
		if (gcpu->last_guest_level == GUEST_LEVEL_1_SIMPLE) {
			MON_ASSERT(gcpu->next_guest_level == GUEST_LEVEL_1_MON);
			/* TODO: separate "level-0" and "merged" VMCSs */
			MON_LOG(mask_anonymous,
				level_trace,
				"%s: Separation of (level-0) and (merged) VMCSs is"
				" not implemented yet\n",
				__FUNCTION__);
			MON_DEADLOOP();
		} else if (gcpu->last_guest_level == GUEST_LEVEL_1_MON) {
			if (gcpu->next_guest_level == GUEST_LEVEL_1_SIMPLE) {
				/* TODO: (level-1) --> simple guest mode */
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Layering switch off is not implemented yet\n",
					__FUNCTION__);
				MON_DEADLOOP();
			} else {
				MON_ASSERT(
					gcpu->next_guest_level ==
					GUEST_LEVEL_2);
				ms_merge_to_level2(gcpu,
					FALSE /* merge all fields */);
			}
		} else {
			MON_ASSERT(gcpu->next_guest_level == GUEST_LEVEL_2);
			MON_ASSERT(gcpu->next_guest_level == GUEST_LEVEL_1_MON);

			ms_split_from_level2(gcpu);
			/* vmexit level2 -> level1 merge all fields */
			ms_merge_to_level1(gcpu, FALSE, FALSE);
		}

		gcpu_raise_proper_events_after_level_change(gcpu, NULL);
	} else {
		/* gcpu->last_guest_level == gcpu->next_guest_level */
		if (gcpu->last_guest_level == GUEST_LEVEL_1_MON) {
			boolean_t merge_only_dirty =
				GET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu) ? FALSE :
				TRUE;

			ms_merge_to_level1(gcpu, TRUE /* level1 -> level1 */,
				merge_only_dirty);
		} else {
			boolean_t merge_only_dirty =
				GET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu) ? FALSE :
				TRUE;

			MON_ASSERT(gcpu->last_guest_level == GUEST_LEVEL_2)
			ms_merge_to_level2(gcpu, merge_only_dirty);
		}
	}

	vmcs_clear_dirty(level0_vmcs);
	vmcs_clear_dirty(level1_vmcs);

	return gcpu;
}

static
void gcpu_process_activity_state_change(guest_cpu_handle_t gcpu)
{
	event_gcpu_activity_state_change_data_t event_data;

	event_data.new_state = gcpu_get_activity_state(gcpu);
	event_data.prev_state = GET_CACHED_ACTIVITY_STATE(gcpu);

	if (event_data.new_state != event_data.prev_state) {
		event_raise(EVENT_GCPU_ACTIVITY_STATE_CHANGE, gcpu,
			&event_data);
		SET_CACHED_ACTIVITY_STATE(gcpu, event_data.new_state);

		if (IS_STATE_INACTIVE(event_data.new_state)) {
			/* switched from active to Wait-For-SIPI
			 * the HW CPU will not be able to respond to any interrupts */
			ipc_change_state_to_sipi(gcpu);
		}

		if (IS_STATE_INACTIVE(event_data.prev_state)) {
			/* switched from Wait-For-SIPI to active state */

			ipc_change_state_to_active(gcpu);
		}
	}

	CLR_ACTIVITY_STATE_CHANGED_FLAG(gcpu);
}

/*---------------------------------------------------------------------------
 *
 * Resume execution.
 * never returns.
 *
 *-------------------------------------------------------------------------- */
void gcpu_resume(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs;

	if (IS_MODE_NATIVE(gcpu)) {
		gcpu = gcpu->resume_func(gcpu); /* layered specific resume */
		gcpu->last_guest_level = gcpu->next_guest_level;
	}

	vmcs = mon_gcpu_get_vmcs(gcpu);
	MON_ASSERT(vmcs);

	MON_ASSERT(0 == GET_EXCEPTION_RESOLUTION_REQUIRED_FLAG(gcpu));

	if (GET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu)) {
		if (GET_ACTIVITY_STATE_CHANGED_FLAG(gcpu)) {
			gcpu_process_activity_state_change(gcpu);
		}

		/* if we in the emulator, it will take care about all settings */
		if (IS_MODE_NATIVE(gcpu)) {
			gcpu_resume_action_t action;

			if (gcpu_decide_on_resume_actions(gcpu,
				    gcpu_get_guest_visible_control_reg
					    (gcpu, IA32_CTRL_CR0),
				    gcpu_get_msr_reg(gcpu,
					    IA32_MON_MSR_EFER),
				    &action)) {
				/* do something */
				gcpu_perform_resume_actions(gcpu, &action);
			}
		}

		CLR_IMPORTANT_EVENT_OCCURED_FLAG(gcpu);
	}

	/* support for active CR3 */
	if (IS_MODE_NATIVE(gcpu)) {
		if (IS_FLAT_PT_INSTALLED(gcpu)) {
			/* gcpu_enforce_flat_memory_setup( gcpu ); VTDBG */
		} else {
			if (!gcpu_cr3_virtualized(gcpu)) {
				uint64_t visible_cr3 =
					gcpu->save_area.gp.reg[CR3_SAVE_AREA];

				if (INVALID_CR3_SAVED_VALUE != visible_cr3) {
					/* CR3 user-visible value was changed inside mon or CR3
					 * virtualization was switched off */
					gcpu_set_control_reg(gcpu,
						IA32_CTRL_CR3,
						visible_cr3);
				}
			}
		}
	}

	if (fvs_is_eptp_switching_supported()) {
		fvs_save_resumed_eptp(gcpu);
	}

	/* restore registers */
	hw_write_cr2(gcpu->save_area.gp.reg[CR2_SAVE_AREA]);
	/* CR3 should not be restored because guest asccess CR3 always causes
	 * VmExit and should be cached by CR3-access handler */
	hw_write_cr8(gcpu->save_area.gp.reg[CR8_SAVE_AREA]);

	if (IS_MODE_NATIVE(gcpu)) {
		/* apply GDB settings */
		vmdb_settings_apply_to_hw(gcpu);
	}

	if (0 != gcpu->hw_enforcements) {
		gcpu_apply_hw_enforcements(gcpu);
	}

	{
		ia32_vmx_vmcs_vmexit_info_idt_vectoring_t idt_vectoring_info;
		idt_vectoring_info.uint32 =
			(uint32_t)mon_vmcs_read(vmcs,
				VMCS_EXIT_INFO_IDT_VECTORING);

		if (idt_vectoring_info.bits.valid
		    &&
		    ((idt_vectoring_info.bits.interrupt_type ==
		      IDT_VECTORING_INTERRUPT_TYPE_EXTERNAL_INTERRUPT)
		     || (idt_vectoring_info.bits.interrupt_type ==
			 IDT_VECTORING_INTERRUPT_TYPE_NMI))) {
			ia32_vmx_vmcs_vmenter_interrupt_info_t interrupt_info;
			processor_based_vm_execution_controls_t ctrls;

			interrupt_info.uint32 =
				(uint32_t)mon_vmcs_read(vmcs,
					VMCS_ENTER_INTERRUPT_INFO);
			MON_ASSERT(!interrupt_info.bits.valid);

			interrupt_info.uint32 = 0;
			interrupt_info.bits.valid = 1;
			interrupt_info.bits.vector =
				idt_vectoring_info.bits.vector;
			interrupt_info.bits.interrupt_type =
				idt_vectoring_info.bits.interrupt_type;

			mon_vmcs_write(vmcs,
				VMCS_ENTER_INTERRUPT_INFO,
				interrupt_info.uint32);

			if (idt_vectoring_info.bits.interrupt_type ==
			    IDT_VECTORING_INTERRUPT_TYPE_NMI) {
				mon_vmcs_write(vmcs,
					VMCS_GUEST_INTERRUPTIBILITY,
					0);
			} else {
				mon_vmcs_write(vmcs,
					VMCS_GUEST_INTERRUPTIBILITY,
					mon_vmcs_read(vmcs,
						VMCS_GUEST_INTERRUPTIBILITY) &
					~0x3);
			}

			ctrls.uint32 =
				(uint32_t)mon_vmcs_read(vmcs,
					VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);

			if ((ctrls.bits.monitor_trap_flag)
			    && (mon_vmcs_read(vmcs, VMCS_EXIT_INFO_REASON) ==
				IA32_VMX_EXIT_BASIC_REASON_EPT_VIOLATION)) {
				gcpu->trigger_log_event = 1 +
							  interrupt_info.bits.
							  vector;
			}
		}
	}

	/* flash VMCS */
	if (!vmcs_sw_shadow_disable[hw_cpu_id()]) {
		vmcs_flush_to_cpu(vmcs);
	}

	vmcs_sw_shadow_disable[hw_cpu_id()] = FALSE;

	if (!vmcs_launch_required(vmcs)) {
		nmi_window_update_before_vmresume(vmcs);
	}

	/* check for Launch and resume */
	if (vmcs_launch_required(vmcs)) {
		vmcs_set_launched(vmcs);
		/* call assembler launch */
		vmentry_func(TRUE);
		MON_LOG(mask_anonymous, level_trace,
			"VmLaunch failed for GCPU %d GUEST %d in %s mode\n",
			gcpu->vcpu.guest_cpu_id, gcpu->vcpu.guest_id,
			IS_MODE_NATIVE(gcpu) ? "NATIVE" : "EMULATED");
	} else {
		/* call assembler resume */
		vmentry_func(FALSE);
		MON_LOG(mask_anonymous, level_trace,
			"VmResume failed for GCPU %d GUEST %d in %s mode\n",
			gcpu->vcpu.guest_cpu_id, gcpu->vcpu.guest_id,
			IS_MODE_NATIVE(gcpu) ? "NATIVE" : "EMULATED");
	}

	MON_DEADLOOP();
	MON_BREAKPOINT();
}

mon_status_t gcpu_set_hw_enforcement(guest_cpu_handle_t gcpu,
				     vmcs_hw_enforcement_id_t enforcement)
{
	mon_status_t status = MON_OK;

	switch (enforcement) {
	case VMCS_HW_ENFORCE_EMULATOR:
	case VMCS_HW_ENFORCE_FLAT_PT:
	case VMCS_HW_ENFORCE_CACHE_DISABLED:
		gcpu->hw_enforcements |= enforcement;
		break;
	default:
		MON_ASSERT(0);
		status = MON_ERROR;
		break;
	}
	return status;
}

mon_status_t gcpu_remove_hw_enforcement(guest_cpu_handle_t gcpu,
					vmcs_hw_enforcement_id_t enforcement)
{
	mon_status_t status = MON_OK;

	switch (enforcement) {
	case VMCS_HW_ENFORCE_EMULATOR:
		gcpu_enforce_settings_on_hardware(gcpu,
			GCPU_TEMP_EXCEPTIONS_RESTORE_ALL);
		gcpu_enforce_settings_on_hardware(gcpu,
			GCPU_TEMP_CR0_RESTORE_WP);
		break;

	case VMCS_HW_ENFORCE_FLAT_PT:
		gcpu_enforce_settings_on_hardware(gcpu,
			GCPU_TEMP_RESTORE_PF_AND_CR3);
		break;

	case VMCS_HW_ENFORCE_CACHE_DISABLED:
		/* do nothing */
		break;

	default:
		MON_ASSERT(0);
		status = MON_ERROR;
		break;
	}

	gcpu->hw_enforcements &= ~enforcement;
	return status;
}

void gcpu_apply_hw_enforcements(guest_cpu_handle_t gcpu)
{
	MON_ASSERT(!GET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu));

	if (gcpu->hw_enforcements & VMCS_HW_ENFORCE_EMULATOR) {
		gcpu_enforce_settings_on_hardware(gcpu,
			GCPU_TEMP_EXCEPTIONS_EXIT_ON_ALL);
		gcpu_enforce_settings_on_hardware(gcpu,
			GCPU_TEMP_CR0_NO_EXIT_ON_WP);
	} else if (gcpu->hw_enforcements & VMCS_HW_ENFORCE_FLAT_PT) {
		gcpu_enforce_settings_on_hardware(gcpu,
			GCPU_TEMP_EXIT_ON_PF_AND_CR3);
		gcpu_enforce_flat_memory_setup(gcpu);
	}

	if (gcpu->hw_enforcements & VMCS_HW_ENFORCE_CACHE_DISABLED) {
		/* CD = 1 is not allowed. */
		vmcs_update(vmcs_hierarchy_get_vmcs(&gcpu->vmcs_hierarchy,
				VMCS_MERGED),
			VMCS_GUEST_CR0, 0, CR0_CD);

		/* flush HW caches */
		hw_wbinvd();

		/* the solution is not full because of
		 * 1. the OS may assume that some non-write-back memory is uncached
		 * 2. caching influencies in multicore environment
		 * 3. internal CPU behavior like in HSW VMCS caching effects. */
	}

	MON_ASSERT(!GET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu));
}
