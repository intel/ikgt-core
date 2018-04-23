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
#include "ldr_dbg.h"
#include "guest_setup.h"
#include "stage0_lib.h"
#include "efi_boot_param.h"
#include "device_sec_info.h"

#include "lib/util.h"
#include "lib/serial.h"
#include "lib/image_loader.h"

#define RETURN_ADDRESS() (__builtin_return_address(0))

void cleanup_sensetive_data(uint64_t tos_startup_info)
{
	tos_startup_info_t *p_startup_info = (tos_startup_info_t *)tos_startup_info;
	memset((void *)(p_startup_info->trusty_mem_base+SEED_MSG_DST_OFFSET), 0, sizeof(device_sec_info_v0_t));
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

	evmm_desc = boot_params_parse(tos_startup_info, stage0_base);

	if (!evmm_desc) {
		print_panic("evmm_desc is NULL\n");
		goto exit;
	}

	if (!check_vmx()) {
		print_panic("VT is not supported\n");
		goto exit;
	}

	if(!file_parse(evmm_desc, stage0_base, 0, EVMM_PKG_BIN_SIZE)) {
		print_panic("file parse failed\n");
		goto exit;
	}

	ret_addr = RETURN_ADDRESS();
	/* Primary guest environment setup */
	if(!g0_gcpu_setup(evmm_desc, rsp, (uint64_t)ret_addr)) {
		print_panic("Guest[0] setup failed\n");
		goto exit;
	}

	setup_32bit_env(&(evmm_desc->trusty_desc.gcpu0_state));

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
