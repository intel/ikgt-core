/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "ldr_dbg.h"
#include "device_sec_info.h"
#include "stage0_lib.h"
#include "lib/util.h"

#define TOS_MAX_IMAGE_SIZE    0x100000 /* Max image size assumed to be 1 MB */
#define BOOT_NULL     (0x00)
#define BOOT_CS       (0x08)
#define BOOT_DS       (0x10)

typedef struct {
	uint32_t Type;           // Field size is 32 bits followed by 32 bit pad
	uint32_t Pad;
	uint64_t PhysicalStart;  // Field size is 64 bits
	uint64_t VirtualStart;   // Field size is 64 bits
	uint64_t NumberOfPages;  // Field size is 64 bits
	uint64_t Attribute;      // Field size is 64 bits
} efi_mem_desc_t;

/* This function will set gcpu state to 32 bit environment
 * Caller should clear whole structure before calling this function
 */
void setup_32bit_env(gcpu_state_t *gcpu_state)
{
	gcpu_state->rflags = RFLAGS_RSVD1;

	/* 32-bit CS */
	fill_segment(&gcpu_state->segment[SEG_CS], 0, 0xffffffff, 0xc09b, BOOT_CS);

	/* 32-bit DS/ES/FS/GS/SS/TR */
	fill_segment(&gcpu_state->segment[SEG_DS], 0, 0xffffffff, 0xc093, BOOT_DS);
	fill_segment(&gcpu_state->segment[SEG_ES], 0, 0xffffffff, 0xc093, BOOT_DS);
	fill_segment(&gcpu_state->segment[SEG_FS], 0, 0xffffffff, 0xc093, BOOT_DS);
	fill_segment(&gcpu_state->segment[SEG_GS], 0, 0xffffffff, 0xc093, BOOT_DS);
	fill_segment(&gcpu_state->segment[SEG_SS], 0, 0xffffffff, 0xc093, BOOT_DS);

	/* Unused TR/LDTR */
	fill_segment(&gcpu_state->segment[SEG_TR], 0, 0xffffffff, 0x808b, BOOT_NULL);
	fill_segment(&gcpu_state->segment[SEG_LDTR], 0, 0, 0x10000, 0);

	gcpu_state->cr0 = CR0_ET|CR0_PE;
}

void make_dummy_trusty_info(void *info)
{
	device_sec_info_v0_t *device_sec_info = (device_sec_info_v0_t *)info;

	memset(device_sec_info, 0, sizeof(device_sec_info_v0_t));

	device_sec_info->size_of_this_struct = sizeof(device_sec_info_v0_t);
	device_sec_info->version = 0;
	device_sec_info->platform = 0;
	device_sec_info->num_seeds = 1;
}

static uint32_t get_file_size(file_offset_header_t *file_hdr, uint32_t file_index)
{
	if (file_hdr == NULL) {
		print_panic("file header is NULL\n");
		return 0;
	}

	if (file_index >= PACK_BIN_COUNT) {
		print_panic("file_index[%u] exceeds max[%u]\n", file_index, PACK_BIN_COUNT);
		return 0;
	}

	if (!file_hdr->file_size[file_index]) {
		print_panic("stage[%u] file size is zero\n", file_index);
		return 0;
	}

	return file_hdr->file_size[file_index];
}

static uint32_t get_file_offset(file_offset_header_t *file_hdr, uint32_t file_index)
{
	uint32_t i;
	uint32_t sum = 0;

	if (file_hdr == NULL) {
		print_panic("file header is NULL\n");
		return 0;
	}

	if (file_index >= PACK_BIN_COUNT) {
		print_panic("file_index[%u] exceeds max[%u]\n", file_index, PACK_BIN_COUNT);
		return 0;
	}

	for (i = 0; i < file_index; i++) {
		if (!file_hdr->file_size[i]) {
			print_panic("stage[%u] file size is zero\n", i);
			return 0;
		}
		sum += file_hdr->file_size[i];
	}

	return sum;
}

boolean_t get_file_params(uint64_t base, packed_file_t *packed_file)
{
	uint32_t i;
	file_offset_header_t *file_hdr;

	if (!packed_file) {
		print_panic("packed_file is NULL!\n");
		return FALSE;
	}

	file_hdr = get_file_offsets_header(base, TOS_MAX_IMAGE_SIZE);
	if (!file_hdr) {
		print_panic("file header is NULL\n");
		return FALSE;
	}

	for (i = 0; i < PACK_BIN_COUNT; i++) {
		packed_file[i].load_addr = base + get_file_offset(file_hdr, i);
		if (!packed_file[i].load_addr) {
			print_panic("failed to get file[%d] load addr\n", i);
			return FALSE;
		}

		packed_file[i].size = (uint64_t)get_file_size(file_hdr, i);
		if (!packed_file[i].size) {
			print_panic("failed to get file[%d] size\n", i);
			return FALSE;
		}
	}

	return TRUE;
}

boolean_t file_parse(evmm_desc_t *evmm_desc, uint64_t base, uint32_t offset, uint32_t size)
{
	file_offset_header_t *file_hdr;

	/* Find file offsets header */
	file_hdr = get_file_offsets_header(base + offset, size);
	if (file_hdr == NULL) {
		print_panic("failed to find file header\n");
		return FALSE;
	}

	if (file_hdr->file_size[STAGE1_BIN_INDEX]) {
		evmm_desc->stage1_file.loadtime_addr = base +
			file_hdr->file_size[STAGE0_BIN_INDEX];
		evmm_desc->stage1_file.loadtime_size = file_hdr->file_size[STAGE1_BIN_INDEX];
	} else {
		print_panic("stage1 file size is zero\n");
		return FALSE;
	}

	if (file_hdr->file_size[EVMM_BIN_INDEX]) {
		evmm_desc->evmm_file.loadtime_addr = evmm_desc->stage1_file.loadtime_addr +
			evmm_desc->stage1_file.loadtime_size;
		evmm_desc->evmm_file.loadtime_size = file_hdr->file_size[EVMM_BIN_INDEX];
	} else {
		print_panic("evmm file size is zero\n");
		return FALSE;
	}

#if defined (MODULE_TRUSTY_GUEST) && defined (PACK_LK)
	if (file_hdr->file_size[LK_BIN_INDEX]) {
		evmm_desc->trusty_desc.lk_file.loadtime_addr = evmm_desc->evmm_file.loadtime_addr +
			evmm_desc->evmm_file.loadtime_size;
		evmm_desc->trusty_desc.lk_file.loadtime_size = file_hdr->file_size[LK_BIN_INDEX];
	} else {
		print_panic("lk file size is zero\n");
		return FALSE;
	}
#endif

#if defined (MODULE_OPTEE_GUEST) && defined (PACK_OPTEE)
    if (file_hdr->file_size[OPTEE_BIN_INDEX]) {
		evmm_desc->optee_desc.optee_file.loadtime_addr = evmm_desc->evmm_file.loadtime_addr +
			evmm_desc->evmm_file.loadtime_size;
		evmm_desc->optee_desc.optee_file.loadtime_size = file_hdr->file_size[OPTEE_BIN_INDEX];
	} else {
		print_panic("op-tee file size is zero\n");
		return FALSE;
	}
#endif

	return TRUE;
}

uint64_t get_top_of_memory(multiboot_info_t *mbi)
{
	uint32_t mmap_len;
	uint64_t mmap_addr;
	uint32_t offs = 0;
	uint64_t tom = 0;
	multiboot_memory_map_t *mmap = NULL;

	if (!mbi) {
		print_panic("Multiboot info is NULL!\n");
		return 0;
	}

	/* get TOM from mmap in mubtiboot info */
	if (!(mbi->flags & MBI_MEMMAP)) {
		print_panic("Multiboot info does not contain mmap field!\n");
		return 0;
	}

	mmap_len = mbi->mmap_length;
	mmap_addr = mbi->mmap_addr;

	for (; offs < mmap_len; offs += (mmap->size + sizeof(mmap->size))) {
		mmap = (multiboot_memory_map_t *)(mmap_addr + offs);
		print_trace(" 0x%03x:[ base = 0x%016llx, length = 0x%016llx, type = 0x%x, size = %d ]\n",
			offs, mmap->addr, mmap->len, mmap->type, mmap->size);
		if (tom < (mmap->addr + mmap->len))
			tom = mmap->addr + mmap->len;
	}
	print_trace("top of memory = %llx\n", tom);

	return tom;
}

/* Get top of memory from efi memory map description */
uint64_t get_efi_tom(uint64_t mmap_addr, uint32_t mmap_size)
{
	uint64_t tom = 0;
	efi_mem_desc_t *mmap = NULL;
	uint32_t i;
	uint32_t nr_entry;

	if (mmap_addr == 0) {
		print_panic("mmap_addr is NULL\n");
		return 0;
	}

	mmap = (efi_mem_desc_t *)mmap_addr;
	nr_entry = mmap_size/sizeof(efi_mem_desc_t);

	for (i = 0; i < nr_entry; i++) {
		if (tom < (mmap->PhysicalStart + mmap->NumberOfPages * PAGE_4K_SIZE))
			tom = mmap->PhysicalStart + mmap->NumberOfPages * PAGE_4K_SIZE;
		mmap ++;
	}
	print_trace("top of memory = %llx\n", tom);

	return tom;
}
