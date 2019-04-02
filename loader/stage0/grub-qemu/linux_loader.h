/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */
#ifndef _LINUX_LOADER_H_
#define _LINUX_LOADER_H_

#define SECTOR_SIZE                 (1 << 9)    /* 0x200 = 512B */

#define DEFAULT_SECTOR_NUM          4           /* default sector number 4 */
#define MAX_SECTOR_NUM              64          /* max sector number 64 */
#define HDRS_MAGIC                  0x53726448
#define GRUB_LINUX_BOOT_LOADER_TYPE 0x72
#define FLAG_LOAD_HIGH              0x01
#define FLAG_CAN_USE_HEAP           0x80
#define GRUB_LINUX_VID_MODE_NORMAL  0xFFFF
#define E820MAX                     128

#define MAX_LINUX_CMDLINE_LEN 1024

typedef struct {
	uint64_t addr;                                  /* start of memory segment */
	uint64_t size;                                  /* size of memory segment */
	uint32_t type;                                  /* type of memory segment */
} __attribute__ ((packed)) e820entry_t;

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
	uint8_t __unused_000[0x1e8];
	uint8_t e820_entries;                                             /* 0x1e8 */
	uint8_t __unused_1e9[0x1f1 - 0x1e9];                              /* 0x1e9 */
	setup_header_t setup_hdr;                                         /* 0x1f1 */    /* setup header */
	uint8_t _pad1[0x290 - 0x1f1 - sizeof(setup_header_t)];
	uint8_t __unused_290[0x2d0 - 0x290];                              /* 0x290 */
	e820entry_t e820_map[E820MAX];                                    /* 0x2d0 */
	uint8_t _pad2[0x1000 - sizeof(e820entry_t) * E820MAX - 0x2d0];
} boot_params_t;

boolean_t linux_kernel_parse(multiboot_info_t *mbi,
		uint64_t *boot_param_addr, uint64_t *entry_point);

#endif

