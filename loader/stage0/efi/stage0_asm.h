/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _STAGE0_ASM_H_
#define _STAGE0_ASM_H_

#define TOS_HEADER_MAGIC              0x6d6d76656967616dULL
#define TOS_HEADER_VERSION            1
#define TOS_IMAGE_VERSION             1
#define TOS_STARTUP_VERSION           3

/*
 * The size of our stack (16KB) for loader(stage0+stage1),
 * the stack would be used by UEFI services when UEFI services library
 * is enabled.
 */
#define STAGE0_STACK_SIZE             0x4000

#define STAGE1_IMG_SIZE	              0xC000
#define SEED_MSG_DST_OFFSET           0

/* This payload memory will store evmm_desc_t and device_sec_info_t */
#define EVMM_PAYLOAD_SIZE             0x2000

#endif
