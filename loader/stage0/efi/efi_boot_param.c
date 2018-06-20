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
#include "vmm_base.h"
#include "ldr_dbg.h"
#include "efi_boot_param.h"
#include "stage0_lib.h"
#include "device_sec_info.h"

#include "lib/util.h"

typedef struct {
	uint8_t image_load[EVMM_PKG_BIN_SIZE];
	uint8_t stage1[STAGE1_IMG_SIZE];
	evmm_desc_t xd;

	device_sec_info_v0_t dev_sec_info;
} memory_layout_t;

static void fill_trusty_desc(trusty_desc_t *trusty_desc, device_sec_info_v0_t *dev_sec_info, tos_startup_info_t *p_startup_info)
{
	trusty_desc->lk_file.runtime_addr = (uint64_t)p_startup_info->trusty_mem_base;
	trusty_desc->lk_file.runtime_total_size = ((uint64_t)(p_startup_info->trusty_mem_size));
	trusty_desc->dev_sec_info = dev_sec_info;
}

evmm_desc_t *boot_params_parse(uint64_t tos_startup_info, uint64_t loader_addr)
{
	tos_startup_info_t *p_startup_info = (tos_startup_info_t *)tos_startup_info;
	memory_layout_t *loader_mem;
	evmm_desc_t *evmm_desc;

	if(!p_startup_info) {
		print_panic("p_startup_info is NULL\n");
		return NULL;
	}

	if (p_startup_info->version != TOS_STARTUP_VERSION ||
		p_startup_info->size != sizeof(tos_startup_info_t)) {
		print_panic("TOS version/size not match\n");
		return NULL;
	}

	loader_mem = (memory_layout_t *) loader_addr;
	evmm_desc = &(loader_mem->xd);
	memset(evmm_desc, 0, sizeof(evmm_desc_t));

	/* get evmm/stage1 runtime_addr/total_size */
	evmm_desc->evmm_file.runtime_addr = (uint64_t)p_startup_info->vmm_mem_base;
	evmm_desc->evmm_file.runtime_total_size = ((uint64_t)(p_startup_info->vmm_mem_size));

	evmm_desc->stage1_file.runtime_addr = (uint64_t)loader_mem->stage1;
	evmm_desc->stage1_file.runtime_total_size = STAGE1_IMG_SIZE;

	evmm_desc->sipi_ap_wkup_addr = (uint64_t)p_startup_info->sipi_ap_wkup_addr;

	evmm_desc->tsc_per_ms = TSC_PER_MS;
	evmm_desc->top_of_mem = TOP_OF_MEM;

	/* Use dummy info(Seed) for kernelflinger */
	make_dummy_trusty_info(&(loader_mem->dev_sec_info));

	/* Fill trusty_device_info structure at Trusty Memory Base */
	fill_trusty_desc(&(loader_mem->xd.trusty_desc), &(loader_mem->dev_sec_info), p_startup_info);

	return evmm_desc;
}
