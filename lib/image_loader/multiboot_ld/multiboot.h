/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _MULTIBOOT_H_
#define _MULTIBOOT_H_

#define MULTIBOOT_MEMORY_AVAILABLE  1
#define MULTIBOOT_MEMORY_RESERVED   2
#define MULTIBOOT_MEMORY_RESERVED   2
#define MULTIBOOT_MEMORY_BAD        5

#define MULTIBOOT_HEADER_MAGIC  0x1BADB002
#define MULTIBOOT_HEADER_SIZE   32

/* bit definitions of flags field of multiboot information */
#define MBI_MEMLIMITS   (1 << 0)
#define MBI_BOOTDEV     (1 << 1)
#define MBI_CMDLINE     (1 << 2)
#define MBI_MODULES     (1 << 3)
#define MBI_AOUT        (1 << 4)
#define MBI_ELF         (1 << 5)
#define MBI_MEMMAP      (1 << 6)
#define MBI_DRIVES      (1 << 7)
#define MBI_CONFIG      (1 << 8)
#define MBI_BTLDNAME    (1 << 9)
#define MBI_APM         (1 << 10)
#define MBI_VBE         (1 << 11)

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
	uint32_t magic;
	uint32_t flags;
	uint32_t checksum;
	uint32_t header_addr;
	uint32_t load_addr;
	uint32_t load_end_addr;
	uint32_t bss_end_addr;
	uint32_t entry_addr;
} multiboot_header_t;

typedef struct {
	uint32_t mod_start;
	uint32_t mod_end;
	uint32_t cmdline;
	uint32_t pad;
} multiboot_module_t;

typedef enum {
	MFIRST_MODULE = 0,
	TRUSTYIMG = MFIRST_MODULE,
	TESTRUNNER,
	GRUB_MODULE_COUNT
} grub_module_index_t;

#endif
