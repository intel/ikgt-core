/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "vmm_arch.h"
#include "vmm_objects.h"
#include "dbg.h"
#include "lib/util.h"
#include "heap.h"
#include "gcpu.h"
#include "guest.h"
#include "trusty_info.h"
#include "gcpu_inject_event.h"

#include "lib/util.h"
#include "lib/image_loader.h"
#include "modules/vmcall.h"
#include "modules/template_tee.h"

#ifndef QEMU_LK
#ifndef PACK_LK
#error "PACK_LK is not defined"
#endif
#endif

#ifdef MODULE_VTD
#include "modules/vtd.h"
#endif

#ifdef MODULE_DEADLOOP
#include "modules/deadloop.h"
#endif

#ifdef MODULE_VMX_TIMER
#include "event.h"
#include "modules/vmx_timer.h"
#include "scheduler.h"

#define TRUSTY_TIMER_INTR 0x21
#endif

#ifdef MODULE_SPM_SRV
#include "modules/spm_srv.h"
#endif

enum {
	TRUSTY_VMCALL_SMC       = 0x74727500,
	TRUSTY_VMCALL_DUMP_INIT = 0x74727507,
#ifdef MODULE_VMX_TIMER
	TRUSTY_VMCALL_VMX_TIMER = 0x74727508,
#endif
	TRUSTY_VMCALL_SECINFO   = 0x74727509,
};

static uint64_t g_init_rdi, g_init_rsp, g_init_rip;
static uint8_t seed[BUP_MKHI_BOOTLOADER_SEED_LEN];

static uint64_t relocate_trusty_image(module_file_info_t *tee_file)
{
	memcpy((void *)tee_file->runtime_addr,
		(void *)tee_file->loadtime_addr,
		tee_file->loadtime_size);

	return tee_file->runtime_addr;
}

static void before_launching_tee(guest_cpu_handle_t gcpu_trusty)
{
	gcpu_set_gp_reg(gcpu_trusty, REG_RDI, g_init_rdi);
	gcpu_set_gp_reg(gcpu_trusty, REG_RSP, g_init_rsp);
	vmcs_write(gcpu_trusty->vmcs, VMCS_GUEST_RIP, g_init_rip);

#ifdef QEMU_LK
	/*
	 * Qemu lk project is used to validate Trusty + iKGT + Android
	 * for patches which have been uploaded to Google Gerrit.
	 *
	 * Set RAX as MULTIBOOT MAGIC WORD, RBX as the offset from MEMBASE,
	 */
	gcpu_set_gp_reg(gcpu_trusty, REG_RAX, 0x2BADB002);
	gcpu_set_gp_reg(gcpu_trusty, REG_RBX, 0x4000);
#endif
}

static void trusty_vmcall_dump_init(guest_cpu_handle_t gcpu)
{
#ifdef MODULE_DEADLOOP
	uint64_t dump_gva;

	D(VMM_ASSERT(gcpu->guest->id == GUEST_REE));

	if (!gcpu_in_ring0(gcpu)) {
		gcpu_inject_ud(gcpu);
		return;
	}

	/* RDI stored the pointer of deadloop_dump_t which allocated by the guest */
	dump_gva = gcpu_get_gp_reg(gcpu, REG_RDI);

	/* register event deadloop */
	if (!deadloop_setup(gcpu, dump_gva))
		return;
#endif
	/* set the return value */
	gcpu_set_gp_reg(gcpu, REG_RAX, 0);
}

static void trusty_vmcall_get_seed(guest_cpu_handle_t gcpu)
{
	static boolean_t fuse = FALSE;
	boolean_t ret;
	uint64_t gva;
	pf_info_t pfinfo;

	D(VMM_ASSERT(gcpu));
	D(VMM_ASSERT(GUEST_REE != gcpu->guest->id));

	if (!fuse && (GUEST_REE != gcpu->guest->id)) {
		gva = gcpu_get_gp_reg(gcpu, REG_RDI);
		D(VMM_ASSERT(gva));

		ret = gcpu_copy_to_gva(gcpu, gva, (uint64_t)seed, BUP_MKHI_BOOTLOADER_SEED_LEN, &pfinfo);
		if (!ret) {
			print_panic("Failed to copy security info\n");
		}

		memset((void *)seed, 0, BUP_MKHI_BOOTLOADER_SEED_LEN);
		barrier();

		fuse = TRUE;
	}
}

/*
 * Pay attention to post_world_switch, since before invokes post_world_switch,
 * SMC hanlder has already scheduled from gcpu to next gcpu on same host cpu.
 * First paramter is destination gcpu of SMC, and second parameter is source gcpu
 * of SMC.
 */
static void post_world_switch(guest_cpu_handle_t gcpu,
		guest_cpu_handle_t gcpu_prev)
{
	D(VMM_ASSERT(gcpu));
	D(VMM_ASSERT(gcpu_prev));

#ifdef MODULE_VMX_TIMER
	vmx_timer_copy(gcpu_prev, gcpu);
#endif
}

#ifdef MODULE_VMX_TIMER
static void trusty_vmcall_vmx_timer(guest_cpu_handle_t gcpu)
{
	uint64_t timer_interval;
	uint64_t tick;

	D(VMM_ASSERT(gcpu));

	/* RDI stores the timer interval in millisecond to be set */
	timer_interval = gcpu_get_gp_reg(gcpu, REG_RDI);

	if (0 == timer_interval) {
		vmx_timer_set_mode(gcpu, TIMER_MODE_STOPPED, 0);
	} else {
		tick = vmx_timer_ms_to_tick(timer_interval);
#ifdef QEMU_LK
		/*
		 * VMX preemption timer is used on QEMU platform only for current stage.
		 *
		 * When Trusty runs on top of QEMU with KVM nested-VT feature enabled,
		 * QEMU would be always scheduled out by host Linux kernel when QEMU guest
		 * runs into sleep or set timer. In this situation, TSC in QEMU guest
		 * increase quite slow, it makes VMX preepmtion timer quite slow.
		 *
		 * In order to schedule in QEMU guest more frequently, add TSC acceleration
		 * to reduce TSC elapse when guest sets VMX preepmtion timer.
		 */
		tick /= 30;
#endif
		vmx_timer_set_mode(gcpu, TIMER_MODE_ONESHOT, tick);
	}
}

static void vmx_timer_event_handler(guest_cpu_handle_t gcpu, UNUSED void* pv)
{
	D(VMM_ASSERT(gcpu));

	gcpu_set_pending_intr(gcpu, TRUSTY_TIMER_INTR);
}
#endif

#ifdef MODULE_SPM_SRV
static boolean_t spm_srv_call(guest_cpu_handle_t gcpu)
{
	D(VMM_ASSERT(gcpu));

	if (is_spm_srv_call(gcpu)) {
		spm_mm_smc_handler(gcpu);
		return TRUE;
	}

	return FALSE;
}
#endif

void init_trusty_tee(evmm_desc_t *evmm_desc)
{
	tee_config_t trusty_cfg;
	tee_desc_t *trusty_desc;
	guest_handle_t trusty_guest;
	uint8_t smc_param_to_tee[] = {REG_RDI, REG_RSI, REG_RDX, REG_RCX};
	uint8_t smc_param_to_ree[] = {REG_RDI, REG_RSI, REG_RDX, REG_RCX};
	uint64_t runtime_addr;
	uint64_t runtime_size;
	uint64_t barrier_size;

	D(VMM_ASSERT_EX(evmm_desc, "evmm_desc is NULL\n"));

	trusty_desc = (tee_desc_t *)&evmm_desc->trusty_tee_desc;
	D(VMM_ASSERT(trusty_desc));

	print_trace("Init trusty guest\n");

	memset(&trusty_cfg, 0, sizeof(tee_config_t));

	trusty_cfg.tee_name = "trusty_tee";
	trusty_cfg.single_gcpu = TRUE;
	trusty_cfg.smc_vmcall_id = TRUSTY_VMCALL_SMC;

	fill_smc_param_to_tee(trusty_cfg, smc_param_to_tee);
	fill_smc_param_from_tee(trusty_cfg, smc_param_to_ree);

	trusty_cfg.launch_tee_first = TRUE;
	trusty_cfg.before_launching_tee = before_launching_tee;
	trusty_cfg.tee_bsp_status = MODE_64BIT;
	trusty_cfg.tee_ap_status = HLT;
	trusty_cfg.post_world_switch = post_world_switch;

#ifdef MODULE_SPM_SRV
	trusty_cfg.spm_srv_call = spm_srv_call;
#endif

	/* Check rowhammer mitigation for TEE */
	if (trusty_desc->tee_file.barrier_size == 0)
		print_warn("No rawhammer mitigation for TEE\n");

	runtime_addr = trusty_desc->tee_file.runtime_addr;
	runtime_size = trusty_desc->tee_file.runtime_total_size;
	barrier_size = trusty_desc->tee_file.barrier_size;

	/*
	 * When we create EPT for Trusty guest, memory region for Trusty guest
	 * should contains Trusty memory and 2 barriers, since memory resides
	 * in barrier region is used for Trusty guest row hammer mitigation,
	 * normal world should never touch barriers.
	 */
	trusty_cfg.tee_runtime_addr = runtime_addr - barrier_size;
	trusty_cfg.tee_runtime_size = runtime_size + 2 * barrier_size;

	memcpy(seed, trusty_desc->seed, BUP_MKHI_BOOTLOADER_SEED_LEN);
	memset((void *)trusty_desc->seed, 0, BUP_MKHI_BOOTLOADER_SEED_LEN);
	barrier();

	/* Set RIP RDI and RSP */
	g_init_rip = relocate_trusty_image(&trusty_desc->tee_file);
	g_init_rdi = runtime_size;
	g_init_rsp = runtime_addr + runtime_size;

	trusty_guest = create_tee(&trusty_cfg);
	VMM_ASSERT_EX(trusty_guest, "Failed to create trusty guest!\n");

#ifdef MODULE_VTD
#ifdef DMA_FROM_CSE
	vtd_assign_dev(gid2did(trusty_guest->id), DMA_FROM_CSE);
#endif
#endif

	vmcall_register(GUEST_REE, TRUSTY_VMCALL_DUMP_INIT, trusty_vmcall_dump_init);

	vmcall_register(trusty_guest->id,
            TRUSTY_VMCALL_SECINFO,
            trusty_vmcall_get_seed);

#ifdef MODULE_VMX_TIMER
	event_register(EVENT_VMX_TIMER, vmx_timer_event_handler);

	vmcall_register(trusty_guest->id,
            TRUSTY_VMCALL_VMX_TIMER,
            trusty_vmcall_vmx_timer);
#endif

	return;
}
