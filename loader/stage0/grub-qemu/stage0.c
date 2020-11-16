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
#include "grub_boot_param.h"
#include "guest_setup.h"
#include "linux_loader.h"
#include "stage0_lib.h"

#include "lib/image_loader.h"
#include "lib/util.h"
#include "lib/serial.h"

#define TOS_MAX_IMAGE_SIZE		0x100000   /* Max image size assumed to be 1 MB */

/* register value ordered by: pushal, pushfl */
typedef struct init_register_protected_mode {
	uint32_t eflags;
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
} init_register_protected_mode_t;

#define STAGE0_RSP_TOP 0xC0000000
/* Function: stage0_main
 * Description: Called by start() in stage0_entry.S. Jumps to stage1.
 * This function never returns back.
 */
void stage0_main(const init_register_protected_mode_t *init_reg,
		uint64_t stage0_base)
{
	evmm_desc_t *evmm_desc = NULL;
	multiboot_info_t *mbi = (multiboot_info_t *)(uint64_t)init_reg->ebx;
	uint64_t (*stage1_main) (evmm_desc_t *xd);
	uint64_t entry = 0;
	uint64_t boot_param_addr = 0;
#ifdef MODULE_TRUSTY_TEE
	uint64_t file_start;
	uint64_t file_size;
	boolean_t ret;
#endif

	if (NULL == mbi) {
		print_panic("Invalid multiboot info\n");
		goto fail;
	}

	print_init(TRUE);

	init_memory_manager(GRUB_HEAP_ADDR, GRUB_HEAP_SIZE);

	evmm_desc = boot_params_parse(mbi);
	if (!evmm_desc) {
		print_panic("evmm desc is NULL\n");
		goto fail;
	}

	if (!check_vmx()) {
		print_panic("VT is not supported\n");
		goto fail;
	}

	if (!file_parse(evmm_desc,
			stage0_base,
			MULTIBOOT_HEADER_SIZE,
			TOS_MAX_IMAGE_SIZE)) {
		print_panic("file parse failed\n");
		goto fail;
	}

#ifdef MODULE_TRUSTY_TEE
	ret = parse_multiboot_module(mbi, &file_start, &file_size, TRUSTYIMG);
	if (FALSE == ret) {
		print_panic("Failed to parse module(%d)\n", TRUSTYIMG);
		goto fail;
	}

	evmm_desc->trusty_tee_desc.tee_file.loadtime_addr = file_start;
	evmm_desc->trusty_tee_desc.tee_file.loadtime_size = file_size;

	ret = linux_kernel_parse(mbi, &boot_param_addr, &entry);
	if(!ret){
		print_info("Try to load test-runner binary.\n");
		ret = parse_multiboot_module(mbi, &file_start, &file_size, TESTRUNNER);
		if (ret) {
			ret = relocate_multiboot_image((uint64_t *)file_start,
					file_size,
					(uint64_t *)&entry);
		}
	}
#else
	linux_kernel_parse(mbi, &boot_param_addr, &entry);
#endif

#ifdef MODULE_TRUSTY_TEE
	/*
	 * Hardcode Trusty runtime address in QEMU project.
	 * In general iKGT design, Trusty runtime address is specified by bootloader,
	 * however, this information would not be provided by bootloader in QEMU project.
	 */
	evmm_desc->trusty_tee_desc.tee_file.runtime_addr = TRUSTY_RUNTIME_BASE;
	reserve_region_from_mmap((boot_params_t *)boot_param_addr,
			evmm_desc->trusty_tee_desc.tee_file.runtime_addr,
			evmm_desc->trusty_tee_desc.tee_file.runtime_total_size);
#endif

	reserve_region_from_mmap((boot_params_t *)boot_param_addr,
		evmm_desc->evmm_file.runtime_addr,
		evmm_desc->evmm_file.runtime_total_size);

	if (0 == entry) {
		print_panic("Entry address invalid\n");
		goto fail;
	}

	/* Primary guest environment setup */
	if(!g0_gcpu_setup(boot_param_addr, evmm_desc, entry)) {
		print_panic("Guest[0] setup failed\n");
		goto fail;
	}

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
