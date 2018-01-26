/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
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
#include "abl_boot_param.h"
#include "trusty_info.h"
#include "guest_setup.h"
#include "stage0_lib.h"
#include "lib/image_loader.h"
#include "lib/util.h"
#include "lib/serial.h"
#include <stack_protect.h>

#define TOS_MAX_IMAGE_SIZE            0x100000   /*Max image size assumed to be 1 MB*/
#define MULTIBOOT_HEADER_SIZE         32

/* Function: stage0_main
 * Description: Called by start() in stage0_entry.S. Jumps to stage1.
 * This function never returns back.
 */
void stage0_main(
		const init_register_t *init_reg,
		uint64_t stage0_base,
		UNUSED uint64_t rsp)
{
	evmm_desc_t *evmm_desc = NULL;
	uint64_t (*stage1_main) (evmm_desc_t *xd);

	print_init(FALSE);

#if (defined STACK_PROTECTOR) && (defined DEBUG)
	/* check the extra code, which is emited by gcc when enable stack protector,
 	 * if is expected or not.
 	 */
	if (!stack_layout_check(*((uint64_t*)&evmm_desc+2))) {
		print_panic("stack layout is corrupted, \
			try to check the extra stack protect code by gcc\n");
		goto fail;
	}
#endif

	evmm_desc = boot_params_parse(init_reg);
	if (!evmm_desc) {
		print_panic("evmm desc is NULL\n");
		goto fail;
	}

	if (!check_vmx()) {
		print_panic("VT is not supported\n");
		goto fail;
	}

	if (!file_parse(evmm_desc, stage0_base, MULTIBOOT_HEADER_SIZE, TOS_MAX_IMAGE_SIZE)) {
		print_panic("file parse failed\n");
		goto fail;
	}

	setup_32bit_env(&(evmm_desc->trusty_desc.gcpu0_state));

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
