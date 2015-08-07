/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef _MON_ARCH_DEFS_H_
#define _MON_ARCH_DEFS_H_

#include "mon_defs.h"

/*****************************************************************************
*
* This file contains unified architecture-related structures, defined by xmon
*
*****************************************************************************/

/*
 * Standard E820 BIOS map
 */
typedef enum {
	INT15_E820_ADDRESS_RANGE_TYPE_MEMORY = 1,
	INT15_E820_ADDRESS_RANGE_TYPE_RESERVED = 2,
	INT15_E820_ADDRESS_RANGE_TYPE_ACPI = 3,
	INT15_E820_ADDRESS_RANGE_TYPE_NVS = 4,
	INT15_E820_ADDRESS_RANGE_TYPE_UNUSABLE = 5
} int15_e820_range_type_t;

typedef union {
	struct {
		uint32_t enabled:1;
		uint32_t non_volatile:1;
		uint32_t reserved:30;
	} PACKED bits;
	uint32_t uint32;
} PACKED int15_e820_memory_map_ext_attributes_t;

typedef struct {
	uint64_t		base_address;
	uint64_t		length;
	int15_e820_range_type_t address_range_type;
} PACKED int15_e820_memory_map_entry_t;

typedef struct {
	int15_e820_memory_map_entry_t		basic_entry;
	int15_e820_memory_map_ext_attributes_t	extended_attributes;
} PACKED int15_e820_memory_map_entry_ext_t;

/*
 * The memory_map_entry may be either int15_e820_memory_map_entry_ext_t (24
 * bytes) or int15_e820_memory_map_entry_t (20 bytes). The returned value size
 * depends on the caller-passed buffer - if caller passed 24 bytes or more,
 * the extended entry is returned. The minimum buffer size must be 20 bytes.
 */
typedef struct {
	/* size in bytes of all entries, not including the size field itself */
	uint32_t				memory_map_size;
	int15_e820_memory_map_entry_ext_t	memory_map_entry[1];
} PACKED int15_e820_memory_map_t;

/* NOTE: This enumerator is referened in assembler */
typedef enum {
	/* GP */
	IA32_REG_RAX = 0,
	IA32_REG_RBX,
	IA32_REG_RCX,
	IA32_REG_RDX,
	IA32_REG_RDI,
	IA32_REG_RSI,
	IA32_REG_RBP,
	IA32_REG_RSP,
	IA32_REG_R8,
	IA32_REG_R9,
	IA32_REG_R10,
	IA32_REG_R11,
	IA32_REG_R12,
	IA32_REG_R13,
	IA32_REG_R14,
	IA32_REG_R15,

	/* RIP */
	IA32_REG_RIP,

	/* flags */
	IA32_REG_RFLAGS,

	/* the count of GP registers */
	IA32_REG_GP_COUNT
} mon_ia32_gp_registers_t;

/* NOTE: This enumerator is referened in assembler */
typedef enum {
	/* XMM */
	IA32_REG_XMM0 = 0,
	IA32_REG_XMM1,
	IA32_REG_XMM2,
	IA32_REG_XMM3,
	IA32_REG_XMM4,
	IA32_REG_XMM5,
	IA32_REG_XMM6,
	IA32_REG_XMM7,
	IA32_REG_XMM8,
	IA32_REG_XMM9,
	IA32_REG_XMM10,
	IA32_REG_XMM11,
	IA32_REG_XMM12,
	IA32_REG_XMM13,
	IA32_REG_XMM14,
	IA32_REG_XMM15,

	/* the count of XMM registers */
	IA32_REG_XMM_COUNT
} mon_ia32_xmm_registers_t;

typedef enum {
	/* general segments */
	IA32_SEG_CS = 0,
	IA32_SEG_DS,
	IA32_SEG_SS,
	IA32_SEG_ES,
	IA32_SEG_FS,
	IA32_SEG_GS,
	IA32_SEG_LDTR,
	IA32_SEG_TR,
	/* the count of general segments */
	IA32_SEG_COUNT
} mon_ia32_segment_registers_t;

typedef enum {
	IA32_REG_DR0 = 0,
	IA32_REG_DR1,
	IA32_REG_DR2,
	IA32_REG_DR3,
	/* dr4 and dr5 are reserved */
	IA32_REG_DR6,
	IA32_REG_DR7,

	/* the count of debug registers */
	IA32_REG_DEBUG_COUNT
} mon_ia32_debug_registers_t;

typedef enum {
	IA32_CTRL_CR0 = 0,
	IA32_CTRL_CR2,
	IA32_CTRL_CR3,
	IA32_CTRL_CR4,
	IA32_CTRL_CR8,

	/* the count of control registers */
	IA32_CTRL_COUNT
} mon_ia32_control_registers_t;

#define UNSUPPORTED_CR   IA32_CTRL_COUNT

typedef enum {
	IA32_MON_MSR_DEBUGCTL = 0,
	IA32_MON_MSR_EFER,
	IA32_MON_MSR_PAT,
	IA32_MON_MSR_SYSENTER_ESP,
	IA32_MON_MSR_SYSENTER_EIP,
	IA32_MON_MSR_SYSENTER_CS,
	IA32_MON_MSR_SMBASE,
	IA32_MON_MSR_PERF_GLOBAL_CTRL,
	IA32_MON_MSR_FEATURE_CONTROL,
	IA32_MON_MSR_STAR,
	IA32_MON_MSR_LSTAR,
	IA32_MON_MSR_FMASK,
	IA32_MON_MSR_FS_BASE,
	IA32_MON_MSR_GS_BASE,
	IA32_MON_MSR_KERNEL_GS_BASE,

	/* the count of supported model specific registers */
	IA32_MON_MSR_COUNT
} mon_ia32_model_specific_registers_t;

/* NOTE: This structure is referened in assembler */
typedef struct {
	uint64_t reg[IA32_REG_GP_COUNT];
} PACKED mon_gp_registers_t;

/* NOTE: This structure is referened in assembler */
typedef struct {
	uint128_t reg[IA32_REG_XMM_COUNT];
} PACKED mon_xmm_registers_t;

typedef union {
	uint32_t attr32;
	struct {
		uint32_t type:4;               /* bits 3:0 */
		uint32_t s_bit:1;              /* bit 4 */
		uint32_t dpl:2;                /* bit2 6:5 */
		uint32_t p_bit:1;              /* bit 7 */
		uint32_t reserved_11_8:4;      /* bits 11:8 */
		uint32_t avl_bit:1;            /* bit 12 */
		uint32_t l_bit:1;              /* bit 13 */
		uint32_t db_bit:1;             /* bit 14 */
		uint32_t g_bit:1;              /* bit 15 */
		uint32_t null_bit:1;           /* bit 16 */
		uint32_t reserved_31_17:15;    /* bits 31:17 */
	} PACKED bits;
} PACKED mon_segment_attributes_t;

typedef struct {
	uint64_t	base;           /* for real mode it should be selector << 4 */
	uint32_t	limit;
	uint32_t	attributes;     /* mon_segment_attributes_t */
	uint16_t	selector;       /* for real mode this is the segment value */
	uint16_t	reserved[3];
} PACKED mon_segment_struct_t;

typedef struct {
	mon_segment_struct_t segment[IA32_SEG_COUNT];
} PACKED mon_segments_t;

typedef struct {
	uint64_t	base;
	uint32_t	limit;
} PACKED mon_ia32_gdt_register_t;

typedef mon_ia32_gdt_register_t mon_ia32_idt_register_t;

typedef struct {
	uint64_t reg[IA32_REG_DEBUG_COUNT];
} PACKED mon_debug_register_t;

typedef struct {
	/* Control registers */
	uint64_t		cr[IA32_CTRL_COUNT];

	/* GDT */
	mon_ia32_gdt_register_t gdtr;

	uint32_t		reserved_1;

	/* IDT */
	mon_ia32_idt_register_t idtr;

	uint32_t		reserved_2;
} PACKED mon_control_registers_t;

typedef struct {
	uint64_t	msr_debugctl;
	uint64_t	msr_efer;
	uint64_t	msr_pat;

	uint64_t	msr_sysenter_esp;
	uint64_t	msr_sysenter_eip;

	uint64_t	pending_exceptions;

	uint32_t	msr_sysenter_cs;

	uint32_t	interruptibility_state;
	uint32_t	activity_state;
	uint32_t	smbase;
} PACKED mon_model_specific_registers_t;




#endif  /* _MON_ARCH_DEFS_H_ */
