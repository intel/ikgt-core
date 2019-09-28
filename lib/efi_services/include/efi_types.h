/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _EFI_TYPES_H_
#define _EFI_TYPES_H_

typedef enum {
  EfiReservedMemoryType,
  EfiLoaderCode,
  EfiLoaderData,
  EfiBootServicesCode,
  EfiBootServicesData,
  EfiRuntimeServicesCode,
  EfiRuntimeServicesData,
  EfiConventionalMemory,
  EfiUnusableMemory,
  EfiACPIReclaimMemory,
  EfiACPIMemoryNVS,
  EfiMemoryMappedIO,
  EfiMemoryMappedIOPortSpace,
  EfiPalCode,
  EfiPersistentMemory,
  EfiMaxMemoryType
} efi_memory_type_t;

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

typedef struct {
  uint32_t type;
  uint32_t pad;
  uint64_t physical_start;
  uint64_t virtual_start;
  uint64_t number_of_pages;
  uint64_t attribute;
} efi_memory_descriptor_t;

#endif
