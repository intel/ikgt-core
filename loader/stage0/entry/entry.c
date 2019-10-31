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
#include "stage0_lib.h"
#include "file_pack.h"

#include "lib/serial.h"
#include "lib/image_loader.h"

/*
 * Layout of stage0.bin:
 *
 *             -----------------------------------------
 *             |        ^          |            |       | High
 *             |        |          |            |       |
 *             | all elf sections  | stage0.elf |       |
 *             | (.text, .bss ...) |            |   M   |
 * ld_base --> +--------------------------------|   E   |
 *             |        ^          |            |   M   |
 *             |        |          |            |   O   |
 *             |  .stage0_runtime  |            |   R   |
 *             |   section         | entry.bin  |   Y   |
 * rt_base --> +-------------------|            |       |
 *             |        ^          |            |       |
 *             |   .text section   |            |       | Low
 * entry_base->+-----------------------------------------
 */
static void get_stage0_elf(uint64_t entry_base, uint64_t ld_base, uint64_t rt_base, uint64_t bin_size, module_file_info_t *file)
{
	file->loadtime_addr = ld_base;
	file->loadtime_size = entry_base + bin_size - ld_base;
	file->runtime_addr = rt_base;
	file->runtime_total_size = ld_base - rt_base;
}

/* Function: entry_main
 * Description: Called by start() in entry_64.S/entry_32.S. Jumps to stage0.
 * Calling convention:
 *   rdi, rsi, rdx, rcx, r8, r9, stack1, stack2
 */
uint32_t entry_main(
	const init_register_t *init_reg,
	const file_offset_header_t *header,
	uint64_t image_base,
	uint64_t stage0_rt_base,
	uint64_t stage0_ld_base)
{
	uint32_t (*stage0_main) (const init_register_t *init_reg, uint64_t stage0_base) = NULL;
	uint64_t ret = -1;
	module_file_info_t stage0_elf_file;

	print_init(FALSE);

	get_stage0_elf(image_base, stage0_ld_base, stage0_rt_base, header->file_size[STAGE0_BIN_INDEX], &stage0_elf_file);

	if (!relocate_elf_image(&stage0_elf_file, (uint64_t *)&stage0_main)) {
		print_panic("relocate stage0 image failed\n");
		goto exit;
	}

	ret = stage0_main(init_reg, image_base);

exit:
	return ret;
}
