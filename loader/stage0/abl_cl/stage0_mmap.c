/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "abl_boot_param.h"
#include "stage0_lib.h"
#include "ldr_dbg.h"
#include "linux_loader.h"

#include "lib/util.h"
#include "lib/string.h"

#define SIPI_AP_WKUP_ADDR           0x59000

typedef struct _stage0_mmap {
	uint64_t addr;
	uint64_t len;
	uint32_t type; // 1~5 are used. We can add more for "used by evmm tmporarily"
	uint32_t pad;
	struct _stage0_mmap *next;
} stage0_mmap_t;

static stage0_mmap_t stage0_mmap[128];
static uint32_t tail = 0;

/* This global variable is used to mark AVAILABLE type memory regions need protect temporarily.
  *These memory regions will be restore to AVAILALBE tpye after stage0 work done
 */
static uint32_t g_tmp_type;

boolean_t init_stage0_mmap(multiboot_info_t *mbi, uint32_t *tmp_type)
{
	uint32_t i = 0, max_type = 0;
	multiboot_memory_map_t *mmap = NULL;

	if(!mbi)
		return FALSE;

	if (!CHECK_FLAG(mbi->flags, 6)) {
		print_panic("mmap info is not valid in mbi!\n");
		return FALSE;
	}

	if (mbi->mmap_length == 0) {
		print_panic("mmap length is 0!\n");
		return FALSE;
	}

	memset(stage0_mmap, 0, sizeof(stage0_mmap));
	mmap = (multiboot_memory_map_t *)(uint64_t)mbi->mmap_addr;

	while ((uint64_t)mmap < mbi->mmap_addr + mbi->mmap_length) {
		stage0_mmap[i].addr = mmap->addr;
		stage0_mmap[i].len = mmap->len;
		stage0_mmap[i].type = mmap->type;
		if (i != 0)
			stage0_mmap[i - 1].next = (stage0_mmap_t *)&stage0_mmap[i];
		if (mmap->type > max_type) {
			max_type = mmap->type;
			if (max_type == 0xFFFFFFFF) {
				print_info("Type value is 0xFFFFFFFF\n");
				return FALSE;
			}
		}
		i++;
		mmap = (multiboot_memory_map_t *)((uint64_t)mmap + mmap->size + sizeof(mmap->size));
	}

	*tmp_type = max_type + 1;
	tail = i - 1;
	g_tmp_type = *tmp_type;

	return TRUE;
}

static stage0_mmap_t *split_stage0_mmap(stage0_mmap_t *mmap, uint64_t new_len, uint32_t new_type)
{
	stage0_mmap_t *new_entry;

	tail++;
	if (tail >= 128) {
		print_info("index of mmap exceed max(128)!\n");
		return NULL;
	}

	new_entry = &stage0_mmap[tail];
	new_entry->addr = mmap->addr + mmap->len - new_len;
	new_entry->len = new_len;
	new_entry->type = new_type;
	new_entry->next = mmap->next;

	mmap->len -= new_len;
	mmap->next = new_entry;

	return new_entry;
}

boolean_t insert_stage0_mmap(uint64_t base, uint64_t len, uint32_t type)
{
	stage0_mmap_t *mmap = &stage0_mmap[0];

	if (len == 0) {
		return TRUE;
	}

	if (type == MULTIBOOT_MEMORY_AVAILABLE) {
		return FALSE;
	}

	while (mmap) {
		if ((base < mmap->addr)
			|| (base + len > mmap->addr + mmap->len)
			|| (mmap->type != MULTIBOOT_MEMORY_AVAILABLE)) {
			mmap = mmap->next;
			continue;
		}

		if (len == mmap->len) { /* new region match the whole mmap */
			mmap->type = type;
			break;
		} else if (base == mmap->addr) { /* new region is the top half */
			mmap->type = type;
			mmap = split_stage0_mmap(mmap, mmap->len - len, MULTIBOOT_MEMORY_AVAILABLE);
			if (!mmap)
				return FALSE;
			break;
		} else if ((base + len) == (mmap->addr + mmap->len)) { /* new region is the bottom half */
			mmap = split_stage0_mmap(mmap, len, type);
			if (!mmap)
				return FALSE;
			break;
		} else { /* new region is the the middle */
			mmap = split_stage0_mmap(mmap, mmap->len - (base - mmap->addr), type);
			if (!mmap)
				return FALSE;

			mmap = split_stage0_mmap(mmap, mmap->len - len, MULTIBOOT_MEMORY_AVAILABLE);
			if (!mmap)
				return FALSE;
			break;
		}
		mmap = mmap->next;
	}

	if (!mmap) {
		return FALSE;
	}

	return TRUE;
}

boolean_t get_max_stage0_mmap(uint64_t *base, uint64_t *len)
{
	uint64_t max_len = 0;
	stage0_mmap_t *mmap_fnd = NULL;
	stage0_mmap_t *mmap = &stage0_mmap[0];

	while (mmap) {
		if ((mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
		    && (mmap->len > max_len)
		    /* The address should below 4G due to linux requriement from boot_params */
		    && (mmap->addr + mmap->len) <= 4 GIGABYTE) {
			max_len = mmap->len;
			mmap_fnd = mmap;
		}
		mmap = mmap->next;
	}

	if (mmap_fnd) {
		*base = mmap_fnd->addr;
		*len = mmap_fnd->len;
		return TRUE;
	}

	return FALSE;
}

/*
 * Get an available sipi page below 1M. Many addresses below 1M are used by
 * Linux kernel without mark them reserved in mmap. It is difficult to list
 * all such addresses. Here, for now, we just hard-code a safe address
 */
uint64_t get_sipi_page(void)
{
	uint64_t base = SIPI_AP_WKUP_ADDR;
	boolean_t ret;

	if ((base == 0) || (base > 1 MEGABYTE - PAGE_4K_SIZE))
		return 0;

	ret = insert_stage0_mmap(base, PAGE_4K_SIZE, MULTIBOOT_MEMORY_RESERVED);
	if (!ret) {
		print_panic("Failed to alloc sipi page, base = %llx!\n", base);
		return 0;
	}

	return base;
}

inline static void set_e820_entry(e820entry_t *e820_map, stage0_mmap_t *mmap)
{
	e820_map->addr = mmap->addr;
	e820_map->size = mmap->len;
	if (mmap->type == g_tmp_type) {
		e820_map->type = MULTIBOOT_MEMORY_AVAILABLE;
	} else {
		e820_map->type = mmap->type;
	}
}

boolean_t stage0_mmap_to_e820(boot_params_t *bp)
{
	uint32_t i = 0;
	stage0_mmap_t *mmap = &stage0_mmap[0];

	if (!bp) {
		print_panic("Input boot params is NULL!\n");
		return FALSE;
	}

	while (mmap) {
		if (i == 0) {
			set_e820_entry(&bp->e820_map[i], mmap);
			i++;
		} else if ((mmap->type == g_tmp_type) &&
				(bp->e820_map[i-1].type == MULTIBOOT_MEMORY_AVAILABLE) &&
				(bp->e820_map[i-1].addr + bp->e820_map[i-1].size == mmap->addr)) {
			bp->e820_map[i-1].size += mmap->len;
		} else {
			set_e820_entry(&bp->e820_map[i], mmap);
			i++;
		}
		mmap = mmap->next;
	}
	bp->e820_entries = i;

	return TRUE;
}
