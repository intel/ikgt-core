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
	uint8_t  setup_sects;
	uint8_t  unused0[8];
	uint16_t vid_mode;
	uint8_t  unused1[6];
	uint32_t header;
	uint16_t version;
	uint8_t  unused2[8];
	uint8_t  type_of_loader;
	uint8_t  loadflags;
	uint8_t  unused3[2];
	uint32_t code32_start;
	uint32_t ramdisk_image;
	uint32_t ramdisk_size;
	uint8_t  unused4[8];
	uint32_t cmd_line_ptr;
	uint8_t  unused5[8];
	uint8_t  relocatable_kernel;
	uint8_t  unused6[3];
	uint32_t cmdline_size;
	uint8_t  unused7[20];
	uint64_t setup_data;
	uint8_t  unused8[12];
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

typedef struct {
	uint64_t next;
	uint32_t type;
	uint32_t len;
	uint8_t  data[0];
}setup_data_t;

enum {
	NONE = 0,
	SETUP_E820,
	SETUP_DTB
};

boolean_t linux_kernel_parse(multiboot_info_t *mbi,
		uint64_t *boot_param_addr, uint64_t *entry_point);

void reserve_region_from_mmap(boot_params_t *boot_params,
		uint64_t start, uint64_t size);

#endif

