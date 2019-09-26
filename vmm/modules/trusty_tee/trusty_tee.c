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
#include "modules/security_info.h"

#ifdef MODULE_VTD
#include "modules/vtd.h"
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

/* Set up trusty startup info */
static uint64_t setup_startup_info(uint64_t runtime_addr, uint64_t runtime_total_size, uint32_t offset)
{
	trusty_startup_info_t *trusty_para;

	/* Setup trusty startup info */
	trusty_para = (trusty_startup_info_t *)ALIGN_F(runtime_addr + offset, 8);
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

static uint64_t parse_trusty_boot_param(guest_cpu_handle_t gcpu, uint64_t *runtime_addr,
					uint64_t *runtime_total_size, uint64_t *barrier_size)
{
	uint64_t rdi;
	trusty_boot_params_t *boot_params;

	/* avoid warning -Wbad-function-cast */
	rdi = gcpu_get_gp_reg(gcpu, REG_RDI);
	if (rdi == 0) {
		print_warn("Invalid trusty boot params\n");
		return 0;
	}

	/* trusty_boot_params is passed from OSloader */
	boot_params = (trusty_boot_params_t *)rdi;
	if (boot_params->version == 0) {
		/* version 0 interface */
		print_warn("No rawhammer mitigation for TEE\n");
		if (boot_params->size_of_this_struct != sizeof(trusty_boot_params_t) - 2 * sizeof(uint32_t)) {
			print_panic("struct size of version 0 doesn't match\n");
			return 0;
		}

		*runtime_addr = boot_params->runtime_addr;
		/* osloader alloc 16M memory for trusty */
		*runtime_total_size = MINIMAL_TEE_RT_SIZE;
		*barrier_size = 0;

		return boot_params->entry_point;
	} else if (boot_params->version == 1) {
		/* Version 1 interface */
		if (boot_params->size_of_this_struct != sizeof(trusty_boot_params_t)) {
			print_warn("struct size of version 1 doesn't match\n");
			return 0;
		}

		if (boot_params->runtime_size < MINIMAL_TEE_RT_SIZE) {
			print_warn("TEE size is smaller than 0x%x\n", MINIMAL_TEE_RT_SIZE);
			return 0;
		}

		if (boot_params->barrier_size == 0) {
			print_warn("barrier size is 0. No rawhammer mitigation for TEE\n");
		}

		*runtime_addr = boot_params->runtime_addr;
		*runtime_total_size = boot_params->runtime_size;
		*barrier_size = boot_params->barrier_size;

		return boot_params->entry_point;
	} else {
		print_panic("Interface version %u between osload and vmm is not supported\n",
				boot_params->version);
		return 0;
	}
}

static void first_smc_to_tee(guest_cpu_handle_t gcpu_ree)
{
	uint64_t runtime_addr, runtime_total_size, barrier_size;
	uint32_t offset;

	/* 1. Get trusty info from osloader then set trusty startup&sec info.
	 * 2. Set RIP RDI and RSP
	 */
	g_init_rip = parse_trusty_boot_param(gcpu_ree, &runtime_addr, &runtime_total_size, &barrier_size);
	/* check and set the return value to osloader. 0:success, 1:fail */
	if (g_init_rip == 0) {
		gcpu_set_gp_reg(gcpu_ree, REG_RAX, 1);
		return;
	}
	gcpu_set_gp_reg(gcpu_ree, REG_RAX, 0);

	offset = mov_secinfo_from_internal((void *)runtime_addr, dev_sec_info);
	dev_sec_info = NULL;

	g_init_rdi = setup_startup_info(runtime_addr, runtime_total_size, offset);
	g_init_rsp = runtime_addr + runtime_total_size;

	launch_tee(trusty_guest, runtime_addr - barrier_size, runtime_total_size + 2 * barrier_size);
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

void init_trusty_tee(evmm_desc_t *evmm_desc)
{
	uint32_t offset;
	tee_config_t trusty_cfg;
	tee_desc_t *trusty_desc;
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
		/* Check rowhammer mitigation for TEE */
		if (trusty_desc->tee_file.barrier_size == 0)
			print_warn("No rawhammer mitigation for TEE\n");

		trusty_cfg.tee_runtime_addr = trusty_desc->tee_file.runtime_addr
					- trusty_desc->tee_file.barrier_size;
		trusty_cfg.tee_runtime_size = trusty_desc->tee_file.runtime_total_size
					+ 2 * trusty_desc->tee_file.barrier_size;

		offset = mov_secinfo((void *)trusty_desc->tee_file.runtime_addr, trusty_desc->dev_sec_info);

		/* Set RIP RDI and RSP */
		g_init_rip = relocate_trusty_image(&trusty_desc->tee_file);
		g_init_rdi = setup_startup_info(trusty_desc->tee_file.runtime_addr,
			trusty_desc->tee_file.runtime_total_size, offset);
		g_init_rsp = trusty_desc->tee_file.runtime_addr + trusty_desc->tee_file.runtime_total_size;
	} else {
		trusty_cfg.first_smc_to_tee = first_smc_to_tee;

		/* Move dev_sec_info from loader to VMM's memory which will be allocated in below function */
		dev_sec_info = mov_secinfo_to_internal(trusty_desc->dev_sec_info);
	}

	trusty_guest = create_tee(&trusty_cfg);
	VMM_ASSERT_EX(trusty_guest, "Failed to create trusty guest!\n");

#ifdef MODULE_VTD
#ifdef DMA_FROM_CSE
	vtd_assign_dev(gid2did(trusty_guest->id), DMA_FROM_CSE);
#endif
#endif

	vmcall_register(GUEST_REE, TRUSTY_VMCALL_DUMP_INIT, trusty_vmcall_dump_init);

	return;
}
