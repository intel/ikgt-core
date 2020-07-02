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

	uint8_t seed[BUP_MKHI_BOOTLOADER_SEED_LEN];
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

static boolean_t hide_runtime_memory(multiboot_info_t *mbi,
		uint64_t hide_mem_addr, uint64_t hide_mem_size)
{
	uint32_t mmap_len;
	uint64_t mmap_addr;
	uint32_t offs = 0, index = 0, num_of_entries = 0, ori_size = 0;
	multiboot_memory_map_t *mmap = NULL;
	multiboot_memory_map_t *newmmap_addr = NULL;

	/* get mmap from mubtiboot info */
	if (!(mbi->flags & MBI_MEMMAP)) {
		print_panic("Multiboot info does not contain mmap field!\n");
		return FALSE;
	}

	mmap_addr = mbi->mmap_addr;
	mmap_len = mbi->mmap_length;

	num_of_entries = mmap_len/sizeof(multiboot_memory_map_t) + 2;
	newmmap_addr = (multiboot_memory_map_t *)allocate_memory(
				        sizeof(multiboot_memory_map_t) * num_of_entries);
	if (newmmap_addr == NULL) {
		print_panic("allocate new e820 mem failed!\n");
		return FALSE;
	}

	for (; offs < mmap_len; index++, offs += (ori_size + sizeof(mmap->size))) {
		mmap = (multiboot_memory_map_t *)(mmap_addr + offs);

		ori_size = mmap->size;
		mmap->size = sizeof(multiboot_memory_map_t) - sizeof(mmap->size);

		if (((mmap->addr + mmap->len) <= hide_mem_addr) ||
			((hide_mem_addr + hide_mem_size) <= mmap->addr)) {
			/*do not modify it*/
			memcpy(&newmmap_addr[index], mmap, sizeof(multiboot_memory_map_t));
		} else {
			if ((hide_mem_addr + hide_mem_size) > (mmap->addr + mmap->len)) {
				print_panic("the hide memory crosses two e820 entries!\n");
				return FALSE;
			}

			if (mmap->type != MULTIBOOT_MEMORY_AVAILABLE) {
				print_panic("the hide memory arrange type is not AVAILABLE in e820 table!\n");
				return FALSE;
			}

			if (hide_mem_addr > mmap->addr) {
				newmmap_addr[index].size = mmap->size;
				newmmap_addr[index].addr = mmap_addr;
				newmmap_addr[index].len = hide_mem_addr - mmap->addr;
				newmmap_addr[index].type = mmap->type;
				index++;
			}

			newmmap_addr[index].size = mmap->size;
			newmmap_addr[index].addr = hide_mem_addr;
			newmmap_addr[index].len = hide_mem_size;
			newmmap_addr[index].type = MULTIBOOT_MEMORY_RESERVED;

			if ((mmap->addr + mmap->len) > (hide_mem_addr + hide_mem_size)) {
				index++;
				newmmap_addr[index].size = mmap->size;
				newmmap_addr[index].addr = hide_mem_addr + hide_mem_size;
				newmmap_addr[index].len = (mmap->addr + mmap->len) - (hide_mem_addr + hide_mem_size);
				newmmap_addr[index].type = mmap->type;
			}
		}
	}

	mbi->mmap_addr = (uint64_t)newmmap_addr;
	mbi->mmap_length = sizeof(multiboot_memory_map_t) * index;

	return TRUE;
}

static evmm_desc_t *init_evmm_desc(void)
{
	evmm_desc_t *evmm_desc = NULL;
	void *evmm_runtime_mem = NULL;
	void *lk_runtime_mem = NULL;
	memory_layout_t *loader_mem = NULL;

	/*allocate memory for every block*/
	evmm_runtime_mem = allocate_memory(EVMM_RUNTIME_SIZE);
	if (evmm_runtime_mem == NULL) {
		print_panic("allocate evmm runtime mem failed!\n");
		return NULL;
	}

	lk_runtime_mem = allocate_memory(LK_RUNTIME_SIZE);
	if (lk_runtime_mem == NULL) {
		print_panic("allocate lk runtime mem failed!\n");
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

	/*fill trusty boot params*/
#ifdef MODULE_TRUSTY_GUEST
	evmm_desc->trusty_desc.lk_file.runtime_addr = (uint64_t)lk_runtime_mem;
	evmm_desc->trusty_desc.lk_file.runtime_total_size = LK_RUNTIME_SIZE;
#endif

#ifdef MODULE_OPTEE_GUEST
	evmm_desc->optee_desc.optee_file.runtime_addr = (uint64_t)lk_runtime_mem;
	evmm_desc->optee_desc.optee_file.runtime_total_size = LK_RUNTIME_SIZE;
#endif

	return evmm_desc;
}

evmm_desc_t *boot_params_parse(multiboot_info_t *mbi)
{
	evmm_desc_t *evmm_desc = NULL;
	uint64_t tom = 0;
	boolean_t ret = FALSE;

	evmm_desc = init_evmm_desc();
	if (!evmm_desc) {
		print_panic("Failed to init evmm desc!\n");
		return NULL;
	}

	ret = hide_runtime_memory(mbi, RUNTIME_MEMORY_ADDR, RUNTIME_MEMORY_SIZE);
	if (!ret) {
		print_panic("hide runtime memory failed!\n");
		return NULL;
	}

	tom = get_top_of_memory(mbi);
	if (tom == 0) {
		print_panic("Failed to get top_of memory from mbi!\n");
		return NULL;
	}

	evmm_desc->num_of_cpu = CPU_NUM;
	evmm_desc->tsc_per_ms = TSC_PER_MS;
	evmm_desc->top_of_mem = tom;

	return evmm_desc;
}

