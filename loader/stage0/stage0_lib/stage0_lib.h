/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _STAGE0_LIB_H_
#define _STAGE0_LIB_H_
#include "evmm_desc.h"
#include "vmm_arch.h"
#include "file_pack.h"

#define STAGE1_RUNTIME_SIZE   0xC000

#define MULTIBOOT_MEMORY_AVAILABLE  1
#define MULTIBOOT_MEMORY_RESERVED   2
#define MULTIBOOT_MEMORY_BAD        5

/* bit definitions of flags field of multiboot information */
#define MBI_MEMLIMITS    (1 << 0)
#define MBI_BOOTDEV      (1 << 1)
#define MBI_CMDLINE      (1 << 2)
#define MBI_MODULES      (1 << 3)
#define MBI_AOUT         (1 << 4)
#define MBI_ELF          (1 << 5)
#define MBI_MEMMAP       (1 << 6)
#define MBI_DRIVES       (1 << 7)
#define MBI_CONFIG       (1 << 8)
#define MBI_BTLDNAME     (1 << 9)
#define MBI_APM          (1 << 10)
#define MBI_VBE          (1 << 11)

typedef struct {
	uint32_t tabsize;
	uint32_t strsize;
	uint32_t addr;
	uint32_t reserved;
} aout_t; /* a.out kernel image */

typedef struct {
	uint32_t num;
	uint32_t size;
	uint32_t addr;
	uint32_t shndx;
} elf_t; /* elf kernel */

/*only used partial of the standard multiboot_info_t, since we only need flags and cmdline */
typedef struct {
	uint32_t flags;

	/* valid if flags[0] (MBI_MEMLIMITS) set */
	uint32_t mem_lower;
	uint32_t mem_upper;

	/* valid if flags[1] set */
	uint32_t boot_device;

	/* valid if flags[2] (MBI_CMDLINE) set */
	uint32_t cmdline;

	/* valid if flags[3] (MBI_MODS) set */
	uint32_t mods_count;
	uint32_t mods_addr;

	/* valid if flags[4] or flags[5] set */
	union {
		aout_t aout_image;
		elf_t elf_image;
	} syms;

	/* valid if flags[6] (MBI_MEMMAP) set */
	uint32_t mmap_length;
	uint32_t mmap_addr;
} multiboot_info_t;

typedef struct {
	uint32_t mod_start;
	uint32_t mod_end;
	uint32_t cmdline;
	uint32_t pad;
} multiboot_module_t;

typedef struct {
	uint32_t size;
	uint64_t addr;
	uint64_t len;
	uint32_t type;
} PACKED multiboot_memory_map_t;

typedef struct {
	uint64_t load_addr;
	uint64_t size;
} packed_file_t;

void setup_32bit_env(gcpu_state_t *gcpu_state);
void make_dummy_trusty_info(void *info);
boolean_t get_file_params(uint64_t base, packed_file_t *packed_file);
boolean_t file_parse(evmm_desc_t *evmm_desc, uint64_t base, uint32_t offset, uint32_t size);
uint64_t get_top_of_memory(multiboot_info_t *mbi);
uint64_t get_efi_tom(uint64_t mmap_addr, uint32_t mmap_size);

#endif
