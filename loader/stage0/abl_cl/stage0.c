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
#include "linux_loader.h"
#include "stage0_mmap.h"
#include "lib/image_loader.h"
#include "lib/util.h"
#include "lib/serial.h"
#include "lib/string.h"

#define EVMM_RUNTIME_SIZE           0x400000        //4M
#define LK_RUNTIME_SIZE             0x1000000       //16M
#define STAGE1_RUNTIME_SIZE         0xC000
#define MAX_LINUX_CMDLINE_LEN       1024

/* register value ordered by: pushal, pushfl */
typedef struct init_register {
	uint32_t eflags;
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
} init_register_t;

/* loader memory layout */
typedef struct {
	/* linux boot params, one page */
	boot_params_t boot_params;

	/* stage1 image in RAM */
	uint8_t stage1[STAGE1_RUNTIME_SIZE];

	evmm_desc_t xd;

	device_sec_info_v0_t dev_sec_info;

	/* add more if any */
} memory_layout_t;

static uint32_t get_image_size(uint64_t base)
{
	uint32_t i, sum = 0;

	 file_offset_header_t *file_hdr;

	 /* Find file offsets header */
	 file_hdr = get_file_offsets_header(base, 0x100000);
	 if (file_hdr == NULL) {
		print_panic("failed to find file header\n");
		return 0;
	}

	for (i=0; i<PACK_BIN_COUNT; i++) {
		sum += file_hdr->file_size[i];
	}

	return sum;
}

static boolean_t fill_device_sec_info(device_sec_info_v0_t *dev_sec_info, uint64_t svnseed_addr, uint64_t rpmb_key_addr)
{
	uint32_t i;
	abl_svnseed_t *svnseed = (abl_svnseed_t *)svnseed_addr;
	uint8_t *rpmb_key = (uint8_t *)rpmb_key_addr;

	if (!svnseed) {
		print_panic("%s: Invalid input params!\n", __func__);
		return FALSE;
	}

	if (svnseed->size_of_this_struct != sizeof(abl_svnseed_t)) {
		print_panic("%s: structure size of abl_svnseed mismatch!\n", __func__);
		return FALSE;
	}

	if (svnseed->num_seeds > ABL_SEED_LIST_MAX) {
		print_panic("%s: number of svnseed exceed max value!\n", __func__);
		return FALSE;
	}

	dev_sec_info->size_of_this_struct = sizeof(device_sec_info_v0_t);
	dev_sec_info->version = 0;

	/* in manufaturing mode | secure boot disabled | production seed */
	dev_sec_info->flags = 0x1 | 0x0 | 0x0;
	dev_sec_info->platform = 1; // APL + ABL

	dev_sec_info->num_seeds = svnseed->num_seeds;

	/* copy svnseed to dseed_list */
	for (i = 0; i < svnseed->num_seeds; i++) {
		dev_sec_info->dseed_list[i].cse_svn = svnseed->seed_list[i].svn;
		memcpy(dev_sec_info->dseed_list[i].seed, svnseed->seed_list[i].seed, ABL_SEED_LEN);
	}

	/* clear original seed */
	memset(svnseed, 0, sizeof(abl_svnseed_t));

	if (rpmb_key) {
		/* RPMB key provisioned by ABL */
		/* copy rpmb key */
		memcpy(&dev_sec_info->rpmb_key[0][0], rpmb_key, ABL_RPMB_KEY_LEN);

		/* clear original rpmb key */
		memset(rpmb_key, 0, ABL_RPMB_KEY_LEN);
	} else {
		/* RPMB key is not provisioned, set a pre-defined key */
		memset(dev_sec_info->rpmb_key, 0x00, sizeof(dev_sec_info->rpmb_key));
	}

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
	device_sec_info_v0_t *dev_sec_info;
	memory_layout_t *loader_mem;
	packed_file_t packed_file[PACK_BIN_COUNT];
	boolean_t ret;
	uint32_t tmp_type, bundle_image_size;
	uint64_t mem_base, mem_len, align_base, align_len;
	uint64_t heap_base, heap_size, sipi_page;
	uint64_t vmm_runtime_base, linux_runtime_base, linux_runtime_size, total_size;
	uint32_t cmdline_len;
#ifdef PACK_LK
	uint64_t lk_runtime_base;
#endif

	print_init(FALSE);

	/* Phase 1: check vmx status */
	if (!check_vmx()) {
		print_panic("VT is not supported\n");
		goto fail;
	}

	/* Phase 2: prepare parameters */
	memset(&cmdline_params, 0, sizeof(cmdline_params_t));

	mbi = (multiboot_info_t *)(uint64_t)init_reg->ebx;

	if (!cmdline_parse(mbi, &cmdline_params)) {
		goto fail;
	}

	tom = get_top_of_memory(mbi);
	if (tom == 0) {
		print_panic("Failed to get top of mem!\n");
		goto fail;
	}

	if (!get_file_params(stage0_base, packed_file)) {
		goto fail;
	}

	/* Phase 3: Prepare memory for vmm , LK, linux, heap and sipi.
	 *          Memory layout for vmm, lk, linux and heap:
	 *              heap: sizeof(memory_layout_t)
	 *              Linux: bzImage bytes
	 *              LK: 16M bytes(if exist)
	 *              EVMM: 4M bytes
	 *          Sipi page(4K bytes) is another memory region below 1M.
	 */
	ret = init_stage0_mmap(mbi, &tmp_type);
	if (!ret) {
		print_panic("Failed to init stage0 mmap!\n");
		goto fail;
	}

	bundle_image_size = get_image_size(stage0_base);
	if (bundle_image_size == 0) {
		print_panic("Failed to get image size!\n");
		goto fail;
	}

	cmdline_len = strnlen_s((const char *)(uint64_t)mbi->cmdline, MAX_LINUX_CMDLINE_LEN);
	if (cmdline_len == 0) {
		print_panic("Failed to get cmdline length!\n");
		goto fail;
	}

	/* There are some memory regions which are already used by ABL.
	 * Mark the regions as sepcial type to avoid alloc memory from them.
	 * Don't care the return value here
	 */
	insert_stage0_mmap(stage0_base, bundle_image_size, tmp_type);
	insert_stage0_mmap(cmdline_params.bzImage_base, cmdline_params.bzImage_size, tmp_type);
	insert_stage0_mmap(cmdline_params.initrd_base, cmdline_params.initrd_size, tmp_type);
	insert_stage0_mmap((uint64_t)mbi, sizeof(multiboot_info_t), tmp_type);
	insert_stage0_mmap((uint64_t)mbi->mmap_addr, (uint64_t)mbi->mmap_length, tmp_type);
	insert_stage0_mmap((uint64_t)mbi->cmdline, (uint64_t)cmdline_len + 1, tmp_type);
	insert_stage0_mmap(cmdline_params.svnseed_addr, sizeof(abl_svnseed_t), tmp_type);
	if (cmdline_params.rpmb_key_addr) {
		insert_stage0_mmap(cmdline_params.rpmb_key_addr, ABL_RPMB_KEY_LEN, tmp_type);
	}

	ret = get_max_stage0_mmap(&mem_base, &mem_len);
	if (!ret) {
		print_panic("Failed to get max memory from mmap!\n");
		goto fail;
	}

	/* The start of the address shoule be 2M aligned due to trusty requriement and performance consideration */
	align_base = PAGE_ALIGN_2M((uint64_t)mem_base);
	align_len = mem_len - (align_base - mem_base);

	linux_runtime_size = cmdline_params.bzImage_size;
	heap_size = sizeof(memory_layout_t);
	total_size = EVMM_RUNTIME_SIZE + linux_runtime_size + heap_size;
#ifdef PACK_LK
	total_size += LK_RUNTIME_SIZE;
#endif
	if (align_len < total_size) {
		print_panic("No enough memory for runtime and heap!\n");
		goto fail;
	}

	/* Reserve mem for evmm */
	vmm_runtime_base = align_base;
	ret = insert_stage0_mmap(align_base, EVMM_RUNTIME_SIZE, MULTIBOOT_MEMORY_RESERVED);
	if (!ret) {
		print_panic("Failed to reserve mem for evmm!\n");
		goto fail;
	}

	align_base += EVMM_RUNTIME_SIZE;

#ifdef PACK_LK
	/* Reserve mem for trusty */
	lk_runtime_base = align_base;
	ret = insert_stage0_mmap(align_base, LK_RUNTIME_SIZE, MULTIBOOT_MEMORY_RESERVED);
	if (!ret) {
		print_panic("Failed to reserve mem for lk!\n");
		goto fail;
	}

	align_base += LK_RUNTIME_SIZE;
#endif

	/* Reserve mem for linux and heap */
	linux_runtime_base = (uint64_t)align_base;
	heap_base = linux_runtime_base + linux_runtime_size;
	ret = insert_stage0_mmap(linux_runtime_base, linux_runtime_size + heap_size, tmp_type);
	if (!ret) {
		print_panic("Failed to reserve mem for linux and heap!\n");
		goto fail;
	}

	/* Get mem for sipi */
	sipi_page = get_sipi_page();
	if (sipi_page == 0) {
		print_panic("Failed to get mem for sipi!\n");
		goto fail;
	}

	loader_mem = (memory_layout_t *)heap_base;
	dev_sec_info = &loader_mem->dev_sec_info;
	memset(dev_sec_info, 0, sizeof(device_sec_info_v0_t));

	fill_device_sec_info(dev_sec_info, cmdline_params.svnseed_addr, cmdline_params.rpmb_key_addr);

	/* Phase 4: Get address of evmm description */
	evmm_desc = &loader_mem->xd;

	memset(evmm_desc, 0, sizeof(evmm_desc_t));

	/* Phase 5: fill members of evmm description */
	evmm_desc->num_of_cpu = (uint8_t)cmdline_params.cpu_num;
	evmm_desc->sipi_ap_wkup_addr = sipi_page;
	evmm_desc->top_of_mem = tom;
	evmm_desc->tsc_per_ms = 0; //The TSC freq will be set in stage1

	evmm_desc->stage1_file.loadtime_addr = packed_file[STAGE1_BIN_INDEX].load_addr;
	evmm_desc->stage1_file.loadtime_size = packed_file[STAGE1_BIN_INDEX].size;
	evmm_desc->stage1_file.runtime_addr = (uint64_t)loader_mem->stage1;
	evmm_desc->stage1_file.runtime_total_size = STAGE1_RUNTIME_SIZE;

	evmm_desc->evmm_file.loadtime_addr = packed_file[EVMM_BIN_INDEX].load_addr;
	evmm_desc->evmm_file.loadtime_size = packed_file[EVMM_BIN_INDEX].size;
	evmm_desc->evmm_file.runtime_addr = (uint64_t)vmm_runtime_base;
	evmm_desc->evmm_file.runtime_total_size = EVMM_RUNTIME_SIZE;

#ifdef PACK_LK
	evmm_desc->trusty_desc.lk_file.loadtime_addr = packed_file[LK_BIN_INDEX].load_addr;
	evmm_desc->trusty_desc.lk_file.loadtime_size = packed_file[LK_BIN_INDEX].size;
	evmm_desc->trusty_desc.lk_file.runtime_addr = (uint64_t)lk_runtime_base;
	evmm_desc->trusty_desc.lk_file.runtime_total_size = LK_RUNTIME_SIZE;
	evmm_desc->trusty_desc.dev_sec_info = dev_sec_info;
	/* rip and rsp will be filled in */
	setup_32bit_env(&evmm_desc->trusty_desc.gcpu0_state);
#endif

	/* Phase 6: Prepare linux load env */
	ret = load_linux_kernel(cmdline_params.bzImage_base,
				cmdline_params.bzImage_size,
				cmdline_params.initrd_base,
				cmdline_params.initrd_size,
				linux_runtime_base,
				linux_runtime_size,
				mbi->cmdline,
				(uint64_t)&loader_mem->boot_params,
				&evmm_desc->guest0_gcpu0_state);
	if (!ret) {
		print_panic("%s: Failed to load linux kernel!\n", __func__);
		goto fail;
	}

	ret = stage0_mmap_to_e820(&loader_mem->boot_params);
	if (!ret) {
		print_panic("failed to convert mmap to e820!\n");
		goto fail;
	}

	/* Phase 7: relocate stage1 file and move to stage1 */
	if (!relocate_elf_image(&(evmm_desc->stage1_file), (uint64_t *)&stage1_main)) {
		print_panic("relocate stage1 failed\n");
		goto fail;
	}

	stage1_main(evmm_desc);
	//stage1_main() will only return in case of failure.
	print_panic("stage1_main() returned because of a error.\n");
fail:
	print_panic("deadloop in stage0\n");
	__STOP_HERE__;
}
/* End of file */
