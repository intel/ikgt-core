/*******************************************************************************
* Copyright (c) 2017 Intel Corporation
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
#include "vmm_asm.h"
#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "file_pack.h"
#include "ldr_dbg.h"
#include "guest_setup.h"
#include "efi_boot_param.h"

#include "lib/util.h"
#include "lib/serial.h"
#include "lib/image_loader.h"
#include <stack_protect.h>

#define RETURN_ADDRESS() (__builtin_return_address(0))

static boolean_t file_parse(evmm_desc_t *evmm_desc, uint64_t stage0_base)
{
	file_offset_header_t *file_hdr;
	file_hdr = get_file_offsets_header((uint64_t)stage0_base, EVMM_PKG_BIN_SIZE);
	if (file_hdr == NULL) {
		print_panic("failed to find file header\n");
		return FALSE;
	}

	/* save module information (file mapped address in RAM + base location )
	 *  TODO: better to caculate what address is stage0 loaded by bootstub...instead of
	 *        using the hardcode address.
	 */
	if (file_hdr->file_size[STAGE1_BIN_INDEX]) {
		evmm_desc->stage1_file.loadtime_addr = stage0_base +
			file_hdr->file_size[STAGE0_BIN_INDEX];
		evmm_desc->stage1_file.loadtime_size =
			file_hdr->file_size[STAGE1_BIN_INDEX];
	} else {
		print_panic("stage1 file size is zero\n");
		return FALSE;
	}

	if (file_hdr->file_size[EVMM_BIN_INDEX]) {
		evmm_desc->evmm_file.loadtime_addr = evmm_desc->stage1_file.loadtime_addr +
			evmm_desc->stage1_file.loadtime_size;
		evmm_desc->evmm_file.loadtime_size = file_hdr->file_size[EVMM_BIN_INDEX];
	} else {
		print_panic("evmm file size is zero\n");
		return FALSE;
	}

#if defined (MODULE_TRUSTY_GUEST) && defined (PACK_LK)
	if (file_hdr->file_size[LK_BIN_INDEX]) {
		evmm_desc->trusty_desc.lk_file.loadtime_addr = evmm_desc->evmm_file.loadtime_addr +
			evmm_desc->evmm_file.loadtime_size;
		evmm_desc->trusty_desc.lk_file.loadtime_size = file_hdr->file_size[LK_BIN_INDEX];
	} else {
		print_panic("lk file size is zero\n");
		return FALSE;
	}
#endif

	return TRUE;
}

void cleanup_sensetive_data(uint64_t tos_startup_info)
{
	tos_startup_info_t *p_startup_info = (tos_startup_info_t *)tos_startup_info;
	memset((void *)(p_startup_info->trusty_mem_base+SEED_MSG_DST_OFFSET), 0, sizeof(trusty_device_info_t));
}

/* Function: stage0_main
 * Description: Called by start() in stage0_entry.S. Jumps to stage1.
 * Calling convention:
 *   rdi, rsi, rdx, rcx, r8, r9, stack1, stack2
 */
uint32_t stage0_main(
	uint64_t tos_startup_info,
	uint64_t stage0_base,
	uint64_t rsp)
{
	evmm_desc_t *evmm_desc;
	uint64_t (*stage1_main) (evmm_desc_t *xd);
	void *ret_addr;

	print_init(FALSE);

#if (defined STACK_PROTECTOR) && (defined DEBUG)
	/* check the extra code, which is emited by gcc when enable stack protector,
 	 * if is expected or not.
 	 */
	if (!stack_layout_check(*((uint64_t*)&evmm_desc+2))) {
		print_panic("stack layout is corrupted, \
			try to check the extra stack protect code by gcc\n");
		goto exit;
	}
#endif

	evmm_desc = boot_params_parse(tos_startup_info, stage0_base);

	if (!evmm_desc) {
		print_panic("evmm_desc is NULL\n");
		goto exit;
	}

	if (!check_vmx()) {
		print_panic("VT is not supported\n");
		goto exit;
	}

	if(!file_parse(evmm_desc, stage0_base)) {
		print_panic("file parse failed\n");
		goto exit;
	}

	ret_addr = RETURN_ADDRESS();
	/* Primary guest environment setup */
	if(!g0_gcpu_setup(evmm_desc, rsp, (uint64_t)ret_addr)) {
		print_panic("Guest[0] setup failed\n");
		goto exit;
	}

	if (!trusty_setup(evmm_desc)) {
		print_panic("trusty setup failed\n");
		goto exit;
	}

	if (!relocate_elf_image(&(evmm_desc->stage1_file), (uint64_t *)&stage1_main)) {
		print_panic("relocate stage1 image failed\n");
		goto exit;
	}

	stage1_main(evmm_desc);
exit:
	/* wipe rot/seed data in the beginning of IMR when error occurs */
	cleanup_sensetive_data(tos_startup_info);
	/* Code will not run to here when boot successfully.
	 * The return value is set in g0_gcpu_setup() when do gcpu_resume. */
	return -1;
}
/* End of file */
