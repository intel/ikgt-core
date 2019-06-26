/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _STAGE0_ASM_H_
#define _STAGE0_ASM_H_

#define LOAD_ADDR     0x12200000
/* The size of our stack (1KB) for stage0 */
#define STAGE0_STACK_SIZE            0x400

/* Simple and small GDT entries for booting only: */
#define __BOOT_NULL     (0x00)
#define __BOOT_CS       (0x08)
#define __BOOT_DS       (0x10)

/* Multiboot header Definitions of OS image*/
#define MULTIBOOT_HEADER_MAGIC          0x1BADB002

/* The flags for the Multiboot header (non-ELF) */
#define MULTIBOOT_HEADER_FLAGS         0x00010003

#endif
