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

static void fill_code32_seg(segment_t *ss, uint16_t sel)
{
	ss->base = 0;
	ss->limit = 0xffffffff;
	ss->attributes = 0xc09b;
	ss->selector = sel;
}

static void fill_code64_seg(segment_t *ss, uint16_t sel)
{
	ss->base = 0;
	ss->limit = 0xffffffff;
	ss->attributes = 0xa09b;
	ss->selector = sel;
}

static void fill_data_seg(segment_t *ss, uint16_t sel)
{
	ss->base = 0;
	ss->limit = 0xffffffff;
	ss->attributes = 0xc093;
	ss->selector = sel;
}

static void fill_tss_seg(segment_t *ss, uint16_t sel)
{
	ss->base = 0;
	ss->limit = 0xffffffff;
	ss->attributes = 0x808b;
	/* it is ok for TR to be NULL while the attribute is not 0.
	 * vmentry will not check it and guest OS will set the correct TR later */
	ss->selector = sel;
}

static void fill_unused_seg(segment_t *ss)
{
	ss->base = 0;
	ss->limit = 0;
	ss->attributes = 0x10000;
	ss->selector = 0;
}

void save_current_cpu_state(gcpu_state_t *s)
{
	asm_sgdt(&(s->gdtr));
	asm_sidt(&(s->idtr));
	s->cr0 = asm_get_cr0();
	s->cr3 = asm_get_cr3();
	s->cr4 = asm_get_cr4();

	s->msr_efer = asm_rdmsr(MSR_EFER);

	/* The selector of LDTR in current environment is invalid which indicates
	 * the bootloader is not using LDTR. So set LDTR unusable here. In
	 * future, exception might occur if LDTR is used in bootloader. Then bootloader
	 * will find us since we changed LDTR to 0, and we can fix it for that bootloader. */
	fill_unused_seg(&s->segment[SEG_LDTR]);
	/* TSS is used for RING switch, which is usually not used in bootloader since
	 * bootloader always runs in RING0. So we hardcode TR here. In future, #TS
	 * might occur if TSS is used bootloader. Then bootlaoder will find us since we
	 * changed TR to 0, and we can fix it for that bootlaoder. */
	fill_tss_seg(&s->segment[SEG_TR], 0);
	/* For segments: get selector from current environment, selector of ES/FS/GS are from DS,
	 * hardcode other fields to make guest launch successful. */
	fill_code64_seg(&s->segment[SEG_CS], asm_get_cs());
	fill_data_seg(&s->segment[SEG_DS], asm_get_ds());
	fill_data_seg(&s->segment[SEG_ES], asm_get_ds());
	fill_data_seg(&s->segment[SEG_FS], asm_get_ds());
	fill_data_seg(&s->segment[SEG_GS], asm_get_ds());
	fill_data_seg(&s->segment[SEG_SS], asm_get_ss());
}

/* This function will set gcpu state to 32 bit environment
 * Caller should clear whole structure before calling this function
 */
void setup_32bit_env(gcpu_state_t *gcpu_state)
{
	gcpu_state->rflags = RFLAGS_RSVD1;

	fill_code32_seg(&gcpu_state->segment[SEG_CS], BOOT_CS);
	fill_data_seg(&gcpu_state->segment[SEG_DS], BOOT_DS);
	fill_data_seg(&gcpu_state->segment[SEG_ES], BOOT_DS);
	fill_data_seg(&gcpu_state->segment[SEG_FS], BOOT_DS);
	fill_data_seg(&gcpu_state->segment[SEG_GS], BOOT_DS);
	fill_data_seg(&gcpu_state->segment[SEG_SS], BOOT_DS);
	fill_tss_seg(&gcpu_state->segment[SEG_TR], BOOT_NULL);
	gcpu_state->segment[SEG_LDTR].attributes = 0x010000;

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
