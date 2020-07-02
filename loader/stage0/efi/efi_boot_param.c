/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "vmm_base.h"
#include "entry_asm.h"
#include "ldr_dbg.h"
#include "efi_boot_param.h"
#include "stage0_lib.h"

#include "lib/util.h"
#ifdef LIB_EFI_SERVICES
#include "lib/efi/efi_services.h"
#endif

typedef struct {
	evmm_desc_t xd;
	uint8_t seed[BUP_MKHI_BOOTLOADER_SEED_LEN];
} evmm_payload_t;

typedef struct {
	uint8_t image_load[EVMM_PKG_BIN_SIZE];
	uint8_t stage1[STAGE1_RT_SIZE];
	evmm_payload_t payload;
} memory_layout_t;

_Static_assert(sizeof(evmm_payload_t) <= EVMM_PAYLOAD_SIZE,
	       "EVMM_PAYLOAD_SIZE is not big enough to hold evmm_payload_t!");

evmm_desc_t *boot_params_parse(tos_startup_info_t *p_startup_info, uint64_t loader_addr)
{
	memory_layout_t *loader_mem;
	evmm_desc_t *evmm_desc;
	uint64_t barrier_size;

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
	evmm_desc = &(loader_mem->payload.xd);
	memset(evmm_desc, 0, sizeof(evmm_desc_t));

	/* get evmm/stage1 runtime_addr/total_size */
	if (((uint64_t)p_startup_info->vmm_mem_base & PAGE_2MB_MASK) ||
		((uint64_t)p_startup_info->vmm_mem_size & PAGE_2MB_MASK)) {
		print_panic("vmm mem address or size is not 2M align\n");
		return NULL;
	}

	barrier_size = calulate_barrier_size(p_startup_info->vmm_mem_size, MINIMAL_EVMM_RT_SIZE);
	if (barrier_size == (uint64_t)-1) {
		print_panic("vmm mem size is smaller than %u\n", MINIMAL_EVMM_RT_SIZE);
		return NULL;
	}

	evmm_desc->evmm_file.barrier_size = barrier_size;
	evmm_desc->evmm_file.runtime_total_size = (uint64_t)p_startup_info->vmm_mem_size - 2 * barrier_size;
	evmm_desc->evmm_file.runtime_addr = (uint64_t)p_startup_info->vmm_mem_base + barrier_size;

	evmm_desc->stage1_file.runtime_addr = (uint64_t)loader_mem->stage1;
	evmm_desc->stage1_file.runtime_total_size = STAGE1_RT_SIZE;

	evmm_desc->sipi_ap_wkup_addr = (uint64_t)p_startup_info->sipi_ap_wkup_addr;
#ifdef LIB_EFI_SERVICES
	evmm_desc->system_table_base = (uint64_t)p_startup_info->system_table_addr;
	evmm_desc->top_of_mem = efi_get_tom();
#endif

	evmm_desc->tsc_per_ms = TSC_PER_MS;

#ifdef MODULE_TRUSTY_GUEST
	evmm_desc->trusty_desc.lk_file.runtime_addr = (uint64_t)p_startup_info->trusty_mem_base;
	evmm_desc->trusty_desc.lk_file.runtime_total_size = ((uint64_t)(p_startup_info->trusty_mem_size));
	memcpy(evmm_desc->trusty_desc.seed, p_startup_info->seed, BUP_MKHI_BOOTLOADER_SEED_LEN);
#endif

#ifdef MODULE_TRUSTY_TEE
	if (((uint64_t)p_startup_info->trusty_mem_base & PAGE_2MB_MASK) ||
		((uint64_t)p_startup_info->trusty_mem_size & PAGE_2MB_MASK)) {
		print_panic("TEE memory address or size is not 2M align\n");
		return NULL;
	}

	barrier_size = calulate_barrier_size(p_startup_info->trusty_mem_size, MINIMAL_TEE_RT_SIZE);
	if (barrier_size == (uint64_t)-1) {
		print_panic("TEE mem size is smaller than %u\n", MINIMAL_TEE_RT_SIZE);
		return NULL;
	}

	evmm_desc->trusty_tee_desc.tee_file.barrier_size = barrier_size;
	evmm_desc->trusty_tee_desc.tee_file.runtime_total_size = (uint64_t)p_startup_info->trusty_mem_size - 2 * barrier_size;
	evmm_desc->trusty_tee_desc.tee_file.runtime_addr = (uint64_t)p_startup_info->trusty_mem_base + barrier_size;

	memcpy(evmm_desc->trusty_tee_desc.seed, p_startup_info->seed, BUP_MKHI_BOOTLOADER_SEED_LEN);
#endif
	return evmm_desc;
}
