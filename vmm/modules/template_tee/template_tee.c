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
#include "modules/ipc.h"

#ifdef MODULE_MSR_ISOLATION
#include "modules/msr_isolation.h"
#endif

#ifdef MODULE_EPT_UPDATE
#include "modules/ept_update.h"
#endif

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
	uint16_t host_cpu = host_cpu_id();
	uint32_t gcpu_status;
	tee_config_ex_t *tee_ex;

	D(VMM_ASSERT(gcpu));

	tee_ex = get_tee_cfg_by_guest(gcpu->guest);
	if (tee_ex == NULL) {
		return;
	}

	if (host_cpu == 0) {
		gcpu_status = tee_ex->tee_config.tee_bsp_status;
	} else {
		gcpu_status = tee_ex->tee_config.tee_ap_status;
	}

	switch (gcpu_status) {
		case MODE_32BIT:
			gcpu_set_32bit_state(gcpu);
			break;
		case MODE_64BIT:
			gcpu_set_64bit_state(gcpu, g_x64_cr3);
			break;
		case WAIT_FOR_SIPI:
			gcpu_set_reset_state(gcpu);
			break;
		case HLT:
			gcpu_set_reset_state(gcpu);
			vmcs_write(gcpu->vmcs, VMCS_GUEST_ACTIVITY_STATE,
			ACTIVITY_STATE_HLT);
			break;
		default:
			VMM_DEADLOOP();
			break;
	}

	return;
}

static void tee_schedule_init_gcpu(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	uint16_t host_cpu = host_cpu_id();
	tee_config_ex_t *tee_ex = g_tee_cfg_ex;
	guest_cpu_handle_t init_gcpu;

	while (tee_ex) {
		/* Find the Tee which needs early boot and meets one of following conditions
		 * 1) on BSP 2) on AP but the Tee support SMP */
		if (tee_ex->tee_config.launch_tee_first &&
				(!tee_ex->tee_config.single_gcpu || (host_cpu == 0))) {
			break;
		}
		tee_ex = tee_ex->next;
	}

	if (tee_ex) {
		init_gcpu = set_initial_guest(tee_ex->tee_guest);
		tee_ex->tee_config.before_launching_tee(init_gcpu);
		print_info("VMM: Launch TEE %s on cpu_id[%u]\n", tee_ex->tee_config.tee_name, host_cpu);
	} else {
		set_initial_guest(guest_handle(GUEST_REE));
		print_info("VMM: Launch Normal World on cpu_id[%u]\n", host_cpu);
	}

	return;
}

static void smc_copy_gp_regs(guest_cpu_handle_t gcpu, guest_cpu_handle_t next_gcpu, tee_config_t *tee_cfg)
{
	uint32_t i;
	uint64_t reg;

	if (gcpu->guest->id == GUEST_REE) {
		/* REE to TEE */
		for (i = 0; i < tee_cfg->smc_param_to_tee_nr; i++) {
			reg = gcpu_get_gp_reg(gcpu, tee_cfg->smc_param_to_tee[i]);
			gcpu_set_gp_reg(next_gcpu, tee_cfg->smc_param_to_tee[i], reg);
		}
	} else {
		/* TEE to REE */
		for (i = 0; i < tee_cfg->smc_param_to_ree_nr; i++) {
			reg = gcpu_get_gp_reg(gcpu, tee_cfg->smc_param_to_ree[i]);
			gcpu_set_gp_reg(next_gcpu, tee_cfg->smc_param_to_ree[i], reg);
		}
	}

	return;
}

static tee_config_ex_t *find_launchable_tee(void)
{
	uint16_t host_cpu = host_cpu_id();
	tee_config_ex_t *tee_ex = g_tee_cfg_ex;

	while (tee_ex) {
		if (tee_ex->tee_config.launch_tee_first && /* The Tee is early launch */
			(!tee_ex->tee_config.single_gcpu || (host_cpu == 0)) && /* On BSP or on AP but Tee supports SMP */
			(tee_ex->tee_status[host_cpu] == TEE_INIT)) { /* The gcpu of this Tee has not been launched yet */
			return tee_ex;
		}
		tee_ex = tee_ex->next;
	}

	return NULL;
}

static void ipc_invalidate_gpm(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	invalidate_gpm_all();
}

/* World Switch */
static void simple_smc_call(guest_cpu_handle_t gcpu, tee_config_ex_t *tee_ex, boolean_t smc_copy)
{
	guest_handle_t guest;
	guest_cpu_handle_t next_gcpu;

	if (tee_ex->tee_config.pre_world_switch)
		tee_ex->tee_config.pre_world_switch(gcpu);

	if (gcpu->guest->id == GUEST_REE) {
		guest = tee_ex->tee_guest;
	} else {
		guest = guest_handle(GUEST_REE);
	}

	next_gcpu = schedule_to_guest(guest);

	if (smc_copy) {
		smc_copy_gp_regs(gcpu, next_gcpu, &tee_ex->tee_config);

		/* Raise event to mitigate L1TF */
		if (GUEST_REE == gcpu->guest->id) {
			event_raise(next_gcpu, EVENT_SWITCH_TO_SECURE, NULL);
		} else {
			event_raise(next_gcpu, EVENT_SWITCH_TO_NONSECURE, NULL);
		}
	}

	if (tee_ex->tee_config.post_world_switch)
		tee_ex->tee_config.post_world_switch(next_gcpu, gcpu);
}

/* Called for first SMC from TEE and currenly TEE is early launch */
static void try_launch_other_tee(guest_cpu_handle_t gcpu, tee_config_ex_t *tee_ex)
{
	uint16_t host_cpu = host_cpu_id();
	tee_config_ex_t *tee_ex_tmp, *next_tee;
	guest_cpu_handle_t next_gcpu;
	guest_cpu_handle_t prev_gcpu;
	guest_handle_t guest;

	if (tee_ex->tee_config.pre_world_switch)
		tee_ex->tee_config.pre_world_switch(gcpu);

	next_tee = find_launchable_tee();
	if (next_tee) {
		guest = next_tee->tee_guest;
		next_gcpu = schedule_to_guest(guest);

		next_tee->tee_config.before_launching_tee(next_gcpu);

		/* post_world_switch is not called here since it is TEE to TEE switch.
		   All post_world_switch will be called later when switching to REE */

		print_info("VMM: Launch TEE %s on cpu_id[%u]\n", next_tee->tee_config.tee_name, host_cpu);
	} else {
		guest = guest_handle(GUEST_REE);
		next_gcpu = schedule_to_guest(guest);

		/* Call post_world_switch for all early launch Tees */
		tee_ex_tmp = g_tee_cfg_ex;
		while (tee_ex_tmp) {
			if (tee_ex_tmp->tee_config.launch_tee_first &&
					(tee_ex_tmp->tee_status[host_cpu] == TEE_LAUNCHED) &&
					tee_ex_tmp->tee_config.post_world_switch) {
				prev_gcpu = get_gcpu_from_guest(tee_ex_tmp->tee_guest, host_cpu);
				VMM_ASSERT_EX(prev_gcpu, "%s(): Failed to get gcpu on cpu_id[%u]\n", __func__, host_cpu);
				tee_ex_tmp->tee_config.post_world_switch(next_gcpu, prev_gcpu);
			}
			tee_ex_tmp = tee_ex_tmp->next;
		}

		print_info("VMM: Launch Normal World on cpu_id[%u]\n", host_cpu);
	}
}

static void smc_handler(guest_cpu_handle_t gcpu)
{
	uint32_t vmcall_id;
	uint16_t host_cpu = host_cpu_id();
	uint8_t tee_status;
	tee_config_ex_t *tee_ex;

	if (!gcpu_in_ring0(gcpu)) {
		gcpu_inject_ud(gcpu);
		return;
	}

	vmcall_id = (uint32_t)get_vmcall_id(gcpu);

	tee_ex = get_tee_cfg_by_vmcall_id(vmcall_id);
	D(VMM_ASSERT_EX(tee_ex, "%s(): Get tee ex failed\n", __func__));

	if (tee_ex->tee_config.single_gcpu && (host_cpu != 0)) {
		gcpu_inject_ud(gcpu);
		return;
	}

	vmcs_read(gcpu->vmcs, VMCS_GUEST_RIP); // update cache
	vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_LEN); // update cache

	tee_status = tee_ex->tee_status[host_cpu];

	/*
	 * Dispatch SPM calls to SPM SMC handler.
	 * If SMC is handle by SPM SMC handler, world switch will not be triggered.
	 */
	if (tee_ex->tee_config.spm_srv_call) {
		if (tee_ex->tee_config.spm_srv_call(gcpu)) {
		    return;
		}
	}

	if (tee_status == TEE_INIT) {
		if (gcpu->guest->id == GUEST_REE) {
			/* First SMC call from Ree */
			tee_ex->tee_config.first_smc_to_tee(gcpu);
		} else {
			/* First SMC call from Tee */

			/* update tee_status here so that the find_launchable_tee() will skip this tee */
			tee_ex->tee_status[host_cpu] = TEE_LAUNCHED;

			if (tee_ex->tee_config.launch_tee_first) {
				/* Tee early launch */
				try_launch_other_tee(gcpu, tee_ex);
			} else {
				/* Tee later launch */
				simple_smc_call(gcpu, tee_ex, FALSE);
			}
		}
	} else {
		/* tee_status is TEE_LAUNCHED */
		simple_smc_call(gcpu, tee_ex, TRUE);
	}
}

/* remove other tees range in this tee */
static void remove_other_tee_range(tee_config_ex_t *this_tee)
{
	tee_config_ex_t *tee_ex = g_tee_cfg_ex;

	while (tee_ex) {
		if ((tee_ex != this_tee) &&
				tee_ex->tee_config.tee_runtime_addr &&
				tee_ex->tee_config.tee_runtime_size) {
			gpm_remove_mapping(this_tee->tee_guest,
					tee_ex->tee_config.tee_runtime_addr,
					tee_ex->tee_config.tee_runtime_size);
		}
		tee_ex = tee_ex->next;
	}
}

/* remove this tee range in other guests */
static void remove_this_tee_range(tee_config_ex_t *this_tee)
{
	guest_handle_t guest = get_guests_list();

	while (guest) {
		if (guest != this_tee->tee_guest) {
			gpm_remove_mapping(guest,
					this_tee->tee_config.tee_runtime_addr,
					this_tee->tee_config.tee_runtime_size);
		}
		guest = guest->next_guest;
	}
}

guest_handle_t create_tee(tee_config_t *cfg)
{
	uint32_t i, cpu_num;
	guest_handle_t guest;
	tee_config_ex_t *new_tee_ex;

	VMM_ASSERT_EX(cfg, "%s(): No configure for tee\n", __func__);
	VMM_ASSERT_EX(cfg->tee_name, "%s(): No tee name\n", __func__);
	VMM_ASSERT_EX(cfg->tee_bsp_status <= MODE_64BIT, "%s(): Tee bsp status is incorrect\n", __func__);
	VMM_ASSERT_EX(cfg->tee_ap_status <= HLT, "%s(): Tee ap status is incorrect\n", __func__);
	VMM_ASSERT_EX((cfg->launch_tee_first || cfg->first_smc_to_tee),
			"%s(): first_smc_to_tee should not be NULL if launch ree first\n", __func__);
	VMM_ASSERT_EX(!cfg->launch_tee_first || cfg->before_launching_tee,
			"%s(): before_launching_tee should not be NULL if launch tee first\n", __func__);
	VMM_ASSERT_EX(!cfg->launch_tee_first || (cfg->tee_runtime_addr && cfg->tee_runtime_size),
			"%s(): tee runtime addr/size should not be NULL if launch tee first\n", __func__);

	VMM_ASSERT_EX((cfg->smc_param_to_tee_nr <= 8), "%s(): smc num to Tee exceed max\n", __func__);
	for (i=0; i<cfg->smc_param_to_tee_nr; i++)
		VMM_ASSERT_EX(cfg->smc_param_to_tee[i] < REG_GP_COUNT, "%s(): smc param to tee exceeds reg max index\n", __func__);

	VMM_ASSERT_EX((cfg->smc_param_to_ree_nr <= 8), "%s(): smc num to Ree exceed max\n", __func__);
	for (i=0; i<cfg->smc_param_to_ree_nr; i++)
		VMM_ASSERT_EX(cfg->smc_param_to_ree[i] < REG_GP_COUNT, "%s(): smc param to ree exceeds reg max index\n", __func__);

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

	new_tee_ex = (tee_config_ex_t *)mem_alloc(sizeof(tee_config_ex_t));
	memcpy(&new_tee_ex->tee_config, cfg, sizeof(tee_config_t));
	new_tee_ex->tee_guest = guest;
	memset(new_tee_ex->tee_status, TEE_INIT, host_cpu_num);
	new_tee_ex->next = g_tee_cfg_ex;
	/* Always add new tee to list head */
	g_tee_cfg_ex = new_tee_ex;

	/* For this tee guest, set rwx(0x7) permission to its own range
	 * For all other guests (note: not all tee, but all guests),
	 * remove this tees' range in oher guest */
	if (cfg->tee_runtime_addr && cfg->tee_runtime_size) {
		gpm_set_mapping(guest,
				cfg->tee_runtime_addr,
				cfg->tee_runtime_addr,
				cfg->tee_runtime_size,
				0x7);

		/* remove this tees range in other guests */
		remove_this_tee_range(new_tee_ex);
	}

	/* remove other tees range in this tee */
	remove_other_tee_range(new_tee_ex);

	return guest;
}

guest_cpu_handle_t launch_tee(guest_handle_t guest, uint64_t tee_rt_addr, uint64_t tee_rt_size)
{
	uint16_t host_cpu = host_cpu_id();
	tee_config_ex_t *tee_ex;
	guest_cpu_handle_t next_gcpu;
	boolean_t updated = FALSE;

	D(VMM_ASSERT_EX(guest, "%s(): guest is NULL\n", __func__));

	tee_ex = get_tee_cfg_by_guest(guest);
	VMM_ASSERT_EX(tee_ex, "%s(): faied to get tee cfg from guest\n", __func__);

	VMM_ASSERT_EX(!tee_ex->tee_config.single_gcpu || (host_cpu == 0),
		"%s(): tee is single gcpu but launched on AP!\n", __func__);

	VMM_ASSERT_EX(tee_ex->tee_status[host_cpu] == TEE_INIT,
		"%s(): Tee[%s] has already been launched on cpu[%u]\n", __func__, tee_ex->tee_config.tee_name, host_cpu);

	if (tee_rt_addr != 0) {
		if (tee_ex->tee_config.tee_runtime_addr == 0) {
			tee_ex->tee_config.tee_runtime_addr = tee_rt_addr;
			updated = TRUE;
		} else {
			VMM_ASSERT_EX(tee_ex->tee_config.tee_runtime_addr == tee_rt_addr,
				"%s(): mismatch runtime addr on cpu_id[%u]!\n", __func__, host_cpu);
		}
	}

	if (tee_rt_size != 0) {
		if (tee_ex->tee_config.tee_runtime_size == 0) {
			tee_ex->tee_config.tee_runtime_size = tee_rt_size;
			updated = TRUE;
		} else {
			VMM_ASSERT_EX(tee_ex->tee_config.tee_runtime_size == tee_rt_size,
				"%s(): mismatch runtime size on cpu_id[%u]!\n", __func__, host_cpu);
		}
	}

	VMM_ASSERT_EX(tee_ex->tee_config.tee_runtime_addr && tee_ex->tee_config.tee_runtime_size,
		"%s(): No avail runtime addr or size\n", __func__);

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

		/* remove this tees range in other guests */
		remove_this_tee_range(tee_ex);

		/* Invalidate all EPT cache on all physical CPUs */
		invalidate_gpm_all();
		ipc_exec_on_all_other_cpus(ipc_invalidate_gpm, NULL);
	}

	/* World switch */
	next_gcpu = schedule_to_guest(guest);

	if (tee_ex->tee_config.before_launching_tee)
		tee_ex->tee_config.before_launching_tee(next_gcpu);

	print_info("VMM: Launch TEE %s on cpu_id[%u]\n", tee_ex->tee_config.tee_name, host_cpu);

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
