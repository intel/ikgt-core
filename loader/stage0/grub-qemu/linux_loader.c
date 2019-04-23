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
#include "grub_boot_param.h"
#include "linux_loader.h"
#include "ldr_dbg.h"
#include "lib/string.h"
#include "lib/util.h"

static multiboot_module_t *loader_get_module(multiboot_info_t *mbi,
							grub_module_index_t midx)
{
	multiboot_module_t *mod = NULL;

	if  ((!(mbi->flags & MBI_MODULES)) ||
		 (0 == mbi->mods_count)        ||
		 (midx > mbi->mods_count)) {
		print_panic("Linux loader: failed to load modules!\n");
		return NULL;
	}

	mod = (multiboot_module_t *)((uint64_t)mbi->mods_addr);

	return &mod[midx];
}

static char *loader_get_kernel_command_line(multiboot_info_t *mbi)
{
#ifdef MODULE_TRUSTY_GUEST
	char *kernel_command_line = (char *)(long)mbi->cmdline;
	char kernel_filename[] = "evmm_pkg.bin ";
	int  len;
	int  ret;

	/*
	 * When we passing evmm_pkg.bin as kernel file name,
	 *  -kenrel evmm_pkg.bin -initrd lk.bin,bzImage -append 'KERNEL_CMD_LINE'
	 * kernel commond line would be "evmm_pkg.bin "+"KERNEL_CMD_LINE", string
	 * "evmm_pkg.bin " (pay attention to space in this string) should be
	 * filtered out of kernel command line.
	 *
	 * This is the way how QEMU pass multiboot command line, more details
	 * about how QEMU construct kernel command line please refer function:
	 * 	load_multiboot()
	 * in file:
	 * 	$QEMU_SOURCE_CODE/hw/i386/multiboot.c
	 */
	len = strnlen_s(kernel_filename, MAX_LINUX_CMDLINE_LEN);
	ret = memcmp(kernel_command_line, kernel_filename, len);
	if (0 != ret) {
		print_panic("Linux loader: kernel file name unexpected!\n");
		return NULL;
	}

	return kernel_command_line + len;
#else
	return kernel_command_line;
#endif
}

static boolean_t loader_setup_boot_params(multiboot_info_t *mbi,
					boot_params_t *boot_params,
					linux_kernel_header_t *header)
{
	uint32_t i;

	/* copy the whole setup data from image header to boot parameter */
	memcpy(&boot_params->setup_hdr, &header->setup_hdr, sizeof(setup_header_t));

	/* detect e820 table, and update e820_map[] in boot parameters */
	if (mbi->flags & MBI_MEMMAP) {
		multiboot_memory_map_t *mmap =
			(multiboot_memory_map_t *)(uint64_t)(mbi->mmap_addr);

		/* get e820 entries from mbi info */
		for (i = 0; i < mbi->mmap_length / sizeof(multiboot_memory_map_t);
			 i++) {
			boot_params->e820_map[i].addr = mmap[i].addr;
			boot_params->e820_map[i].size = mmap[i].len;

			if (mmap[i].type == MULTIBOOT_MEMORY_BAD) {
				boot_params->e820_map[i].type = MULTIBOOT_MEMORY_RESERVED;
			} else {
				boot_params->e820_map[i].type = mmap[i].type;
			}
		}

		boot_params->e820_entries = i;
	} else {
		print_panic("Linux loader: no memory map info in multiboot info\n");
		return FALSE;
	}

	return TRUE;
}

void reserve_region_from_mmap(boot_params_t *boot_params,
		uint64_t start,
		uint64_t size)
{
	e820entry_t *mmap = boot_params->e820_map;
	e820entry_t mmap_bak[E820MAX];
	uint32_t org = 0;
	uint32_t new = 0;
	uint64_t end = start + size;

	for (org = 0; org < boot_params->e820_entries; org++) {
		mmap_bak[org].addr = mmap[org].addr;
		mmap_bak[org].size = mmap[org].size;
		mmap_bak[org].type = mmap[org].type;
	}

	/*
	 * Assumption:
	 * Trusty and evmm always reside in available memory region, so we can skip
	 * type check.
	 */
	for (org = 0; org < boot_params->e820_entries; org++) {
		if ((mmap_bak[org].addr <= start) &&
			((mmap_bak[org].addr + mmap_bak[org].size) > start)) {

			mmap[new].addr = mmap_bak[org].addr;

			if (mmap[new].addr == start) {
				mmap[new].type = MULTIBOOT_MEMORY_RESERVED;
				mmap[new].size = size;

			} else {
				mmap[new].size = start - mmap[new].addr;

				/* Next entry should be reserved */
				new++;
				mmap[new].addr = start;
				mmap[new].size = size;
				mmap[new].type = MULTIBOOT_MEMORY_RESERVED;
			}

			while (org < boot_params->e820_entries)  {
				if ((mmap_bak[org].addr <= end) &&
					(mmap_bak[org].addr + mmap_bak[org].size > end)) {

					break;
				}
				org++;
			}

			/* When we reach here, either find an index, or at the end of E820 table */
			if (org == boot_params->e820_entries) {
				mmap[new].size = mmap_bak[org-1].addr + mmap_bak[org-1].size - start;
				boot_params->e820_entries = new;
				return;
			} else {
				if (mmap_bak[org].type == MULTIBOOT_MEMORY_RESERVED) {
					mmap[new].size = mmap_bak[org].addr + mmap_bak[org].size - start;
				} else {
					new++;
					mmap[new].addr = end;
				mmap[new].size = mmap_bak[org].addr + mmap_bak[org].size - end;
				mmap[new].type = MULTIBOOT_MEMORY_AVAILABLE;
				}
			}
		} else {
			mmap[new].addr = mmap_bak[org].addr;
			mmap[new].size = mmap_bak[org].size;
		}
		new++;
	}

	boot_params->e820_entries = new;
}

static linux_kernel_header_t *check_linux_header(void *image,
								uint32_t size,
								uint64_t *boot_param,
								uint64_t *entry)
{
	linux_kernel_header_t *header = NULL;

	if ((NULL == image)      ||
	    (0 == size)          ||
	    (NULL == boot_param) ||
		(NULL == entry)      ||
		(size < sizeof(linux_kernel_header_t))) {
		print_panic("Linux loader: prarams check failure!\n");
		return NULL;
	}

	header = (linux_kernel_header_t *)image;

	if ((header->setup_hdr.header != HDRS_MAGIC)          ||
	    (!(header->setup_hdr.loadflags & FLAG_LOAD_HIGH)) ||
	    ((header->setup_hdr.version) < 0x020a)) {
		print_info("Linux loader: Linux header check failure!\n");
		return NULL;
	}

	return header;
}

static boolean_t prepare_linux_header(multiboot_info_t *mbi,
					boot_params_t **boot_params,
					linux_kernel_header_t *header)
{
	const char *kernel_cmdline = NULL;
	unsigned long real_mode_size = 0;
	uint64_t protected_mode_base = 0;
	boot_params_t *boot_para;
	uint32_t cmdline_len;

	/* allocate "boot_params+cmdline" from heap space*/
	boot_para = (boot_params_t *)allocate_memory(
		sizeof(boot_params_t) + header->setup_hdr.cmdline_size);

	if (NULL == boot_para) {
		print_panic("Linux loader: failed to allocate memory for parameters!\n");
		return FALSE;
	}

	/* put cmd_line_ptr after boot_parameters */
	header->setup_hdr.cmd_line_ptr = (uint64_t)(boot_para) + sizeof(boot_params_t);

	/* get cmdline param */
	kernel_cmdline = loader_get_kernel_command_line(mbi);

	cmdline_len = strnlen_s(kernel_cmdline, MAX_LINUX_CMDLINE_LEN);

	/* check max cmdline_size */
	if (cmdline_len > header->setup_hdr.cmdline_size) {
		print_panic("Linux loader: cmdline size exceeds max value!\n");
		return FALSE;
	}

	/* copy cmdline to boot parameter */
	memcpy((void *)(uint64_t)header->setup_hdr.cmd_line_ptr,
			kernel_cmdline,
			cmdline_len);

	real_mode_size = (header->setup_hdr.setup_sects + 1) * SECTOR_SIZE;

	if (header->setup_hdr.relocatable_kernel) {
		/*
		 * A relocatable kernel that is loaded at an alignment
		 * incompatible value will be realigned during kernel
		 * initialization.
		 */
		protected_mode_base = (uint64_t)header + real_mode_size;
	} else {
		/*
		 * If need to support older kernel, need to move
		 * kernel to pref_address.
		 */
		print_panic("Linux loader: old kernel not relocatable!\n");
		return FALSE;
	}

	header->setup_hdr.code32_start = protected_mode_base;

	/* Ramdisk is obsoleted now */
	header->setup_hdr.ramdisk_image = 0;
	header->setup_hdr.ramdisk_size = 0;

	/* boot loader is grub2, so set type_of_loader to 0x72 */
	header->setup_hdr.type_of_loader = GRUB_LINUX_BOOT_LOADER_TYPE;

	/* clear loadflags and heap_end_ptr */
	header->setup_hdr.loadflags &= ~FLAG_CAN_USE_HEAP;

	/* set vid_mode -- hardcode as normal mode */
	header->setup_hdr.vid_mode = GRUB_LINUX_VID_MODE_NORMAL;

	if (header->setup_hdr.setup_sects == 0) {
		header->setup_hdr.setup_sects = DEFAULT_SECTOR_NUM;
	}

	*boot_params = boot_para;
	return TRUE;
}

static boolean_t expand_linux_image(multiboot_info_t *mbi,
					void *linux_image, uint32_t linux_size,
					uint64_t *boot_param_addr, uint64_t *entry_point)
{
	linux_kernel_header_t *header = NULL;
	boot_params_t *boot_params = NULL;

	header = check_linux_header(linux_image,
				linux_size,
				boot_param_addr,
				entry_point);

	if (NULL == header) {
		print_info("Linux loader: invalid kernel image!\n");
		return FALSE;
	}

	if(!prepare_linux_header(mbi, &boot_params, header)) {
		print_panic("Linux loader: failed to prepare linux header!\n");
		return FALSE;
	}

	/* setup boot parameters according to linux boot protocol */
	if (!loader_setup_boot_params(mbi, boot_params, header)) {
		print_panic("Linux loader: failed to configure linux boot parames!\n");
		return FALSE;
	}

	/* get 64 bit entry point */
	*entry_point = (uint64_t)(boot_params->setup_hdr.code32_start + 0x200);

	/* get boot params address */
	*boot_param_addr = (uint64_t)boot_params;

	return TRUE;
}

boolean_t linux_kernel_parse(multiboot_info_t *mbi,
			uint64_t *boot_param_addr,
			uint64_t *entry_point)
{
	void *kernel_image = NULL;
	uint32_t kernel_size = 0;
	multiboot_module_t *module = NULL;

	if (NULL == mbi) {
		print_panic("Linux loader: invalid multiboot info!\n");
		return FALSE;
	}

	module = loader_get_module(mbi, LINUXIMG);
	if (NULL == module) {
		print_panic("Linux loader: failed to get kernel module!\n");
		return FALSE;
	}

	kernel_image = (void *)((uint64_t)module->mod_start);
	kernel_size = module->mod_end - module->mod_start;

	/*
	 * Move Linux kernel to safe place:
	 * When Trusty LK binary and Linux kernel are treated as multiboot modules,
	 * Linux kernel image resides right after Trusty LK binary, which means
	 * Linux kernel image would be overwritten in Trusty LK runtime.
	 * To Keep Linux kernel image safe, move it out of Trusty LK memory region.
	 */
	memcpy((void *)KERNEL_RUNTIME_ADDRESS, kernel_image, kernel_size);
	kernel_image = (void *)KERNEL_RUNTIME_ADDRESS;

	if (!expand_linux_image(mbi, kernel_image, kernel_size,
			boot_param_addr, entry_point)) {
		print_info("Linux loader: failed to expand linux image!\n");
		return FALSE;
	}

	return TRUE;
}
