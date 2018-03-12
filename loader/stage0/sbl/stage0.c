/*******************************************************************************
* Copyright (c) 2015-2018 Intel Corporation
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
#include "sbl_boot_param.h"
#include "stage0_lib.h"
#include "lib/image_loader.h"
#include "lib/util.h"
#include "lib/serial.h"
#include <stack_protect.h>

/* loader memory layout */
typedef struct memory_layout {
	/* below must be page aligned for
	 * further ept protection */
	/* stage1 image in RAM */
	uint8_t stage1[STAGE1_RUNTIME_SIZE];

	evmm_desc_t xd;

	/* add more if any */
} memory_layout_t;

static void fill_g0gcpu0(gcpu_state_t *gcpu_state, payload_gcpu_state_t *payload_gcpu_state)
{
	/* Set basic payload gcpu state from SBL */
	gcpu_state->rip = (uint64_t)payload_gcpu_state->eip;
	gcpu_state->gp_reg[REG_RAX] = (uint64_t)payload_gcpu_state->eax;
	gcpu_state->gp_reg[REG_RBX] = (uint64_t)payload_gcpu_state->ebx;
	gcpu_state->gp_reg[REG_RSI] = (uint64_t)payload_gcpu_state->esi;
	gcpu_state->gp_reg[REG_RDI] = (uint64_t)payload_gcpu_state->edi;
	gcpu_state->gp_reg[REG_RCX] = (uint64_t)payload_gcpu_state->ecx;

	/* Setup other payload gcpu state which doesn't set by SBL */
	setup_32bit_env(gcpu_state);
}

static boolean_t check_params(image_boot_params_t *image_boot)
{
	void *dev_sec_info;
	platform_info_t *plat_info;
	vmm_boot_params_t *vmm_boot;

	dev_sec_info = (void *)image_boot->p_device_sec_info;
	plat_info    = (platform_info_t *)image_boot->p_platform_info;
	vmm_boot     = (vmm_boot_params_t *)image_boot->p_vmm_boot_param;

	if (!(dev_sec_info && plat_info && vmm_boot)) {
		print_panic("dev_sec_info/plat_info/vmm_boot is NULL!\n");
		return FALSE;
	}

	if (plat_info->size_of_this_struct != sizeof(platform_info_t)
		|| vmm_boot->size_of_this_struct != sizeof(vmm_boot_params_t)) {
		print_panic("plat_info/vmm_boot size is not match!\n");
		return FALSE;
	}

	if (!vmm_boot->vmm_heap_addr) {
		print_panic("vmm_heap_addr is NULL\n");
		return FALSE;
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
	uint64_t (*stage1_main)(evmm_desc_t *xd);
	uint64_t tom;
	multiboot_info_t *mbi;
	void *dev_sec_info;
	platform_info_t *plat_info;
	vmm_boot_params_t *vmm_boot;
	image_boot_params_t *image_boot;
	memory_layout_t *loader_mem;
	packed_file_t packed_file[PACK_BIN_COUNT];

	print_init(FALSE);

	/* Phase 1: check vmx status */
	if (!check_vmx()) {
		print_panic("VT is not supported\n");
		goto fail;
	}

#if (defined STACK_PROTECTOR) && (defined DEBUG)
	/* check the extra code, which is emited by gcc when enable
	 * stack protector, if is expected or not.
	 */
	if (!stack_layout_check(*((uint64_t *)&evmm_desc+2))) {
		print_panic("stack layout is corrupted, \
			try to check the extra stack protect code by gcc\n");
		goto fail;
	}
#endif

	/* Phase 2: prepare parameters */
	mbi = (multiboot_info_t *)(uint64_t)init_reg->ebx;

	image_boot = cmdline_parse(mbi);
	if (!image_boot) {
		print_panic("cmdline parse failed!\n");
		goto fail;
	}

	if (!check_params(image_boot)) {
		print_panic("Check params failed!\n");
		goto fail;
	}

	dev_sec_info = (void *)image_boot->p_device_sec_info;
	plat_info    = (platform_info_t *)image_boot->p_platform_info;
	vmm_boot     = (vmm_boot_params_t *)image_boot->p_vmm_boot_param;

	tom = get_top_of_memory(mbi);
	if (tom == 0) {
		print_panic("Failed to get top_of memory from mbi!\n");
		goto fail;
	}

	if (!get_file_params(stage0_base, packed_file)) {
		goto fail;
	}

	/* Phase 3: Get address of evmm description */
	loader_mem = (memory_layout_t *)(uint64_t)vmm_boot->vmm_heap_addr;
	evmm_desc = &loader_mem->xd;

	memset(evmm_desc, 0, sizeof(evmm_desc_t));

	/* Phase 4: fill members of evmm description */
	evmm_desc->num_of_cpu = plat_info->cpu_num;
	evmm_desc->sipi_ap_wkup_addr = (uint64_t)vmm_boot->sipi_page;
	evmm_desc->top_of_mem = tom;
	evmm_desc->tsc_per_ms = plat_info->cpu_frequency_MHz * 1000ULL;

	evmm_desc->stage1_file.loadtime_addr = packed_file[STAGE1_BIN_INDEX].load_addr;
	evmm_desc->stage1_file.loadtime_size = packed_file[STAGE1_BIN_INDEX].size;
	evmm_desc->stage1_file.runtime_addr = (uint64_t)loader_mem->stage1;
	evmm_desc->stage1_file.runtime_total_size = STAGE1_RUNTIME_SIZE;

	evmm_desc->evmm_file.loadtime_addr = packed_file[EVMM_BIN_INDEX].load_addr;
	evmm_desc->evmm_file.loadtime_size = packed_file[EVMM_BIN_INDEX].size;
	evmm_desc->evmm_file.runtime_addr = (uint64_t)vmm_boot->vmm_runtime_addr;
	evmm_desc->evmm_file.runtime_total_size = 4 MEGABYTE;

	fill_g0gcpu0(&evmm_desc->guest0_gcpu0_state, &vmm_boot->payload_cpu_state);

	/* loadtime_addr and loadtime_size don't need to set,
	   runtime_addr will be set in trusty_guest.c */
	evmm_desc->trusty_desc.lk_file.runtime_total_size = 16 MEGABYTE;
	evmm_desc->trusty_desc.dev_sec_info = dev_sec_info;
	/* rip and rsp will be filled in vmcall from osloader */
	setup_32bit_env(&evmm_desc->trusty_desc.gcpu0_state);

	/* Phase 5: relocate stage1 file and move to stage1 */
	if (!relocate_elf_image(&(evmm_desc->stage1_file), (uint64_t *)&stage1_main)) {
		print_panic("relocate stage1 failed\n");
		goto fail;
	}

	/* stage1_main() will only return in case of failure */
	stage1_main(evmm_desc);

	print_panic("stage1_main() returned because of a error.\n");
fail:
	print_panic("deadloop in stage0\n");
	__STOP_HERE__;
}
