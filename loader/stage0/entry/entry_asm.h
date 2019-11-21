/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _ENTRY_ASM_H_
#define _ENTRY_ASM_H_

#define TOS_HEADER_MAGIC              0x6d6d76656967616d
#define TOS_HEADER_VERSION            1
#define TOS_IMAGE_VERSION             1
#define TOS_STARTUP_VERSION           3

/*
 * The size of our stack (16KB) for loader(stage0+stage1),
 * the stack might be used by UEFI services when UEFI services library
 * is enabled.
 */
#define STAGE0_STACK_SIZE             0x4000

#define STAGE0_RT_SIZE                0x10000

/* Temp STACK for stage1 to launch APs */
#define AP_TEMP_STACK_SIZE            (0x400 * ((MAX_CPU_NUM) + 1))

#define STAGE1_RT_SIZE                (0xA000 + (AP_TEMP_STACK_SIZE))

/* This payload memory will store evmm_desc_t and device_sec_info_t */
#define EVMM_PAYLOAD_SIZE             0x2000

#endif
