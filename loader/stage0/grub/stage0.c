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
#include "grub_boot_param.h"
#include "linux_loader.h"
#include "guest_setup.h"
#include "stage0_lib.h"

#include "lib/image_loader.h"
#include "lib/util.h"
#include "lib/serial.h"

#define TOS_MAX_IMAGE_SIZE            0x100000   /*Max image size assumed to be 1 MB*/
#define MULTIBOOT_HEADER_SIZE         32

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

/* Function: stage0_main
 * Description: Called by start() in stage0_entry.S. Jumps to stage1.
 * This function never returns back.
 */
void stage0_main(const init_register_t *init_reg,
		uint64_t stage0_base)
{
	evmm_desc_t *evmm_desc = NULL;
	uint64_t (*stage1_main) (evmm_desc_t *xd);
	uint64_t boot_param_addr, entry_point;
	multiboot_info_t *mbi = (multiboot_info_t *)(uint64_t)init_reg->ebx;

	print_init(TRUE);

	init_memory_manager(GRUB_HEAP_ADDR, GRUB_HEAP_SIZE);

	evmm_desc = boot_params_parse(mbi);
	if (!evmm_desc) {
		print_panic("evmm desc is NULL\n");
		goto fail;
	}

	if(!linux_kernel_parse(mbi, &boot_param_addr, &entry_point)) {
		print_panic("linux kernel parse failed!\n");
		goto fail;
	}

	if (!g0_gcpu_setup(evmm_desc, boot_param_addr, entry_point)) {
		print_panic("g0 gcpu setup failed!\n");
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

#ifdef MODULE_TRUSTY_GUEST
	setup_32bit_env(&(evmm_desc->trusty_desc.gcpu0_state));
#endif

#ifdef MODULE_OPTEE_GUEST
	setup_32bit_env(&(evmm_desc->optee_desc.gcpu0_state));
#endif

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
