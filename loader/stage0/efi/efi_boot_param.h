/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _EFI_BOOT_PARAM_H_
#define _EFI_BOOT_PARAM_H_

#include "evmm_desc.h"
#include "device_sec_info.h"

#define TRUSTY_KEYBOX_KEY_SIZE        32

/*
 * This structure is used to pass data to TOS entry when calling to TOS entry to
 * launch VMM/Trusty.
 * It includes:
 *   1. EFI memory map info structure
 *   2. TOS image base address (<4G)
 *   3. Code address for sipi ap bringup/wakeup.
 *   4. Trusty/VMM regions (base/size)
 *   5. Seed List for trusty get from CSE or TPM
 *   6. Serial: MMC product name with a string representation of PSN
 */
typedef struct tos_startup_info {
	/* version of TOS startup info structure, currently set it as 1 */
	uint32_t version;

	/* Size of this structure for mismatching check, bootloader must populate it,
	 * and TOS loader must verify if it is matched with sizeof(tos_startup_info).
	 */
	uint32_t size;

	/* UEFI memory map address */
	uint64_t efi_memmap;
	/* EFI memory map size */
	uint32_t efi_memmap_size;

	/* Reserved for APs\u2019 wake-up, must be less than 1M and 4K-aligned according to
	 * CPU SIPI spec, Bootloader is responsible for allocating it (4kB in length)
	 * with Type EfiLoaderData.
	 */
	uint32_t sipi_ap_wkup_addr;

	/* Bootloader retrieves the trusty/vmm memory (base/size) from CSE/BIOS,
	 * and the size of region should be 4KB-aligned for both.
	 */
	uint64_t trusty_mem_base;
	uint64_t vmm_mem_base;
	uint32_t trusty_mem_size;
	uint32_t vmm_mem_size;

	/* rpmb keys, Currently HMAC-SHA256 is used in RPMB spec and 256-bit (32byte) is enough.
	   Hence only lower 32 bytes will be used for now for each entry. But keep higher 32 bytes
	   for future extension. Note that, RPMB keys are already tied to storage device serial number.
	   If there are multiple RPMB partitions, then we will get multiple available RPMB keys.
	   And if rpmb_key[n][64] == 0, then the n-th RPMB key is unavailable (Either because of no such
	   RPMB partition, or because OSloader doesn't want to share the n-th RPMB key with Trusty)
	*/
	uint8_t rpmb_key[RPMB_MAX_PARTITION_NUMBER][64];

	/* Seed */
	uint32_t num_seeds;
	seed_info_t seed_list[BOOTLOADER_SEED_MAX_ENTRIES];
	/* Concatenation of mmc product name with a string representation of PSN */
	uint8_t serial[MMC_PROD_NAME_WITH_PSN_LEN];

	uint8_t attkb_key[TRUSTY_KEYBOX_KEY_SIZE];

	uint64_t system_table_addr;
} __attribute__((packed))  tos_startup_info_t;

/*  TOS image header:
 *  Bootloader/Kernelflinger retrieves required info from this header.
 *  NOTE:
 *  1. Bootloader firstly get the boot_image_hdr, then get this header
 *     through "kernel_addr" field.
 *  2. Bootloader must relocate (copy) the TOS image to the
 *     address of loadtime memory that is allocated with size tos_ldr_size, and
 *     then call into the entry of TOS, this entry is calculated with:
 *     Base address (loadtime memory allocated with size of tos_ldr_size) + entry_offset.
 */
typedef struct tos_image_header{
	/* a 64 bit magic value */
	uint64_t magic;

	/* version of this image header */
	uint32_t version;

	/* size of this structure, populated by TOS image build tool for mismatching check.
	 * Bootloader must check if it is matched with sizeof(tos_image_header)
	 */
	uint32_t size;

	/* TOS image version and patch level combination, format compliant to
	 * keymaster-defined osVersion in AOSP:/system/core/mkbootimg/bootimg.h
	 */
	uint32_t tos_version;

	/* tos entry offset, the entry function prototype is defined as:
	 * int32_t  (*call_entry) (struct tos_startup_info *);
	 * here the call_entry is Base address (loadtime memory) + entry_offset
	 */
	uint32_t entry_offset;

	/* Bootloader allocates a memory region with this specified size, and copies TOS image
	 * to this allocated space. Note its memory type is EfiLoaderData, and address space
	 * must be below 4G for compatibility.
	 * Besides, Bootloader must wipe/free it after TOS launch.
	 */
	uint32_t tos_ldr_size;
	/* Version to indicate version of tos_startup_info_t */
	uint8_t startup_struct_version;
	uint8_t reserved[3];
}tos_image_header_t;

evmm_desc_t *boot_params_parse(tos_startup_info_t *tos_startup_info, uint64_t loader_addr);

#endif /* _EFI_BOOT_PARAM_H_ */
