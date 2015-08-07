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

#ifndef _EM64T_DEFS_H_
#define _EM64T_DEFS_H_

#include "msr_defs.h"


/*
 * IA-32 EFLAGS Register
 */
typedef union {
	struct {
		uint32_t cf:1;                 /* Carry Flag */
		uint32_t reserved_0:1;         /* reserved */
		uint32_t pf:1;                 /* Parity Flag */
		uint32_t reserved_1:1;         /* reserved */
		uint32_t af:1;                 /* Auxiliary Carry Flag */
		uint32_t reserved_2:1;         /* reserved */
		uint32_t zf:1;                 /* Zero Flag */
		uint32_t sf:1;                 /* Sign Flag */
		uint32_t tp:1;                 /* Trap Flag */
		uint32_t ifl:1;                /* Interrupt Enable Flag */
		uint32_t df:1;                 /* Direction Flag */
		uint32_t of:1;                 /* Overflow Flag */
		uint32_t iopl:2;               /* I/O Privilege Level */
		uint32_t nt:1;                 /* Nexted Task */
		uint32_t reserved_3:1;         /* reserved */
		uint32_t rf:1;                 /* Resume Flag */
		uint32_t vm:1;                 /* Virtual 8086 Mode */
		uint32_t ac:1;                 /* Alignment Check */
		uint32_t vif:1;                /* Virtual Interrupt Flag */
		uint32_t vip:1;                /* Virtual Interrupt Pending */
		uint32_t id:1;                 /* ID Flag */
		uint32_t reserved_4:10;        /* reserved */
		uint32_t reserved_5:32;        /* reserved */
	} PACKED bits;
	uint64_t uint64;
} PACKED em64t_rflags_t;

/*
 * IA-32 Control Register #0 (CR0)
 */
#define CR0_PE                    0x00000001
#define CR0_MP                    0x00000002
#define CR0_EM                    0x00000004
#define CR0_TS                    0x00000008
#define CR0_ET                    0x00000010
#define CR0_NE                    0x00000020
#define CR0_WP                    0x00010000
#define CR0_AM                    0x00040000
#define CR0_NW                    0x20000000
#define CR0_CD                    0x40000000
#define CR0_PG                    0x80000000

typedef union {
	struct {
		uint32_t pe:1;                 /* Protection Enable */
		uint32_t mp:1;                 /* Monitor Coprocessor */
		uint32_t em:1;                 /* Emulation */
		uint32_t ts:1;                 /* Task Switched */
		uint32_t et:1;                 /* Extension Type */
		uint32_t ne:1;                 /* Numeric Error */
		uint32_t reserved_0:10;        /* reserved */
		uint32_t wp:1;                 /* Write Protect */
		uint32_t reserved_1:1;         /* reserved */
		uint32_t am:1;                 /* Alignment Mask */
		uint32_t reserved_2:10;        /* reserved */
		uint32_t nw:1;                 /* Not Write-through */
		uint32_t cd:1;                 /* Cache Disable */
		uint32_t pg:1;                 /* Paging */
		uint32_t reserved_3:32;        /* Must be zero */
	} PACKED bits;
	uint64_t uint64;
} PACKED em64t_cr0_t;

#define EM64T_CR1_RESERVED_BITS(cr1)                   \
	((cr1).bits.reserved_0 && (cr1).bits.reserved_1 &&  \
	 (cr1).bits.reserved_2 && (cr1).bits.reserved_3)

/*
 * IA-32 Control Register #3 (CR3)
 */
typedef struct {
	struct {
		uint32_t reserved_0_2:3;
		uint32_t pwt:1;              /* Page Write Through */
		uint32_t pcd:1;              /* Page Cache Disable */
		uint32_t reserved_5_11:7;
		uint32_t base_address_lo:20; /* bits 31..12 of base address (low bits are zeroes) */
	} lo;
	struct {
		uint32_t base_address_hi:20; /* bits 51..32 of base address */
		uint32_t zeroes:11;
		uint32_t no_execute:1;
	} hi;
} PACKED em64t_cr3_t;

/*
 * IA-32 Control Register #4 (CR4)
 */
#define CR4_VME         0x00000001
#define CR4_PVI         0x00000002
#define CR4_TSD         0x00000004
#define CR4_DE          0x00000008
#define CR4_PSE         0x00000010
#define CR4_PAE         0x00000020
#define CR4_MCE         0x00000040
#define CR4_PGE         0x00000080
#define CR4_PCE         0x00000100
#define CR4_OSFXSR      0x00000200
#define CR4_OSXMMEXCPT  0x00000400
#define CR4_VMXE        0x00002000
#define CR4_SMXE        0x00004000
#define CR4_OSXSAVE     0x00040000

typedef union {
	struct {
		uint32_t vme:1;                /* Virtual-8086 Mode Extensions */
		uint32_t pvi:1;                /* Protected-Mode Virtual Interrupts */
		uint32_t tsd:1;                /* Time Stamp Disable */
		uint32_t de:1;                 /* Debugging Extensions */
		uint32_t pse:1;                /* Page Size Extensions */
		uint32_t pae:1;                /* Physical Address Extension */
		uint32_t mce:1;                /* Machine Check Enable */
		uint32_t pge:1;                /* Page Global Enable */
		uint32_t pce:1;                /* Performance Monitoring Counter Enable */
		uint32_t osfxsr:1;             /* Operating System Support for FXSAVE and FXRSTOR instructions */
		uint32_t osxmmexcpt:1;         /* Operating System Support for Unmasked SIMD Floating Point Exceptions */
		uint32_t reserved_0:2;         /* reserved */
		uint32_t vmxe:1;               /* VMX Enable */
		uint32_t smxe:1;               /* SMX Enable */
		uint32_t reserved_1:1;         /* Reseved */
		uint32_t fsgsbase:1;           /* Enables the instructions RDFSBASE, RDGSBASE, WRFSBASE, and WRGSBASE. */
		uint32_t pcide:1;              /* PCIDE */
		uint32_t osxsave:1;            /* XSAVE and Processor Extended States-Enable Bit */
		uint32_t reserved_2:1;         /* Reseved */
		uint32_t smep:1;               /* Supervisor Mode Execution Prevention */
		uint32_t smap:1;               /* Supervisor Mode ACCESS Prevention */
		uint32_t reserved_3:10;        /* reserved */
		uint32_t reserved_4:32;        /* reserved, must be zero */
	} PACKED bits;
	uint64_t uint64;
} PACKED em64t_cr4_t;

#define EM64T_CR4_RESERVED_BITS(cr4)                   \
	((cr4).bits.reserved_0 && (cr4).bits.reserved_1 &&  \
	 (cr4).bits.reserved_2)

/*
 * IA-32 Control Register #8 (CR8)
 */
typedef union {
	struct {
		uint32_t tpr:4;                /* Reflect APIC.TPR[7:4] bits */
		uint32_t reserved_1:28;        /* reserved, must be zero */
		uint32_t reserved_2:32;        /* reserved, must be zero */
	} PACKED bits;
	uint64_t uint64;
} PACKED em64t_cr8_t;

#define EM64T_CR8_VALID_BITS_MASK ((uint64_t)0x0F)

/*
 * Descriptor for the Global Descriptor Table(GDT) and Interrupt Descriptor
 * Table(IDT)
 */

typedef struct {
	uint16_t	limit;
	uint64_t	base;
} PACKED em64t_gdtr_t;

#define EM64T_SEGMENT_IS_UNUSABLE_ATTRUBUTE_VALUE 0x10000

/*
 * Code Segment Entry in Global Descriptor Table(GDT)
 */
typedef struct {
	uint32_t reserved;
	struct {
		uint32_t reserved_00_07:8;
		uint32_t accessed:1;
		uint32_t readable:1;
		uint32_t conforming:1;
		uint32_t mbo_11:1;     /* Must Be One */
		uint32_t mbo_12:1;     /* Must Be One */
		uint32_t dpl:2;        /* Descriptor Privilege Level */
		uint32_t present:1;
		uint32_t reserved_19_16:4;
		uint32_t avl:1; /* Available to software */
		uint32_t long_mode:1;
		uint32_t default_size:1;
		uint32_t granularity:1;
		uint32_t reserved_31_24:8;
	} hi;
} PACKED em64t_code_segment_descriptor_t;

#define CS_SELECTOR_CPL_BIT 0x3

/*
 * TSS Entry in Global Descriptor Table(GDT)
 */
typedef struct {
	struct {
		uint32_t segment_limit_00_15:16;
		uint32_t base_address_00_15:16;
	} q0;
	struct {
		uint32_t base_address_23_16:8;
		uint32_t type:4;
		uint32_t mbz_12:1;
		uint32_t dpl:2;
		uint32_t present:1;
		uint32_t segment_limit_16_19:4;
		uint32_t avl:1;
		uint32_t mbz_21_22:2;
		uint32_t granularity:1;
		uint32_t base_address_31_24:8;
	} q1;
	struct {
		uint32_t base_address_32_63;
	} q2;
	uint32_t q3;              /* reserved, must be zero */
} PACKED em64t_tss_segment_descriptor_t;

typedef struct {
	uint32_t	reserved_1;
	uint64_t	rsp[3];
	uint64_t	reserved_2;
	uint64_t	ist[7];
	uint64_t	reserved_3;
	uint16_t	reserved4;
	uint16_t	io_bitmap_address; /* offset inside TSS */
	uint8_t		io_bitmap_last_byte;
	uint8_t		pad[7];
} PACKED em64t_task_state_segment_t;

/*
 * Page-Map Level-4 and Ptr Directory Page Table
 */
typedef struct {
	struct {
		uint32_t present:1;
		uint32_t rw:1;
		uint32_t us:1;         /* user / supervisor */
		uint32_t pwt:1;        /* Page Write Through */
		uint32_t pcd:1;        /* Page Cache Disable */
		uint32_t accessed:1;
		uint32_t ignored:1;
		uint32_t zeroes:2;
		uint32_t avl:3;                /* available to software */
		uint32_t base_address_lo:20;   /* bits 31..12 of base address (low bits are zeroes) */
	} lo;
	struct {
		uint32_t base_address_hi:20; /* bits 51..32 of base address */
		uint32_t available:11;
		uint32_t no_execute:1;
	} hi;
} PACKED em64t_pml4_t, em64t_pdpe_t;

/*
 * Page Table Entry for 2MB pages
 */
typedef struct {
	struct {
		uint32_t present:1;
		uint32_t rw:1;
		uint32_t us:1;         /* user / supervisor */
		uint32_t pwt:1;        /* Page Write Through */
		uint32_t pcd:1;        /* Page Cache Disable */
		uint32_t accessed:1;
		uint32_t dirty:1;
		uint32_t pse:1;        /* must be set */
		uint32_t global:1;
		uint32_t avl:3;        /* available to software */
		uint32_t pat:1;
		uint32_t zeroes:8;
		uint32_t base_address_lo:11; /* bits 31..21 of base address (low bits are zeroes) */
	} lo;
	struct {
		uint32_t base_address_hi:20; /* bits 51..32 of base address */
		uint32_t available:11;
		uint32_t no_execute:1;
	} hi;
} PACKED em64t_pde_2mb_t;

/*
 * EM64T Interrupt Descriptor Table - Gate Descriptor
 */
typedef struct {
	/* offset 0 */
	uint32_t offset_0_15:16;       /* Offset bits 15..0 */
	uint32_t css:16;               /* Command Segment Selector */

	/* offset 4 */
	uint32_t ist:3;                /* interrupt Stack Table */
	uint32_t reserved_0:5;         /* reserved. must be zeroes */
	uint32_t gate_type:4;          /* Gate Type.  See #defines above */
	uint32_t reserved2_0:1;        /* must be zero */
	uint32_t dpl:2;                /* Descriptor Privilege Level must be zero */
	uint32_t present:1;            /* Segment Present Flag */
	uint32_t offset_15_31:16;      /* Offset bits 31..16 */

	/* offset 8 */
	uint32_t	offset_32_63; /* Offset bits 32..63 */

	/* offset 12 */
	uint32_t	reserved3;
} PACKED em64t_idt_gate_descriptor_t;

typedef em64t_idt_gate_descriptor_t em64t_idt_table_t[256];

typedef struct {
	uint16_t	limit;
	uint64_t	base;
} PACKED em64t_idt_descriptor_t;

/*
 * ia32_misc_enable_msr_t
 */
typedef union {
	struct {
		uint32_t fast_string_enable:1;
		uint32_t reserved0:1;
		uint32_t x87fpu_fopcode_compability_mode_enable:1;
		uint32_t thermal_monitor_enable:1;
		uint32_t split_lock_disable:1;
		uint32_t reserved1:1;
		uint32_t third_level_cache_disable:1;
		uint32_t performance_monitoring_available:1;
		uint32_t supress_lock_enable:1;
		uint32_t prefetch_queue_disable:1;
		uint32_t ferr_interrupt_reporting_enable:1;
		uint32_t branch_trace_storage_unavailable:1;
		uint32_t precise_event_based_sampling_unavailable:1;
		uint32_t reserved2:6;
		uint32_t adjacent_cache_line_prefetch_disable:1;
		uint32_t reserved3:4;
		uint32_t l1_data_cache_context_mode:1;
		uint32_t reserved4:7;
		uint32_t reserved5:32;
	} PACKED bits;
	uint32_t	uint32[2];
	uint64_t	uint64;
} PACKED ia32_misc_enable_msr_t;

/*
 * IA-32 MSR Register EFER (0xC0000080)
 */
#define EFER_SCE 0x00000001
#define EFER_LME 0x00000100
#define EFER_LMA 0x00000400
#define EFER_NXE 0x00000800

typedef union {
	struct {
		uint32_t sce:1;        /* (00) SysCall Enable/Disable (R/W) */
		uint32_t reserved_0:7;
		uint32_t lme:1;        /* (08) Long Mode Enable (IA-32e) (R/W) */
		uint32_t reserved_1:1;
		uint32_t lma:1;        /* (10) Long Mode Active (IA-32e) (R) */
		uint32_t nxe:1;        /* (11) Execute Disabled Enable (R/W) */
		uint32_t reserved_2:20;
		uint32_t reserved_3:32;
	} PACKED bits;
	struct {
		uint32_t	lower;
		uint32_t	upper;
	} PACKED uint32;
	uint64_t uint64;
} PACKED ia32_efer_t;

/* offset in the VMCS MsrBitmap structure for subset of MSR to force VmExit */
#define IA32_EFER_WRITE_MSR_VMCS_BITMAP_BYTES_OFFSET        0x80
#define IA32_EFER_WRITE_MSR_VMCS_BITMAP_BIT                 0x1

#define IA32_SIZE_OF_RDMSR_INST              2
#define IA32_SIZE_OF_WRMSR_INST              2

/*
 * Yonah/Merom specific MSRs
 */
/* #define IA32_PMG_IO_CAPTURE_INDEX 0xE4 */

typedef union {
	struct {
		uint32_t lvl2_base_address:16;
		uint32_t cst_range:7;
		uint32_t reserved_0:9;
		uint32_t reserved_1:32;
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_pmg_io_capture_msr_t;

typedef union {
	struct {
		uint32_t type:4;                 /* bits 3:0   */
		uint32_t s_bit:1;                /* bit  4     */
		uint32_t dpl:2;                  /* bits 6:5   */
		uint32_t p_bit:1;                /* bit  7     */
		uint32_t reserved_11_8:4;        /* bits 11:8  */
		uint32_t avl_bit:1;              /* bit  12    */
		uint32_t l_bit:1;                /* bit  13    */
		uint32_t db_bit:1;               /* bit  14    */
		uint32_t g_bit:1;                /* bit  15    */
		uint32_t null_bit:1;             /* bit  16    */
		uint32_t reserved_31_17:15;      /* bits 31:17 */
	} bits;
	uint32_t uint32;
} seg_ar_bits_t;

#endif   /* _EM64T_DEFS_H_ */
