/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "grub_boot_param.h"
#include "ldr_dbg.h"
#include "device_sec_info.h"
#include "stage0_lib.h"

#include "lib/util.h"
#include "lib/string.h"

#define RUNTIME_MEMORY_ADDR         GRUB_HEAP_ADDR
#define EVMM_RUNTIME_SIZE           0x400000        //4M
#define LK_RUNTIME_SIZE             0x1000000       //16M
#define RUNTIME_MEMORY_SIZE         0x1400000       //20M

#define SIPI_AP_WKUP_ADDR           0x59000

#define STAGE1_IMG_SIZE             0xC000

/* loader memory layout */
typedef struct {
	/* below must be page aligned for
	 * further ept protection */
	/* stage1 image in RAM */
	uint8_t stage1[STAGE1_IMG_SIZE];

	evmm_desc_t xd;

	device_sec_info_v0_t dev_sec_info;
	/* add more if any */
} memory_layout_t;

static uint64_t heap_current;
static uint64_t heap_top;

void init_memory_manager(uint64_t heap_base_address, uint32_t heap_size)
{
	heap_current = heap_base_address;
	heap_top = heap_base_address + heap_size;
}

void *allocate_memory(uint32_t size_request)
{
	uint64_t address;

	if (heap_current + size_request > heap_top) {
		print_panic("memory allocation faied, current heap = 0x%llx, size = 0x%lx,"
				" heap top = 0x%llx\n", heap_current, size_request, heap_top);
		return NULL;
	}

	address = heap_current;
	heap_current += size_request;
	memset((void *)address, 0, size_request);
	return (void *)address;
}

static evmm_desc_t *init_evmm_desc(void)
{
	evmm_desc_t *evmm_desc = NULL;
	void *evmm_runtime_mem = NULL;
	memory_layout_t *loader_mem = NULL;

	/*allocate memory for every block*/
	evmm_runtime_mem = allocate_memory(EVMM_RUNTIME_SIZE);
	if (evmm_runtime_mem == NULL) {
		print_panic("allocate evmm runtime mem failed!\n");
		return NULL;
	}

	loader_mem = (memory_layout_t *)allocate_memory(sizeof(memory_layout_t));
	if (loader_mem == NULL) {
		print_panic("allocate loader mem failed!\n");
		return NULL;
	}

	/*fill evmm boot params*/
	evmm_desc = &(loader_mem->xd);

	evmm_desc->evmm_file.runtime_addr = (uint64_t)evmm_runtime_mem;
	evmm_desc->evmm_file.runtime_total_size = EVMM_RUNTIME_SIZE;

	evmm_desc->stage1_file.runtime_addr = (uint64_t)loader_mem->stage1;
	evmm_desc->stage1_file.runtime_total_size = STAGE1_IMG_SIZE;

	evmm_desc->sipi_ap_wkup_addr = (uint64_t)SIPI_AP_WKUP_ADDR;

	/* Use dummy info(Seed) for Grub */
	make_dummy_trusty_info(&(loader_mem->dev_sec_info));

	/*fill trusty boot params*/
	evmm_desc->trusty_desc.lk_file.runtime_addr = 0;
	evmm_desc->trusty_desc.lk_file.runtime_total_size = LK_RUNTIME_SIZE;
	evmm_desc->trusty_desc.dev_sec_info = &(loader_mem->dev_sec_info);

	return evmm_desc;
}

evmm_desc_t *boot_params_parse(multiboot_info_t *mbi)
{
	evmm_desc_t *evmm_desc = NULL;
	uint64_t top_of_mem = 0;

	evmm_desc = init_evmm_desc();
	if (!evmm_desc) {
		print_panic("Failed to init evmm desc!\n");
		return NULL;
	}

	top_of_mem = get_top_of_memory(mbi);
	if (top_of_mem == 0) {
		print_panic("Failed to get top_of memory from mbi!\n");
		return NULL;
	}

	evmm_desc->num_of_cpu = CPU_NUM;
	evmm_desc->tsc_per_ms = TSC_PER_MS;
	evmm_desc->top_of_mem = top_of_mem;

	return evmm_desc;
}

