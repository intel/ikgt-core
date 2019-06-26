/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _TRUSTY_BOOT_INFO_H_
#define _TRUSTY_BOOT_INFO_H_

#define BOOTLOADER_SEED_MAX_ENTRIES     10
#define RPMB_MAX_PARTITION_NUMBER       6
#define MMC_PROD_NAME_WITH_PSN_LEN      15
#define BUP_MKHI_BOOTLOADER_SEED_LEN    64

/* Structure of seed info */
typedef struct _seed_info {
	uint8_t cse_svn;
	uint8_t bios_svn;
	uint8_t padding[2];
	uint8_t seed[BUP_MKHI_BOOTLOADER_SEED_LEN];
} seed_info_t;

typedef struct {
	uint32_t size_of_this_struct;

	/* version info:
		0: baseline structure
		1: add ** new field
	 */
	uint32_t version;

	/* platform:
		0: Dummy (fake secret)
		1: APL (APL + ABL/SBL)
		2: ICL (ICL + SBL)
		3: CWP (APL|ICL + SBL + CWP)
		4: Brillo (Android Things)
	*/
	uint32_t platform;

	/* flags info:
		Bit 0: manufacturing state (0:manufacturing done; 1:in manufacturing mode)
		Bit 1: secure boot state (0:disabled; 1: enabled)
		Bit 2: test seeds (ICL only - 0:production seeds; 1: test seeds)
		other bits all reserved as 0
	*/
	uint32_t flags;

	/* Keep 64-bit align */
	uint32_t pad1;

	/* Seed list, include useeds(user seeds) and dseed(device seeds) */
	uint32_t num_seeds;
	seed_info_t useed_list[BOOTLOADER_SEED_MAX_ENTRIES];
	seed_info_t dseed_list[BOOTLOADER_SEED_MAX_ENTRIES];

	/* For ICL+ */
	/* rpmb keys, Currently HMAC-SHA256 is used in RPMB spec and 256-bit (32byte) is enough.
	   Hence only lower 32 bytes will be used for now for each entry. But keep higher 32 bytes
	   for future extension. Note that, RPMB keys are already tied to storage device serial number.
	   If there are multiple RPMB partitions, then we will get multiple available RPMB keys.
	   And if rpmb_key[n][64] == 0, then the n-th RPMB key is unavailable (Either because of no such
	   RPMB partition, or because OSloader doesn’t want to share the n-th RPMB key with Trusty)
	*/
	uint8_t rpmb_key[RPMB_MAX_PARTITION_NUMBER][64];

	/* 256-bit AES encryption key to encrypt/decrypt attestation keybox,
	   this key should be derived from a fixed key which is RPMB seed.
	   RPMB key (HMAC key) and this encryption key (AES key) are both
	   derived from the same RPMB seed.
	*/
	uint8_t attkb_enc_key[32];

	/* For APL only */
	/* RPMB key is derived with dseed together with this serial number, for ICL +,
	   CSE directly provides the rpmb_key which is already tied to serial number.
	   Concatenation of emmc product name with a string representation of PSN
	*/
	char serial[MMC_PROD_NAME_WITH_PSN_LEN];
	char pad2;
} device_sec_info_v0_t;

#endif
