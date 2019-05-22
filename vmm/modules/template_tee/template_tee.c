/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "lib/util.h"
#include "heap.h"
#include "gcpu.h"
#include "guest.h"
#include "gcpu_state.h"
#include "gpm.h"
#include "scheduler.h"
#include "event.h"
#include "ept.h"
#include "hmm.h"
#include "gcpu_inject_event.h"
#include "modules/vmcall.h"
#include "modules/template_tee.h"

#ifdef MODULE_MSR_ISOLATION
#include "modules/msr_isolation.h"
#endif

#ifdef MODULE_EPT_UPDATE
#include "modules/ept_update.h"
#endif

#define GUEST_REE 0 /* OSloader or Android/Linux/Fuchsia */

enum {
	TEE_INIT = 0,
	TEE_LAUNCHED
};

typedef struct tee_config_ex {
	tee_config_t tee_config;
	guest_handle_t tee_guest;
	/* only 2 status: before tee boot and after tee boot; the reasons to use uint8_t
	   instead of bitmap are: 1. Easy to operate; 2. May extend to more status in future */
	uint8_t tee_status[ALIGN_F(MAX_CPU_NUM, 8)];
	struct tee_config_ex *next;
} tee_config_ex_t;

static tee_config_ex_t *g_tee_cfg_ex;
static uint64_t g_x64_cr3;

static tee_config_ex_t *get_tee_cfg_by_guest(guest_handle_t guest)
{
	tee_config_ex_t *tee_ex = g_tee_cfg_ex;

	while (tee_ex) {
		if (tee_ex->tee_guest == guest)
			return tee_ex;
		tee_ex = tee_ex->next;
	}

	return NULL;
}

static tee_config_ex_t *get_tee_cfg_by_vmcall_id(uint32_t vmcall_id)
{
	tee_config_ex_t *tee_ex = g_tee_cfg_ex;

	while (tee_ex) {
		if (tee_ex->tee_config.smc_vmcall_id == vmcall_id)
			return tee_ex;
		tee_ex = tee_ex->next;
	}

	return NULL;
}

static void tee_set_gcpu_state(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	tee_config_ex_t *tee_ex;

	D(VMM_ASSERT(gcpu));

	tee_ex = get_tee_cfg_by_guest(gcpu->guest);
	if (tee_ex == NULL) {
		return;
	}

	switch (tee_ex->tee_config.tee_bsp_status) {
		case MODE_32BIT:
			gcpu_set_32bit_state(gcpu);
			break;
		case MODE_64BIT:
			gcpu_set_64bit_state(gcpu, g_x64_cr3);
			break;
		default:
			VMM_DEADLOOP();
			break;
	}

	/* SMP will support later */
	//if (gcpu->id != 0) {
	//	gcpu_set_reset_state(gcpu);
	//}

	return;
}

static void tee_schedule_init_gcpu(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	tee_config_ex_t *tee_ex;

	set_initial_guest(guest_handle(GUEST_REE));

	tee_ex = g_tee_cfg_ex;
	while (tee_ex) {
		if (tee_ex->tee_config.launch_tee_first) {
			set_initial_guest(tee_ex->tee_guest);
			break;
		}
		tee_ex = tee_ex->next;
	}

	if (tee_ex) {
		print_info("VMM: Launch TEE %s\n", tee_ex->tee_config.tee_name);
	} else {
		print_info("VMM: Launch Normal World\n");
	}

	return;
}

/* todo: remove
static void before_launching_tee(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	tee_config_ex_t *tee_ex;

	D(VMM_ASSERT(gcpu));

	tee_ex = get_tee_cfg_by_guest(gcpu->guest);
	if (tee_ex == NULL) {
		return;
	}

	if (tee_ex->tee_config.launch_tee_first) {
		tee_ex->tee_config.before_launching_tee(gcpu);
	}

	return;
}
*/

static void smc_copy_gp_regs(guest_cpu_handle_t gcpu, guest_cpu_handle_t next_gcpu, tee_config_t *tee_cfg)
{
	uint32_t i;
	uint64_t reg;

	if (gcpu->guest->id == GUEST_REE) { // REE to TEE
		for (i = 0; i < tee_cfg->smc_param_to_tee_nr; i++) {
			reg = gcpu_get_gp_reg(gcpu, tee_cfg->smc_param_to_tee[i]);
			gcpu_set_gp_reg(next_gcpu, tee_cfg->smc_param_to_tee[i], reg);
		}
	} else { // TEE to REE
		for (i = 0; i < tee_cfg->smc_param_to_ree_nr; i++) {
			reg = gcpu_get_gp_reg(gcpu, tee_cfg->smc_param_to_ree[i]);
			gcpu_set_gp_reg(next_gcpu, tee_cfg->smc_param_to_ree[i], reg);
		}
	}

	return;
}

static void smc_handler(guest_cpu_handle_t gcpu)
{
	uint32_t vmcall_id;
	uint16_t host_cpu = host_cpu_id();
	uint8_t tee_status;
	tee_config_ex_t *tee_ex;
	guest_cpu_handle_t next_gcpu;
	guest_handle_t guest;

	if(!gcpu_in_ring0(gcpu)) {
		gcpu_inject_ud(gcpu);
		return;
	}

	vmcall_id = (uint32_t)get_vmcall_id(gcpu);

	tee_ex = get_tee_cfg_by_vmcall_id(vmcall_id);
	D(VMM_ASSERT_EX(tee_ex, "Get tee ex failed\n"));

	tee_status = tee_ex->tee_status[host_cpu];

	/* callback of post_world_switch */
	if (tee_status == TEE_LAUNCHED || gcpu->guest->id != GUEST_REE) {
		if (tee_ex->tee_config.pre_world_switch)
			tee_ex->tee_config.pre_world_switch(gcpu);
	} else {
		D(VMM_ASSERT_EX(tee_ex->tee_config.first_smc_to_tee, "first smc to tee callback is NULL\n"));
		tee_ex->tee_config.first_smc_to_tee(gcpu);
	}

	/* Raise event to mitigate L1TF */
	if (GUEST_REE == gcpu->guest->id) {
		event_raise(NULL, EVENT_SWITCH_TO_SECURE, NULL);
	} else {
		event_raise(NULL, EVENT_SWITCH_TO_NONSECURE, NULL);
	}

	vmcs_read(gcpu->vmcs, VMCS_GUEST_RIP); // update cache
	vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_LEN); // update cache

	/* World switch */
	//todo: print log to show TEE/REE launch
	if (gcpu->guest->id == GUEST_REE) {
		guest = tee_ex->tee_guest;
	} else {
		guest = guest_handle(GUEST_REE);
	}

	next_gcpu = schedule_to_guest(guest);
	VMM_ASSERT_EX(next_gcpu, "Failed to schedule guest\n");

	/* SMC param copy */
	if (tee_status == TEE_LAUNCHED) {
		smc_copy_gp_regs(gcpu, next_gcpu, &tee_ex->tee_config);
	}

	/* callback of post_world_switch */
	if ((tee_status == TEE_LAUNCHED) || (gcpu->guest->id != GUEST_REE)) {
		if(tee_ex->tee_config.post_world_switch)
			tee_ex->tee_config.post_world_switch(gcpu, next_gcpu);
	}

	/* Change TEE status from INIT to LAUNCHED */
	if ((tee_status == TEE_INIT) && (gcpu->guest->id != GUEST_REE)) {
		tee_ex->tee_status[host_cpu] = TEE_LAUNCHED;
	}

	return;
}

guest_handle_t create_tee(tee_config_t *cfg)
{
	uint32_t i, cpu_num;
	guest_handle_t guest;
	tee_config_ex_t *tee_ex, *new_tee_ex;

	if (cfg == NULL) {
		print_warn("No configure for tee\n");
		return NULL;
	}

	if (cfg->tee_name == NULL) {
		print_warn("No Tee name\n");
		return NULL;
	}

	if (cfg->smc_param_to_tee_nr >= 8 || cfg->smc_param_to_ree_nr >= 8) {
		print_warn("smc num from/to tee exceed max\n");
		return NULL;
	}

	if (cfg->tee_bsp_status > MODE_64BIT || cfg->tee_ap_status > HLT) {
		print_warn("Tee bsp or ap status is incorrect\n");
		return NULL;
	}

	if (!cfg->launch_tee_first && !cfg->first_smc_to_tee) {
		print_warn("first_smc_to_tee should not be NULL if launch ree first.\n");
		return NULL;
	}

	if (cfg->launch_tee_first) {
		if (cfg->before_launching_tee == NULL) {
			print_warn("before_launching_tee should not be NULL if launch tee first.\n");
			return NULL;
		}

		if (cfg->tee_runtime_addr == 0 || cfg->tee_runtime_size) {
			print_warn("tee runtime addr/size should not be NULL if launch tee first.\n");
			return NULL;
		}
	}

	if (cfg->single_gcpu)
		cpu_num = 1;
	else
		cpu_num = host_cpu_num;

#ifdef MODULE_EPT_UPDATE
	guest = create_guest(cpu_num, 0x0);
	ept_update_install(guest->id);
#else
	/* Tee should not have X permission in REE memory. Set it to RW(0x3) */
	guest = create_guest(cpu_num, 0x3);
#endif
	vmcall_register(GUEST_REE, cfg->smc_vmcall_id, smc_handler);
	vmcall_register(guest->id, cfg->smc_vmcall_id, smc_handler);

	/* For this tee guest, set rwx(0x7) permission to its own range
	 * For all other guests (note: not all tee, but all guests),
	 * remove this tees' range in oher guest */
	if (cfg->tee_runtime_addr && cfg->tee_runtime_size) {
		gpm_set_mapping(guest,
				cfg->tee_runtime_addr,
				cfg->tee_runtime_addr,
				cfg->tee_runtime_size,
				0x7);

		for (i=0; i<guest->id; i++) {
			gpm_remove_mapping(guest_handle(i),
					cfg->tee_runtime_addr,
					cfg->tee_runtime_size);
		}
	}

	/* remove other tees' range in this tee */
	tee_ex = g_tee_cfg_ex;
	while (tee_ex) {
		if (tee_ex->tee_config.tee_runtime_addr &&
			tee_ex->tee_config.tee_runtime_size) {
			gpm_remove_mapping(guest,
					tee_ex->tee_config.tee_runtime_addr,
					tee_ex->tee_config.tee_runtime_size);
		}
		tee_ex = tee_ex->next;
	}

	new_tee_ex = (tee_config_ex_t *)mem_alloc(sizeof(tee_config_ex_t));
	memcpy(&new_tee_ex->tee_config, cfg, sizeof(tee_config_t));
	new_tee_ex->tee_guest = guest;
	memset(new_tee_ex->tee_status, TEE_INIT, host_cpu_num);
	new_tee_ex->next = g_tee_cfg_ex;
	g_tee_cfg_ex = new_tee_ex;

	return guest;
}

guest_cpu_handle_t launch_tee(guest_handle_t guest, uint64_t tee_rt_addr, uint64_t tee_rt_size)
{
	tee_config_ex_t *tee_ex, *tee_ex_tmp;
	guest_cpu_handle_t next_gcpu;
	boolean_t updated = FALSE;

	D(VMM_ASSERT_EX(guest, "guest is NULL\n"));

	tee_ex = get_tee_cfg_by_guest(guest);
	if (tee_ex == NULL) {
		print_warn("%s: failed to get tee_ex!\n", __func__);
		return NULL;
	}

	if (tee_rt_addr != 0) {
		if (tee_ex->tee_config.tee_runtime_addr == 0) {
			tee_ex->tee_config.tee_runtime_addr = tee_rt_addr;
			updated = TRUE;
		} else if (tee_ex->tee_config.tee_runtime_addr != tee_rt_addr) {
			print_warn("%s: mismatch runtime addr!\n", __func__);
			return NULL;
		}
	}

	if (tee_rt_size != 0) {
		if (tee_ex->tee_config.tee_runtime_size == 0) {
			tee_ex->tee_config.tee_runtime_size = tee_rt_size;
			updated = TRUE;
		} else if (tee_ex->tee_config.tee_runtime_size != tee_rt_size) {
			print_warn("%s: mismatch runtime size!\n", __func__);
			return NULL;
		}
	}

	if (tee_ex->tee_config.tee_runtime_addr == 0 || tee_ex->tee_config.tee_runtime_size == 0) {
		print_warn("%s: No avail runtime addr or size!\n", __func__);
		return NULL;
	}

	/* Tee runtime memory or size has been updated */
	if (updated) {
		/* For this tee guest, set rwx(0x7) permission to its own range
		 * For all other guests (note: not all tee, but all guests),
		 * remove this tees' range in other guest */
		gpm_set_mapping(guest,
				tee_ex->tee_config.tee_runtime_addr,
				tee_ex->tee_config.tee_runtime_addr,
				tee_ex->tee_config.tee_runtime_size,
				0x7);

		tee_ex_tmp = g_tee_cfg_ex;
		while (tee_ex_tmp) {
			if (tee_ex_tmp->tee_guest != guest) {
				gpm_remove_mapping(tee_ex_tmp->tee_guest,
						tee_ex->tee_config.tee_runtime_addr,
						tee_ex->tee_config.tee_runtime_size);
			}
			tee_ex_tmp = tee_ex_tmp->next;
		}

		/* Invalidate all EPT cache on all physical CPUs */
		invalidate_gpm_all();
	}

	/* World switch */
	next_gcpu = schedule_to_guest(guest);
	VMM_ASSERT_EX(next_gcpu, "Failed to schedule guest\n");

	return next_gcpu;
}

void template_tee_init(uint64_t x64_cr3)
{
	g_x64_cr3 = x64_cr3;
	event_register(EVENT_GCPU_INIT, tee_set_gcpu_state);
	event_register(EVENT_SCHEDULE_INITIAL_GCPU, tee_schedule_init_gcpu);

#ifdef MODULE_MSR_ISOLATION
	/* Isolate below MSRs between guests and set initial value to 0 */
	add_to_msr_isolation_list(MSR_STAR, 0, GUESTS_ISOLATION);
	add_to_msr_isolation_list(MSR_LSTAR, 0, GUESTS_ISOLATION);
	add_to_msr_isolation_list(MSR_FMASK, 0, GUESTS_ISOLATION);
	add_to_msr_isolation_list(MSR_KERNEL_GS_BASE, 0, GUESTS_ISOLATION);
#endif
}
