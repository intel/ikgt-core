/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _ABL_BOOT_PARAM_H_
#define _ABL_BOOT_PARAM_H_

#include "stage0_asm.h"
#include "evmm_desc.h"
#include "stage0_lib.h"

#define IMAGE_ID_MAX_LEN 8

#define IMAGE_BOOT_PARAMS_VERSION   1
#define VMM_BOOT_PARAMS_VERSION     1
#define TRUSTY_BOOT_PARAMS_VERSION  1
#define ANDROID_BOOT_PARAMS_VERSION 1

#define VMM_IMAGE_ID            0x00000000004d4d56ULL /* "MMV" standards for "VMM" */
#define TRUSTY_IMAGE_ID         0x0000797473757254ULL /* "ytsurT" standards for "Trusty" */
#define ANDROID_IMAGE_ID        0x0064696f72646e41ULL /* "diordnA" standards for "Android" */

/* register value ordered by: pushal, pushfl */
typedef struct init_register {
	uint32_t eflags;
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
} init_register_t;

typedef enum {
	IMAGE_AOSP,
	IMAGE_ELF,
	IMAGE_OUT,
	IMAGE_MULTIBOOT,
	IMAGE_PROPRIETARY
} image_format_t;

typedef struct {
	char ImageID[IMAGE_ID_MAX_LEN];    /* "Android", "VMM", "Trusty", etc... */
	image_format_t ImageFormat;        /* AOSP, elf, a.out, multiboot, etc... */
	uint32_t ImageDataSize;            /* size of image's boot params */
	uint32_t ImageDataPtr;             /* 32-bit pointer to image's boot params */
} image_element_t;

typedef struct {
	uint16_t Version;                  /* version of this structure */
	uint16_t NbImage;                  /* num of images */
	uint32_t ImageElementAddr;         /* address of list of image_element_t */
} image_params_t;

typedef struct {
	uint16_t Version;                  /* version of this structure */
	uint32_t VMMheapAddr;              /* ABL shall alloc a 36MB heap for VMM */
	uint32_t VMMheapSize;              /* heap size allocated */
	uint32_t VMMSipiApWkupAddr;        /* ABL shall alloc 1 page under 1MB mem */
	uint32_t VMMMemBase;               /* VMM's Base address */
	uint32_t VMMMemSize;               /* assumed to be 4MB */
}PACKED vmm_boot_params_t;

#define ABL_SEED_LEN 32
#define ABL_SEED_LIST_MAX 4
typedef struct _abl_seed_info {
	uint8_t svn;
	uint8_t reserved[3];
	uint8_t seed[ABL_SEED_LEN];
} abl_seed_info_t;

typedef struct {
	uint16_t Version;                  /* version of this structure */
	uint32_t TrustyMemBase;            /* Trusty mem base address */
	uint32_t TrustyMemSize;            /* assumed to be 16MB */
	uint32_t num_seeds;
	abl_seed_info_t seed_list[ABL_SEED_LIST_MAX];
	//rot_data_t rot;                  /* No need for APL+ABL */
}PACKED abl_trusty_boot_params_t;

typedef struct {
	uint64_t base;
	uint32_t limit;
	uint32_t attributes;
	uint16_t selector;
	uint16_t reserved[3];
} segment_struct_t;

typedef struct {
	uint16_t limit;
	uint32_t base;
	uint32_t reserved;
}PACKED idt_gdt_register_t;

typedef struct {
	uint64_t cpu_gp_register[16]; /* rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8..r15 */
	uint64_t rip;
	uint64_t rflags;
	idt_gdt_register_t gdtr;
	idt_gdt_register_t idtr;
	segment_struct_t cs;
	segment_struct_t ds;
	segment_struct_t ss;
	segment_struct_t es;
	segment_struct_t fs;
	segment_struct_t gs;
	segment_struct_t ldtr;
	segment_struct_t tr;
	uint32_t cr0;
	uint32_t cr3;
	uint32_t cr4;
	uint32_t cr8;
	uint64_t msr_efer;
}PACKED cpu_state_t;

typedef struct {
	uint16_t Version;                              /* version of this structure */
	uint16_t ImagePreload;
	union {
		cpu_state_t CpuState;                  /* used in case ImagePreload is true */
		struct {                               /* used in case ImagePreload is false */
			uint32_t OemKeyManifestAddr;
			uint32_t OemKeyManifestSize;
			uint32_t AndroidCmdLine;
		} AndroidImageUnpreload;
	};
} android_image_boot_params_t;

/* arguments parsed from cmdline */
typedef struct cmdline_params {
	uint64_t image_boot_param_addr;
	uint64_t cpu_num;
	uint64_t cpu_freq;
} cmdline_params_t;

boolean_t cmdline_parse(multiboot_info_t *mbi, cmdline_params_t *cmdline_param);
boolean_t find_boot_params(cmdline_params_t *cmdline_params,
			abl_trusty_boot_params_t **trusty_boot,
			vmm_boot_params_t **vmm_boot,
			android_image_boot_params_t **and_boot);
boolean_t get_emmc_serial(android_image_boot_params_t *android_boot_params, char *serial);
void fill_g0gcpu0(gcpu_state_t *evmm_g0gcpu0, cpu_state_t *abl_g0gcpu0);
#endif
