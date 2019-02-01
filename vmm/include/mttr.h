/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef MTRR_ABSTRACTION_H
#define MTRR_ABSTRACTION_H

#include "vmm_base.h"
#include "vmm_arch.h"

#define MSR_MTRRCAP  0xFE
#define MSR_MTRR_DEF_TYPE 0x2FF
#define MSR_MTRR_FIX64K_00000 0x250
#define MSR_MTRR_FIX16K_80000 0x258
#define MSR_MTRR_FIX16K_A0000 0x259
#define MSR_MTRR_FIX4K_C0000  0x268
#define MSR_MTRR_FIX4K_C8000  0x269
#define MSR_MTRR_FIX4K_D0000  0x26A
#define MSR_MTRR_FIX4K_D8000  0x26B
#define MSR_MTRR_FIX4K_E0000  0x26C
#define MSR_MTRR_FIX4K_E8000  0x26D
#define MSR_MTRR_FIX4K_F0000  0x26E
#define MSR_MTRR_FIX4K_F8000  0x26F
#define MSR_MTRR_PHYSBASE0    0x200
#define MSR_MTRR_PHYSMASK0    0x201
#define MSR_MTRR_PHYSBASE1    0x202
#define MSR_MTRR_PHYSMASK1    0x203
#define MSR_MTRR_PHYSBASE2    0x204
#define MSR_MTRR_PHYSMASK2    0x205
#define MSR_MTRR_PHYSBASE3    0x206
#define MSR_MTRR_PHYSMASK3    0x207
#define MSR_MTRR_PHYSBASE4    0x208
#define MSR_MTRR_PHYSMASK4    0x209
#define MSR_MTRR_PHYSBASE5    0x20a
#define MSR_MTRR_PHYSMASK5    0x20b
#define MSR_MTRR_PHYSBASE6    0x20c
#define MSR_MTRR_PHYSMASK6    0x20d
#define MSR_MTRR_PHYSBASE7    0x20e
#define MSR_MTRR_PHYSMASK7    0x20f
#define MSR_MTRR_PHYSBASE8    0x210
#define MSR_MTRR_PHYSMASK8    0x211
#define MSR_MTRR_PHYSBASE9    0x212
#define MSR_MTRR_PHYSMASK9    0x213

#define MTRRCAP_WC_SUPPORTED (1ULL<<10)
#define MTRRCAP_FIX_SUPPORTED (1ULL<<8)
#define MTRRCAP_VCNT(mtrrcap) ((uint8_t)(mtrrcap))

#define MTRR_ENABLE (1ULL<<11)
#define MTRR_FIX_ENABLE (1ULL<<10)
#define MTRR_DEFAULT_TYPE(mtrr_def) ((uint8_t)(mtrr_def))

typedef struct _mtrr_section {
	uint64_t base;
	uint64_t size;
	cache_type_t type;
	uint8_t pad[7];
	struct _mtrr_section *next;
} mtrr_section_t;

void mtrr_init(void);
mtrr_section_t *get_mtrr_section_list(void);

#ifdef DEBUG
void mtrr_check(void);
#endif

#endif /* MTRR_ABSTRACTION_H */
