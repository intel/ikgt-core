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

#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "sbl_boot_param.h"
#include "trusty_info.h"
#include "stage0_lib.h"
#include "ldr_dbg.h"

#include "lib/util.h"
#include "lib/string.h"

#define CHECK_FLAG(flag, bit)   ((flag) & (1 << (bit)))
#define STAGE1_IMG_SIZE         0xC000

/* loader memory layout */
typedef struct memory_layout {
	/* below must be page aligned for
	 * further ept protection */
	/* stage1 image in RAM */
	uint8_t stage1[STAGE1_IMG_SIZE];

	evmm_desc_t xd;

	/* add more if any */
} memory_layout_t;

static void fill_evmm_boot_params(evmm_desc_t *evmm_desc, vmm_boot_params_t *vmm_boot_params)
{
	memory_layout_t *loader_mem;

	loader_mem = (memory_layout_t *)(uint64_t)vmm_boot_params->vmm_heap_addr;

	evmm_desc->evmm_file.runtime_addr = (uint64_t)vmm_boot_params->vmm_runtime_addr;
	evmm_desc->evmm_file.runtime_total_size = 4 MEGABYTE;

	evmm_desc->stage1_file.runtime_addr = (uint64_t)loader_mem->stage1;
	evmm_desc->stage1_file.runtime_total_size = STAGE1_IMG_SIZE;

	evmm_desc->sipi_ap_wkup_addr = (uint64_t)vmm_boot_params->sipi_page;
}

static inline void fill_trusty_desc(trusty_desc_t *trusty_desc, void *dev_sec_info)
{
	trusty_desc->lk_file.runtime_total_size = 16 MEGABYTE;
	trusty_desc->dev_sec_info = dev_sec_info;
}

static inline void fill_platform_info(evmm_desc_t *evmm_desc, platform_info_t *platform_info)
{
	evmm_desc->num_of_cpu = platform_info->cpu_num;
	evmm_desc->tsc_per_ms = platform_info->cpu_frequency_MHz * 1000ULL;
}

static void fill_payload_params(gcpu_state_t *gcpu_state, payload_gcpu_state_t *payload_gcpu_state)
{
	gcpu_state->rip = (uint64_t)payload_gcpu_state->eip;
	gcpu_state->gp_reg[REG_RAX] = (uint64_t)payload_gcpu_state->eax;
	gcpu_state->gp_reg[REG_RBX] = (uint64_t)payload_gcpu_state->ebx;
	gcpu_state->gp_reg[REG_RSI] = (uint64_t)payload_gcpu_state->esi;
	gcpu_state->gp_reg[REG_RDI] = (uint64_t)payload_gcpu_state->edi;
	gcpu_state->gp_reg[REG_RCX] = (uint64_t)payload_gcpu_state->ecx;
}

/* The cmdline is present like:
 *       "ImageBootParamsAddr=0x12345"
 */
static image_boot_params_t *cmdline_parse(multiboot_info_t *mbi)
{
	const char *cmdline;
	const char *arg;
	const char *param;
	uint32_t addr;

	if (!mbi) {
		print_panic("Multiboot info is NULL!\n");
		return NULL;
	}

	if (!CHECK_FLAG(mbi->flags, 2)) {
		print_panic("Multiboot info does not contain cmdline field!\n");
		return NULL;
	}

	cmdline = (const char *)(uint64_t)mbi->cmdline;

	/* Parse ImageBootParamsAddr */
	print_trace("cmdline form SBL: %s\n", cmdline);
	arg = strstr_s(cmdline, MAX_STR_LEN, "ImageBootParamsAddr=",
				sizeof("ImageBootParamsAddr=")-1);

	if (!arg) {
		print_panic("ImageBootParamsAddr not found in cmdline!\n");
		return NULL;
	}

	param = arg + sizeof("ImageBootParamsAddr=") - 1;

	addr = str2uint(param, 18, NULL, 16);
	if ((addr == (uint32_t)-1) || (addr == 0)) {
		print_panic("Failed to parse image_boot_param_addr!\n");
		return NULL;
	}

	return (image_boot_params_t *)(uint64_t)addr;
}

static evmm_desc_t *setup_boot_params(image_boot_params_t *image_boot_params)
{
	void *dev_sec_info;
	platform_info_t *platform_info;
	vmm_boot_params_t *vmm_boot_param;
	evmm_desc_t *evmm_desc;
	memory_layout_t *loader_mem;

	/* Check information from SBL*/
	if (!image_boot_params ||
		image_boot_params->size_of_this_struct != sizeof(image_boot_params_t)) {
		print_panic("size of image_boot_params is not match!\n");
		return NULL;
	}

	dev_sec_info = (void *)image_boot_params->p_device_sec_info;
	platform_info   = (platform_info_t *)image_boot_params->p_platform_info;
	vmm_boot_param  = (vmm_boot_params_t *)image_boot_params->p_vmm_boot_param;

	if (!(dev_sec_info && platform_info && vmm_boot_param)) {
		print_panic("dev_sec_info/platform_info/vmm_boot_param is NULL!\n");
		return NULL;
	}

	if (platform_info->size_of_this_struct != sizeof(platform_info_t) ||
		vmm_boot_param->size_of_this_struct != sizeof(vmm_boot_params_t)) {
		print_panic("size of platform_info or vmm_boot_param is not match!\n");
		return NULL;
	}

	loader_mem = (memory_layout_t *)(uint64_t)vmm_boot_param->vmm_heap_addr;
	if (!loader_mem) {
		print_panic("vmm_heap_addr is NULL\n");
		return NULL;
	}

	evmm_desc = &loader_mem->xd;
	memset(evmm_desc, 0, sizeof(evmm_desc_t));

	/* Setup evmm boot params */
	fill_evmm_boot_params(evmm_desc, vmm_boot_param);

	/* Setup payload params */
	fill_payload_params(&evmm_desc->guest0_gcpu0_state, &vmm_boot_param->payload_cpu_state);

	/* Setup payload gcpu state which doesn't set on sbl */
	setup_32bit_env(&evmm_desc->guest0_gcpu0_state);

#ifdef MODULE_TRUSTY_GUEST
	/* Setup trusty info */
	fill_trusty_desc(&loader_mem->xd.trusty_desc, dev_sec_info);
#endif

	/* Setup platform info */
	fill_platform_info(evmm_desc, platform_info);

	return evmm_desc;
}

evmm_desc_t *boot_params_parse(const init_register_t *init_reg)
{
	evmm_desc_t *evmm_desc = NULL;
	image_boot_params_t *boot_param;
	uint64_t tom = 0;

	multiboot_info_t *mbi = (multiboot_info_t *)(uint64_t)init_reg->ebx;

	boot_param = cmdline_parse(mbi);
	if (!boot_param) {
		print_panic("cmdline parse failed!\n");
		return NULL;
	}

	evmm_desc = setup_boot_params(boot_param);
	if (!evmm_desc) {
		print_panic("Failed to setup boot params!\n");
		return NULL;
	}

	tom = get_top_of_memory(mbi);
	if (tom == 0) {
		print_panic("Failed to get top_of memory from mbi!\n");
		return NULL;
	}

	evmm_desc->top_of_mem = tom;

	return evmm_desc;
}
