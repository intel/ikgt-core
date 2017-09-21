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
#include "grub_boot_param.h"
#include "linux_loader.h"
#include "ldr_dbg.h"

#include "lib/util.h"

#define SECTOR_SIZE                 (1 << 9)    /* 0x200 = 512B */

#define DEFAULT_SECTOR_NUM          4           /* default sector number 4 */
#define MAX_SECTOR_NUM              64          /* max sector number 64 */
#define HDRS_MAGIC                  0x53726448
#define GRUB_LINUX_BOOT_LOADER_TYPE 0x72
#define FLAG_LOAD_HIGH              0x01
#define FLAG_CAN_USE_HEAP           0x80
#define GRUB_LINUX_VID_MODE_NORMAL  0xFFFF
#define E820MAX                     128

typedef struct {
	uint64_t addr;                                  /* start of memory segment */
	uint64_t size;                                  /* size of memory segment */
	uint32_t type;                                  /* type of memory segment */
} __attribute__ ((packed)) e820entry_t;

typedef struct {
	uint32_t efi_loader_signature;
	uint32_t efi_systab;
	uint32_t efi_memdesc_size;
	uint32_t efi_memdesc_version;
	uint32_t efi_memmap;
	uint32_t efi_memmap_size;
	uint32_t efi_systab_hi;
	uint32_t efi_memmap_hi;
} efi_info_t;

/* setup header according to the linux boot protocol */
typedef struct {
	uint8_t setup_sects;                    /* The size of the setup in sectors */

	uint16_t root_flags;                    /* If set, the root is mounted readonly */
	uint32_t syssize;                       /* The size of the 32-bit code in 16-byte paras */
	uint16_t ram_size;                      /* DO NOT USE - for bootsect.S use only */
	uint16_t vid_mode;                      /* Video mode control */
	uint16_t root_dev;                      /* Default root device number */
	uint16_t boot_flag;                     /* 0xAA55 magic number */
	uint16_t jump;                          /* Jump instruction */

	uint32_t header;                        /* Magic signature "HdrS" */

	uint16_t version;                       /* Boot protocol version supported */
	uint32_t realmode_swtch;                /* Boot loader hook */
	uint16_t start_sys;                     /* The load-low segment (0x1000) (obsolete) */
	uint16_t kernel_version;                /* Points to kernel version string */

	uint8_t type_of_loader;                 /* Boot loader identifier */

	uint8_t loadflags;                      /* Boot protocol option flags */

	uint16_t setup_move_size;               /* Move to high memory size (used with hooks) */
	uint32_t code32_start;                  /* Boot loader hook */
	uint32_t ramdisk_image;                 /* initrd load address (set by boot loader) */
	uint32_t ramdisk_size;                  /* initrd size (set by boot loader) */
	uint32_t bootsect_kludge;               /* DO NOT USE - for bootsect.S use only */
	uint16_t heap_end_ptr;                  /* Free memory after setup end */
	uint16_t pad1;                          /* Unused */
	uint32_t cmd_line_ptr;                  /* 32-bit pointer to the kernel command line */
	uint32_t initrd_addr_max;               /* Highest legal initrd address */
	uint32_t kernel_alignment;              /* Physical addr alignment required for kernel */
	uint8_t relocatable_kernel;             /* Whether kernel is relocatable or not */
	uint8_t min_alignment;
	uint16_t xloadflags;                    /* 2.12+ has this flag */
	uint32_t cmdline_size;                  /* Maximum size of the kernel command line */
	uint32_t hardware_subarch;              /* Hardware subarchitecture */
	uint64_t hardware_subarch_data;         /* Subarchitecture-specific data */
	uint32_t payload_offset;
	uint32_t payload_length;
	uint64_t setup_data;
	uint64_t pref_address;
	uint32_t init_size;
} __attribute__((packed)) setup_header_t;


typedef struct {
	uint8_t code1[0x0020];
	uint16_t cl_magic;                              /* Magic number 0xA33F */
	uint16_t cl_offset;                             /* The offset of command line */
	uint8_t code2[0x01F1 - 0x0020 - 2 - 2];

	setup_header_t setup_hdr;
} linux_kernel_header_t;

/* boot params structure according to the linux boot protocol */
typedef struct  {
	uint8_t screen_info[0x040 - 0x000];                     /* 0x000 */
	uint8_t apm_bios_info[0x054 - 0x040];                   /* 0x040 */
	uint8_t _pad2[4];                                       /* 0x054 */
	uint8_t tboot_shared_addr[8];                           /* 0x058 */
	uint8_t ist_info[0x070 - 0x060];                        /* 0x060 */
	uint8_t _pad3[16];                                      /* 0x070 */
	uint8_t hd0_info[16];                                   /* obsolete! */         /* 0x080 */
	uint8_t hd1_info[16];                                   /* obsolete! */         /* 0x090 */
	uint8_t sys_desc_table[0x0b0 - 0x0a0];                  /* 0x0a0 */
	uint8_t _pad4[144];                                     /* 0x0b0 */
	uint8_t edid_info[0x1c0 - 0x140];                       /* 0x140 */
	efi_info_t efi_info;                                    /* 0x1c0 */
	uint8_t alt_mem_k[0x1e4 - 0x1e0];                       /* 0x1e0 */
	uint8_t scratch[0x1e8 - 0x1e4];                         /* 0x1e4 */
	uint8_t e820_entries;                                   /* 0x1e8 */
	uint8_t eddbuf_entries;                                 /* 0x1e9 */
	uint8_t edd_mbr_sig_buf_entries;                        /* 0x1ea */
	uint8_t _pad6[6];                                       /* 0x1eb */
	setup_header_t setup_hdr;                               /* setup header */      /* 0x1f1 */
	uint8_t _pad7[0x290 - 0x1f1 - sizeof(setup_header_t)];
	uint8_t edd_mbr_sig_buffer[0x2d0 - 0x290];              /* 0x290 */
	e820entry_t e820_map[E820MAX];                          /* 0x2d0 */
	uint8_t _pad8[48];                                      /* 0xcd0 */
	uint8_t eddbuf[0xeec - 0xd00];                          /* 0xd00 */
	uint8_t _pad9[276];                                     /* 0xeec */
} boot_params_t;

static inline boolean_t plus_overflow_u32(uint32_t x, uint32_t y)
{
	return (((uint32_t)(~0)) - x) < y;
}

static uint32_t strlen(const char *str)
{
	const char *s = str;

	while (*s)
		++s;

	return s - str;
}

static multiboot_module_t *loader_get_module(multiboot_info_t *mbi,
				    grub_module_index_t midx)
{
	multiboot_module_t *mod = NULL;

	if (!(mbi->flags & MBI_MODULES)) {
		print_panic("multiboot info does not contain mods field!\n");
		return NULL;
	}

	if (mbi->mods_count == 0) {
		print_panic("no module available!\n");
		return NULL;
	}

	if (midx > mbi->mods_count) {
		print_panic("request index exceeds module count!\n");
		return NULL;
	}

	mod = (multiboot_module_t *)((uint64_t)mbi->mods_addr);

	return &mod[midx];
}

static char *loader_get_module_cmdline(multiboot_info_t *mbi,
				    grub_module_index_t midx)
{
	multiboot_module_t *mod = loader_get_module(mbi, midx);

	if (mod == NULL) {
		print_panic("can't get valid module'!\n");
		return NULL;
	}

	return (char *)((uint64_t)mod->cmdline);
}

static void loader_get_highest_sized_ram(multiboot_info_t *mbi,
				uint64_t size, uint64_t limit,
				uint64_t *ram_base, uint64_t *ram_size)
{
	uint64_t last_fit_base = 0, last_fit_size = 0;

	multiboot_memory_map_t *mmap = (multiboot_memory_map_t *)(uint64_t)(mbi->mmap_addr);

	/* walk through each one of mem map entry to get highest aviable address */
	for (unsigned int i = 0; i < mbi->mmap_length / sizeof(multiboot_memory_map_t); i++) {
		multiboot_memory_map_t *entry = &(mmap[i]);

		/* only check AVAILABLE memory range */
		if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
			uint64_t base = entry->addr;
			uint64_t length = entry->len;

			/* over "limit" so use the last region that fits */
			if (base + length > limit) {
				break;
			}

			if (size <= length) {
				/* do not assume the "base" is always larger than "last_fit_base" */
				if (base > last_fit_base) {
				    last_fit_base = base;
				    last_fit_size = length;
				}
			}
		}
	}

	*ram_base = last_fit_base;
	*ram_size = last_fit_size;
}

static boolean_t loader_setup_boot_params(multiboot_info_t *mbi,
				    boot_params_t *boot_params,
				    linux_kernel_header_t *hdr)
{
	uint32_t i;

	/* copy the whole setup data from image header to boot parameter */
	memcpy(&boot_params->setup_hdr, &hdr->setup_hdr, sizeof(setup_header_t));


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
		print_panic("no memory map info in multiboot info structure\n");
		return FALSE;
	}

	return TRUE;
}

static boolean_t expand_linux_image(multiboot_info_t *mbi,
				    void *linux_image, uint32_t linux_size,
				    void *initrd_image, uint32_t initrd_size,
				    uint64_t *boot_param_addr, uint64_t *entry_point)
{
	linux_kernel_header_t *hdr = NULL;
	boot_params_t *boot_params = NULL;
	const char *kernel_cmdline = NULL;
	uint64_t protected_mode_base = 0, initrd_base = 0;
	unsigned long real_mode_size = 0, prot_size = 0;

	/*params check*/
	if (linux_image == NULL || linux_size == 0) {
		printf("prarams check failed 1111\n");
		return FALSE;
	}

	if (linux_size < sizeof(linux_kernel_header_t)) {
		printf("prarams check failed 2222, header size = 0x%lx\n", sizeof(linux_kernel_header_t));
		return FALSE;
	}

	if (entry_point == NULL || boot_param_addr == NULL) {
		printf("prarams check failed 3333\n");
		return FALSE;
	}

	hdr = (linux_kernel_header_t *)linux_image;

	/*kernel header check and parse*/
	if (hdr->setup_hdr.setup_sects == 0) {
		hdr->setup_hdr.setup_sects = DEFAULT_SECTOR_NUM;
	}

	if (hdr->setup_hdr.setup_sects > MAX_SECTOR_NUM) {
		print_panic("exceed the max sector number, invalid kernel!\n");
		return FALSE;
	}

	if (hdr->setup_hdr.header != HDRS_MAGIC) {
		//old kernel
		print_panic("old kernel header magic not supported now!\n");
		return FALSE;
	}

	if (!(hdr->setup_hdr.loadflags & FLAG_LOAD_HIGH)) {
		print_panic("cannot support the old kernel that not loaded to high memory!\n");
		return FALSE;
	}

	real_mode_size = (hdr->setup_hdr.setup_sects + 1) * SECTOR_SIZE;

	/* allocate "boot_params+cmdline" from heap space*/
	boot_params = (boot_params_t *)allocate_memory(
		sizeof(boot_params_t) + hdr->setup_hdr.cmdline_size);
	if (boot_params == NULL) {
		print_panic("allocate memory for linux boot_params failed!\n");
		return FALSE;
	}

	/* put cmd_line_ptr after boot_parameters */
	hdr->setup_hdr.cmd_line_ptr = (uint64_t)(boot_params) + sizeof(boot_params_t);

	/*
	 *  check boot protocol version 2.10 (Kernel 2.6.31+)
	 */
	if ((hdr->setup_hdr.version) >= 0x020a) {
		prot_size = hdr->setup_hdr.init_size;
		prot_size = PAGE_ALIGN_4K(prot_size);
	} else {
		print_panic("boot protocol version < 2.10, not supported right now. ver=0x%",
			hdr->setup_hdr.version);
		return FALSE;
	}


	/* boot loader is grub2, so set type_of_loader to 0x72 */
	hdr->setup_hdr.type_of_loader = GRUB_LINUX_BOOT_LOADER_TYPE;

	/* clear loadflags and heap_end_ptr*/
	hdr->setup_hdr.loadflags &= ~FLAG_CAN_USE_HEAP; /* can not use heap */

	if ((initrd_image !=  0) && (initrd_size != 0)) {
		/* load initrd and set ramdisk_image and ramdisk_size
		 *  The initrd should typically be located as high in memory as possible
		 */
		uint64_t mem_limit = 0x100000000ULL;
		uint64_t max_ram_base, max_ram_size;

		loader_get_highest_sized_ram(mbi, initrd_size, mem_limit,
			&max_ram_base, &max_ram_size);

		if (max_ram_base == 0) {
			return FALSE;
		}
		if (max_ram_size == 0) {
			return FALSE;
		}

		if (initrd_size > max_ram_size) {
			return FALSE;
		}
		if (max_ram_base > ((uint64_t)(uint32_t)(~0))) {
			return FALSE;
		}
		if (plus_overflow_u32((uint32_t)max_ram_base,
				(uint32_t)(max_ram_size - initrd_size))) {
			return FALSE;
		}

		/*
		 *  try to get the higher part in an AVAILABLE memory range
		 *  and clear lower 12 bit to make it page-aligned down.
		 */
		initrd_base = (max_ram_base + max_ram_size - initrd_size) & (~PAGE_4K_MASK);

		/* exceed initrd_addr_max specified in vmlinuz header? */
		if (initrd_base + initrd_size > hdr->setup_hdr.initrd_addr_max) {
			/* make it much lower, if exceed it */
			initrd_base = hdr->setup_hdr.initrd_addr_max - initrd_size;
			initrd_base = initrd_base & (~PAGE_4K_MASK);
		}
	}

	if (hdr->setup_hdr.relocatable_kernel) {
		/* A relocatable kernel that is loaded at an alignment
		 * incompatible value will be realigned during kernel
		 * initialization.
		 */
		protected_mode_base = (uint64_t)linux_image + real_mode_size;
	} else {
		/* If need to support older kernel, need to move
		 * kernel to pref_address.
		 */
		print_panic("Linux protected mode not loaded (old kernel not relocatable)!\n");
		return FALSE;
	}

	if ((initrd_image != 0) && (initrd_size != 0)) {
		/* make sure no overlap between initrd and protected mode kernel code */
		if ((protected_mode_base + prot_size) > initrd_base) {
			print_panic("Initrd size is too large (or protected mode code size is too large)!\n");
			return FALSE;
		}

		/* relocate initrd image to higher end location. */
		memcpy((void *)initrd_base, initrd_image, initrd_size);

		hdr->setup_hdr.ramdisk_image = initrd_base;
		hdr->setup_hdr.ramdisk_size = initrd_size;
	} else {
		hdr->setup_hdr.ramdisk_image = 0;
		hdr->setup_hdr.ramdisk_size = 0;
	}

	hdr->setup_hdr.code32_start = protected_mode_base;


	/* set vid_mode
	 * hardcode as normal mode,
	 * TODO-need to get it from cmdlline if present.
	 */
	hdr->setup_hdr.vid_mode = GRUB_LINUX_VID_MODE_NORMAL;

	/* get cmdline param */
	kernel_cmdline = (char *)(loader_get_module_cmdline(mbi, MVMLINUZ));

	/* check max cmdline_size */
	if (strlen(kernel_cmdline) > hdr->setup_hdr.cmdline_size) {
		print_panic("cmdline size exceeds the max allowable value!\n");
		return FALSE;
	}

	/* copy cmdline to boot parameter */
	memcpy((void *)(uint64_t)hdr->setup_hdr.cmd_line_ptr, kernel_cmdline, strlen(kernel_cmdline));

	/* setup boot parameters according to linux boot protocol */
	if (!loader_setup_boot_params(mbi, boot_params, hdr)) {
		print_panic("failed to configure linux boot parames!\n");
		return FALSE;
	}

	/* get 64 bit entry point */
	*entry_point = (uint64_t)(boot_params->setup_hdr.code32_start + 0x200);

	/*
	 *  get boot params address
	 *  (will be put into esi according to boot protocol)
	 */
	*boot_param_addr = (uint64_t)boot_params;

	return TRUE;
}

boolean_t linux_kernel_parse(multiboot_info_t *mbi, uint64_t *boot_param_addr, uint64_t *entry_point)
{
	void *kernel_image, *initrd_image;
	uint32_t kernel_size, initrd_size;

	multiboot_module_t *module = loader_get_module(mbi, MVMLINUZ);
	if (module == NULL) {
		print_panic("get kernel module failed!\n");
		return FALSE;
	}

	kernel_image = (void *)((uint64_t)module->mod_start);
	kernel_size = module->mod_end - module->mod_start;

	/*get initrd module*/
	module = loader_get_module(mbi, MINITRD);
	if (module == NULL) {
		initrd_image = 0;
		initrd_size = 0;
	} else {
		initrd_image = (void *)((uint64_t)module->mod_start);
		initrd_size = module->mod_end - module->mod_start;
	}

	if (!expand_linux_image(mbi, kernel_image, kernel_size,
				initrd_image, initrd_size, boot_param_addr, entry_point)) {
		print_panic("failed to expand linux image!\n");
		return FALSE;
	}

	return TRUE;
}
