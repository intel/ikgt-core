/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "ldr_dbg.h"
#include "abl_boot_param.h"
#include "stage0_lib.h"
#include "device_sec_info.h"
#include "lib/image_loader.h"
#include "lib/util.h"
#include "lib/serial.h"

/* loader memory layout */
typedef struct {
	/* below must be page aligned for
	 * further ept protection */
	/* stage1 image in RAM */
	uint8_t stage1[STAGE1_RUNTIME_SIZE];

	evmm_desc_t xd;

	device_sec_info_v0_t dev_sec_info;

	/* add more if any */
} memory_layout_t;

static boolean_t fill_device_sec_info(device_sec_info_v0_t *dev_sec_info,
				abl_trusty_boot_params_t *trusty_boot,
				android_image_boot_params_t *and_boot)
{
	uint32_t i;
	char serial[MMC_PROD_NAME_WITH_PSN_LEN] = {0};

	dev_sec_info->size_of_this_struct = sizeof(device_sec_info_v0_t);
	dev_sec_info->version = 0;

	/* in manufacturing mode | secure boot disabled | production seed */
	dev_sec_info->flags = 0x1 | 0x0 | 0x0;
	dev_sec_info->platform = 1; /* APL + ABL */

	if (trusty_boot->num_seeds > ABL_SEED_LIST_MAX) {
		print_panic("Number of seeds exceeds predefined max number!\n");
		return FALSE;
	}

	dev_sec_info->num_seeds = trusty_boot->num_seeds;

	memset(dev_sec_info->dseed_list, 0, sizeof(dev_sec_info->dseed_list));

	/* copy seed_list from ABL to dev_sec_info->dseed_list */
	for (i = 0; i < (trusty_boot->num_seeds); i++) {
		dev_sec_info->dseed_list[i].cse_svn = trusty_boot->seed_list[i].svn;
		memcpy(dev_sec_info->dseed_list[i].seed,
			trusty_boot->seed_list[i].seed,
			ABL_SEED_LEN);
	}

	/* Do NOT erase seed here, OSloader need to derive RPMB key from seed */
	//memset(abl_trusty_boot_params->seed_list, 0, sizeof(abl_trusty_boot_params->seed_list));

	/* Setup device_sec_info of trusty info */
	if (!get_emmc_serial(and_boot, serial)) {
		print_panic("Failed to search EMMC serial number from cmdline!\n");
		return FALSE;
	}

	memcpy(dev_sec_info->serial, serial, sizeof(dev_sec_info->serial));

	return TRUE;
}

/* Function: stage0_main
 * Description: Called by start() in stage0_entry.S. Jumps to stage1.
 * This function never returns back.
 */
void stage0_main(
		const init_register_t *init_reg,
		uint64_t stage0_base,
		UNUSED uint64_t rsp)
{
	evmm_desc_t *evmm_desc;
	uint64_t (*stage1_main) (evmm_desc_t *xd);
	multiboot_info_t *mbi;
	uint64_t tom;
	cmdline_params_t cmdline_params;
	device_sec_info_v0_t *dev_sec_info = NULL;
	abl_trusty_boot_params_t *trusty_boot = NULL;
	vmm_boot_params_t *vmm_boot;
	android_image_boot_params_t *and_boot;
	memory_layout_t *loader_mem;
	packed_file_t packed_file[PACK_BIN_COUNT];
	uint64_t barrier_size;

	print_init(FALSE);

	/* Phase 1: check vmx status */
	if (!check_vmx()) {
		print_panic("VT is not supported\n");
		goto fail;
	}

	/* Phase 2: prepare parameters */
	mbi = (multiboot_info_t *)(uint64_t)init_reg->ebx;

	if (!cmdline_parse(mbi, &cmdline_params)) {
		goto fail;
	}

	if (!find_boot_params(&cmdline_params, &trusty_boot, &vmm_boot, &and_boot)) {
		print_panic("find_boot_params failed!\n");
		goto fail;
	}

	tom = get_top_of_memory(mbi);
	if (tom == 0) {
		print_panic("Failed to get top_of memory from mbi!\n");
		goto fail;
	}

	loader_mem = (memory_layout_t *)(uint64_t)vmm_boot->VMMheapAddr;
	dev_sec_info = &loader_mem->dev_sec_info;
	memset(dev_sec_info, 0, sizeof(device_sec_info_v0_t));

	if (!fill_device_sec_info(dev_sec_info, trusty_boot, and_boot)) {
		goto fail;
	}

	if (!get_file_params(stage0_base, packed_file)) {
		goto fail;
	}

	/* Phase 3: Get address of evmm description */
	evmm_desc = &loader_mem->xd;

	memset(evmm_desc, 0, sizeof(evmm_desc_t));

	/* Phase 4: fill members of evmm description */
	evmm_desc->num_of_cpu = cmdline_params.cpu_num;
	evmm_desc->sipi_ap_wkup_addr = (uint64_t)vmm_boot->VMMSipiApWkupAddr;
	evmm_desc->top_of_mem = tom;
	evmm_desc->tsc_per_ms = 0; //The TSC frequency will be set in Stage1

	evmm_desc->stage1_file.loadtime_addr = packed_file[STAGE1_BIN_INDEX].load_addr;
	evmm_desc->stage1_file.loadtime_size = packed_file[STAGE1_BIN_INDEX].size;
	evmm_desc->stage1_file.runtime_addr = (uint64_t)loader_mem->stage1;
	evmm_desc->stage1_file.runtime_total_size = STAGE1_RUNTIME_SIZE;

	evmm_desc->evmm_file.loadtime_addr = packed_file[EVMM_BIN_INDEX].load_addr;
	evmm_desc->evmm_file.loadtime_size = packed_file[EVMM_BIN_INDEX].size;

	barrier_size = calulate_barrier_size((uint64_t)vmm_boot->VMMMemSize << 10, MINIMAL_EVMM_RT_SIZE);
	if (barrier_size == (uint64_t)-1) {
		print_panic("vmm mem size is smaller than %u\n", MINIMAL_EVMM_RT_SIZE);
		goto fail;
	}

	evmm_desc->evmm_file.barrier_size = barrier_size;
	evmm_desc->evmm_file.runtime_total_size = (((uint64_t)vmm_boot->VMMMemSize) << 10) - 2 * barrier_size;
	evmm_desc->evmm_file.runtime_addr = (uint64_t)vmm_boot->VMMMemBase + barrier_size;

	fill_g0gcpu0(&evmm_desc->guest0_gcpu0_state, &and_boot->CpuState);

#ifdef MODULE_TRUSTY_TEE
	/* For trusty, tos.img will be relocated by osloader */
	evmm_desc->trusty_tee_desc.dev_sec_info = dev_sec_info;
#endif

#if defined (MODULE_TRUSTY_GUEST)
	/* tos.img will be relocated by osloader */
	evmm_desc->trusty_desc.lk_file.runtime_total_size = 16 MEGABYTE;
	evmm_desc->trusty_desc.dev_sec_info = dev_sec_info;
	/* rip and rsp will be filled in vmcall from osloader */
	setup_32bit_env(&evmm_desc->trusty_desc.gcpu0_state);
#endif

#if defined (MODULE_OPTEE_GUEST)
#if defined (PACK_OPTEE)
	evmm_desc->optee_desc.optee_file.loadtime_addr = packed_file[OPTEE_BIN_INDEX].load_addr;
	evmm_desc->optee_desc.optee_file.loadtime_size = packed_file[OPTEE_BIN_INDEX].size;
#endif
	evmm_desc->optee_desc.optee_file.runtime_addr = (uint64_t)trusty_boot->TrustyMemBase;
	evmm_desc->optee_desc.optee_file.runtime_total_size = ((uint64_t)(trusty_boot->TrustyMemSize)) << 10;
	evmm_desc->optee_desc.dev_sec_info = dev_sec_info;
	/* rip and rsp will be filled in vmcall from osloader */
	setup_32bit_env(&evmm_desc->optee_desc.gcpu0_state);
#endif

	/* Phase 5: relocate stage1 file and move to stage1 */
	if (!relocate_elf_image(&(evmm_desc->stage1_file), (uint64_t *)&stage1_main)) {
		print_panic("relocate stage1 failed\n");
		goto fail;
	}

	stage1_main(evmm_desc);
	//stage1_main() will only return in case of failure.
	print_panic("stage1_main() returned because of a error.\n");
fail:
	print_panic("deadloop in stage0\n");

	if (trusty_boot) {
		memset(trusty_boot, 0, sizeof(abl_trusty_boot_params_t));
		barrier();
	}

	if (dev_sec_info) {
		memset(dev_sec_info, 0, sizeof(device_sec_info_v0_t));
		barrier();
	}

	__STOP_HERE__;
}
/* End of file */
