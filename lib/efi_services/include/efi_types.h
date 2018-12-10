/*
 * Copyright (c) 2018 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _EFI_TYPES_H_
#define _EFI_TYPES_H_

typedef uint64_t uintn_t;
typedef long long intn_t;
typedef uint8_t efi_bool_t;

typedef uintn_t efi_status_t;
typedef void *efi_handle_t;
typedef void *efi_event_t;
typedef uintn_t efi_tpl_t;

/* For x86-64: UEFI follows MS_ABI calling convention */
#define EFI_API __attribute__((ms_abi))

typedef struct {
	uint32_t data1;
	uint16_t data2;
	uint16_t data3;
	uint8_t data4[8];
} efi_guid_t;

#endif
