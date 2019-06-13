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
#include "device_sec_info.h"

#include "lib/util.h"
#include "lib/image_loader.h"
#include "modules/vmcall.h"
#include "modules/template_tee.h"

#ifdef MODULE_VTD
#include "modules/vtd.h"
#endif

#ifdef DERIVE_KEY
#include "modules/crypto.h"
#endif

#ifdef MODULE_DEADLOOP
#include "modules/deadloop.h"
#endif

enum {
	TRUSTY_VMCALL_SMC       = 0x74727500,
	TRUSTY_VMCALL_DUMP_INIT = 0x74727507,
};

static uint64_t g_init_rdi, g_init_rsp, g_init_rip;
static guest_handle_t trusty_guest;
/* For Tee early launch, dev_sec_info gets from evmm->trusty_desc which
 * belongs to evmm stack, so no need to free it.
 * For later launch, dev_sec_info is dynamic alloc during trusty_tee_init,
 * then copy secure info to it. The memory should be free in first_smc_to_tee.
*/
static void *dev_sec_info;

/* Reserved size for dev_sec_info/trusty_startup and Stack(1 page) */
/*
 * Trusty load size formula:
 * MEM assigned to Trusty is 16MB size in total
 * Stack size must be reserved
 * Trusty Tee runtime memory will be calculated by Trusty itself
 * The rest region can be use to load Trusty image
 * ------------------------
 *     ^     |          |
 *   Stack   |          |
 * ----------|          |
 *     ^     |          |
 *     |     |          |
 *   HEAP    |          |
 *     |     |    M     |
 *  ---------|          |
 *     ^     |    E     |
 *     |     |          |
 *   Trusty  |    M     |
 *   load    |          |
 *   size    |          |
 *     |     |          |
 *  ---------|          |
 *     ^     |          |
 *  Boot info|          |
 * ------------------------
 */

static uint64_t relocate_trusty_image(module_file_info_t *tee_file)
{
	boolean_t ret = FALSE;
	uint64_t entry_point;

	/* lk region: first page is trusty info, last page is stack. */
	tee_file->runtime_addr += PAGE_4K_SIZE;
	tee_file->runtime_total_size -= PAGE_4K_SIZE;

	ret = relocate_elf_image(tee_file, &entry_point);
	if (!ret) {
		print_trace("Failed to load ELF file. Try multiboot now!\n");
		ret = relocate_multiboot_image((uint64_t *)tee_file->loadtime_addr,
				tee_file->loadtime_size, &entry_point);
	}
	VMM_ASSERT_EX(ret, "Failed to relocate Trusty image!\n");

	/* restore lk runtime address and total size */
	tee_file->runtime_addr -= PAGE_4K_SIZE;
	tee_file->runtime_total_size += PAGE_4K_SIZE;

	return entry_point;
}

#ifdef DERIVE_KEY
static int get_max_svn_index(device_sec_info_v0_t *sec_info)
{
	uint32_t i, max_svn_idx = 0;

	if ((sec_info->num_seeds == 0) || (sec_info->num_seeds > BOOTLOADER_SEED_MAX_ENTRIES))
		return -1;

	for (i = 1; i < sec_info->num_seeds; i++) {
		if (sec_info->dseed_list[i].cse_svn > sec_info->dseed_list[i - 1].cse_svn) {
			max_svn_idx = i;
		}
	}

	return max_svn_idx;
}

static void key_derive(device_sec_info_v0_t *sec_info)
{
	const char salt[] = "Attestation Keybox Encryption Key";
	const uint8_t *ikm;
	uint8_t *prk;
	uint32_t ikm_len;
	int max_svn_idx;

	max_svn_idx = get_max_svn_index(sec_info);
	if (max_svn_idx < 0) {
		print_info("VMM: failed to get max svn index\n");
		memset(sec_info, 0, sizeof(device_sec_info_v0_t));
		return;
	}

	ikm = sec_info->dseed_list[max_svn_idx].seed;
	ikm_len = 32;

	prk = sec_info->attkb_enc_key;

	if (hmac_sha256((const uint8_t *)salt, sizeof(salt), ikm, ikm_len, prk) != 1) {
		memset(sec_info, 0, sizeof(device_sec_info_v0_t));
		print_panic("VMM: failed to derive key!\n");
	}
}
#endif

/* Set up trusty device security info and trusty startup info */
static uint64_t setup_trusty_mem(uint64_t runtime_addr, uint64_t runtime_total_size)
{
	uint32_t dev_sec_info_size;
	trusty_startup_info_t *trusty_para;

	/* Setup trusty boot info */
	dev_sec_info_size = *((uint32_t *)dev_sec_info);
	memcpy((void *)runtime_addr, dev_sec_info, dev_sec_info_size);
	memset(dev_sec_info, 0, dev_sec_info_size);
	barrier();

#ifdef DERIVE_KEY
	key_derive((device_sec_info_v0_t *)runtime_addr);
#endif

	/* Setup trusty startup info */
	trusty_para = (trusty_startup_info_t *)ALIGN_F(runtime_addr + dev_sec_info_size, 8);
	VMM_ASSERT_EX(((uint64_t)trusty_para + sizeof(trusty_startup_info_t)) <
			(runtime_addr + PAGE_4K_SIZE),
			"size of (dev_sec_info+trusty_startup_info) exceeds the reserved 4K size!\n");
	trusty_para->size_of_this_struct  = sizeof(trusty_startup_info_t);
	trusty_para->mem_size             = runtime_total_size;
	trusty_para->calibrate_tsc_per_ms = tsc_per_ms;
	trusty_para->trusty_mem_base      = runtime_addr;

	return (uint64_t)trusty_para;
}

static void before_launching_tee(guest_cpu_handle_t gcpu_trusty)
{
	gcpu_set_gp_reg(gcpu_trusty, REG_RDI, g_init_rdi);
	gcpu_set_gp_reg(gcpu_trusty, REG_RSP, g_init_rsp);
	vmcs_write(gcpu_trusty->vmcs, VMCS_GUEST_RIP, g_init_rip);
}

static uint64_t parse_trusty_boot_param(guest_cpu_handle_t gcpu, uint64_t *runtime_addr, uint64_t *runtime_total_size)
{
	uint64_t rdi;
	trusty_boot_params_t *trusty_boot_params;

	/* avoid warning -Wbad-function-cast */
	rdi = gcpu_get_gp_reg(gcpu, REG_RDI);
	VMM_ASSERT_EX(rdi, "Invalid trusty boot params\n");

	/* trusty_boot_params is passed from OSloader */
	trusty_boot_params = (trusty_boot_params_t *)rdi;

	*runtime_addr = trusty_boot_params->runtime_addr;
	/* osloader alloc 16M memory for trusty */
	*runtime_total_size = 16 MEGABYTE;

	return trusty_boot_params->entry_point;
}

static void first_smc_to_tee(guest_cpu_handle_t gcpu_ree)
{
	uint64_t runtime_addr, runtime_total_size;

	/* 1. Get trusty info from osloader then set trusty startup&sec info.
	 * 2. Set RIP RDI and RSP
	 */
	g_init_rip = parse_trusty_boot_param(gcpu_ree, &runtime_addr, &runtime_total_size);
	g_init_rdi = setup_trusty_mem(runtime_addr, runtime_total_size);
	g_init_rsp = runtime_addr + runtime_total_size;

	mem_free(dev_sec_info);
	dev_sec_info = NULL;

	launch_tee(trusty_guest, runtime_addr, runtime_total_size);
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

static void *sensitive_info;
static void trusty_erase_sensetive_info(void)
{
#ifndef DEBUG //in DEBUG build, access from VMM to Trusty will be removed. so, only erase sensetive info in RELEASE build
	memset(sensitive_info, 0, PAGE_4K_SIZE);
#endif
}

void trusty_register_deadloop_handler(evmm_desc_t *evmm_desc)
{
	D(VMM_ASSERT_EX(evmm_desc, "evmm_desc is NULL\n"));
	sensitive_info = (void *)evmm_desc->trusty_tee_desc.tee_file.runtime_addr;

	register_final_deadloop_handler(trusty_erase_sensetive_info);
}

#ifdef AP_START_IN_HLT
static void set_guest0_aps_to_hlt_state(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	D(VMM_ASSERT(gcpu));

	if (gcpu->guest->id == 0) {
		if (gcpu->id != 0) {
			vmcs_write(gcpu->vmcs, VMCS_GUEST_ACTIVITY_STATE, ACTIVITY_STATE_HLT);
		}
	}
}
#endif

void init_trusty_tee(evmm_desc_t *evmm_desc)
{
	tee_config_t trusty_cfg;
	tee_desc_t *trusty_desc;
	uint32_t dev_sec_info_size;
	uint8_t smc_param_to_tee[] = {REG_RDI, REG_RSI, REG_RDX, REG_RBX};
	uint8_t smc_param_to_ree[] = {REG_RDI, REG_RSI, REG_RDX, REG_RBX};

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

#ifdef PACK_LK
	trusty_cfg.launch_tee_first = TRUE;
#else
	trusty_cfg.launch_tee_first = FALSE;
#endif

	trusty_cfg.before_launching_tee = before_launching_tee;
	trusty_cfg.tee_bsp_status = MODE_32BIT;
	trusty_cfg.tee_ap_status = HLT;

	if (trusty_cfg.launch_tee_first) {
		trusty_cfg.tee_runtime_addr = trusty_desc->tee_file.runtime_addr;
		trusty_cfg.tee_runtime_size = trusty_desc->tee_file.runtime_total_size;

		/* Set RIP RDI and RSP */
		g_init_rip = relocate_trusty_image(&trusty_desc->tee_file);
		g_init_rdi = setup_trusty_mem(trusty_desc->tee_file.runtime_addr, trusty_desc->tee_file.runtime_total_size);
		g_init_rsp = trusty_desc->tee_file.runtime_addr + trusty_desc->tee_file.runtime_total_size;

		dev_sec_info = trusty_desc->dev_sec_info;
	} else {
		trusty_cfg.first_smc_to_tee = first_smc_to_tee;

		/* Copy dev_sec_info from loader to VMM's memory */
		dev_sec_info_size = *((uint32_t *)trusty_desc->dev_sec_info);
		VMM_ASSERT_EX(!(dev_sec_info_size & 0x3ULL), "size of trusty boot info is not 32bit aligned!\n");
		dev_sec_info = mem_alloc(dev_sec_info_size);
		memcpy(dev_sec_info, trusty_desc->dev_sec_info, dev_sec_info_size);
		memset(trusty_desc->dev_sec_info, 0, dev_sec_info_size);
		barrier();
	}

	trusty_guest = create_tee(&trusty_cfg);
	VMM_ASSERT_EX(trusty_guest, "Failed to create trusty guest!\n");

#ifdef AP_START_IN_HLT
	event_register(EVENT_GCPU_MODULE_INIT, set_guest0_aps_to_hlt_state);
#endif

#ifdef DMA_FROM_CSE
	vtd_assign_dev(gid2did(trusty_guest->id), DMA_FROM_CSE);
#endif

	vmcall_register(GUEST_REE, TRUSTY_VMCALL_DUMP_INIT, trusty_vmcall_dump_init);

	return;
}
