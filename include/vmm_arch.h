/*******************************************************************************
* Copyright (c) 2017 Intel Corporation
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
#ifndef _VMM_ARCH_H_
#define _VMM_ARCH_H_

#include "vmm_base.h"

#define RFLAGS_CF (1ull << 0) /* Carry Flag */
#define RFLAGS_RSVD1 (1ull << 1) /* RSVD_1 */
#define RFLAGS_PF (1ull << 2) /* Parity Flag */
#define RFLAGS_AF (1ull << 4) /* Auxiliary Carry Flag */
#define RFLAGS_ZF (1ull << 6) /* Zero Flag */
#define RFLAGS_SF (1ull << 7) /* Sign Flag */
#define RFLAGS_TP (1ull << 8) /* Trap Flag */
#define RFLAGS_IF (1ull << 9) /* Interrupt Enable Flag */
#define RFLAGS_DF (1ull << 10) /* Direction Flag */
#define RFLAGS_OF (1ull << 11) /* Overflow Flag */
#define RFLAGS_NT (1ull << 14) /* Nexted Task */
#define RFLAGS_RF (1ull << 16) /* Resume Flag */
#define RFLAGS_VM (1ull << 17) /* Virtual 8086 Mode */
#define RFLAGS_AC (1ull << 18) /* Alignment Check */
#define RFLAGS_VIF (1ull << 19) /* Virtual Interrupt Flag */
#define RFLAGS_VIP (1ull << 20) /* Virtual Interrupt Pending */
#define RFLAGS_ID (1ull << 21) /* ID Flag */

#define RFLAGS_IOPL(rflags) (((rflags) >> 12) & 0x3ull)

/* CPUID bit support for h/w features */
/*eax = 1*/
#define CPUID_ECX_VMX        (1U << 5)  /* ecx bit 5 for VMX */
#define CPUID_ECX_SMX        (1U << 6)  /* ecx bit 6 for SMX */
#define CPUID_ECX_XSAVE      (1U << 26)
#define CPUID_EDX_SYSCALL_SYSRET (1U << 11) /* edx bit 11 for syscall/ret */
#define CPUID_EDX_PSE36      (1U << 17)
#define CPUID_EDX_FXSAVE     (1U << 24)
#define CPUID_EDX_HTT        (1U << 28)
/*eax = 7 ecx = 0*/
#define CPUID_EBX_TSC_ADJUST (1U << 1)
#define CPUID_EBX_SGX        (1U << 2)
#define CPUID_EBX_RTM        (1U << 11)
/*eax = 0xd ecx = 0*/
#define CPUID_XSS            (1ULL << 8)
/*eax = 0x80000001*/
#define CPUID_EDX_1G_PAGE    (1U << 26)
/* eax = 7, ecx = 0 */
#define CPUID_EDX_IBRS_IBPB  (1U << 26)
#define CPUID_EDX_L1D_FLUSH  (1U << 28)
#define CPUID_EDX_ARCH_CAP   (1U << 29)

/*
 * Descriptor for the Global Descriptor Table(GDT) and Interrupt Descriptor
 * Table(IDT)
 */

typedef struct {
	uint16_t	limit;
	uint32_t	base;
} PACKED gdtr32_t, idtr32_t;

typedef struct {
	uint16_t	limit;
	uint64_t	base;
} PACKED gdtr64_t, idtr64_t;

/*
 * IA-32 Control Register #0 (CR0)
 */
#define CR0_PE          (1ull << 0)
#define CR0_MP          (1ull << 1)
#define CR0_EM          (1ull << 2)
#define CR0_TS          (1ull << 3)
#define CR0_ET          (1ull << 4)
#define CR0_NE          (1ull << 5)
#define CR0_WP          (1ull << 16)
#define CR0_AM          (1ull << 18)
#define CR0_NW          (1ull << 29)
#define CR0_CD          (1ull << 30)
#define CR0_PG          (1ull << 31)

/*
 * IA-32 Control Register #4 (CR4)
 */
#define CR4_VME         (1ull << 0)
#define CR4_PVI         (1ull << 1)
#define CR4_TSD         (1ull << 2)
#define CR4_DE          (1ull << 3)
#define CR4_PSE         (1ull << 4)
#define CR4_PAE         (1ull << 5)
#define CR4_MCE         (1ull << 6)
#define CR4_PGE         (1ull << 7)
#define CR4_PCE         (1ull << 8)
#define CR4_OSFXSR      (1ull << 9)
#define CR4_OSXMMEXCPT  (1ull << 10)
#define CR4_UMIP        (1ull << 11)
#define CR4_VMXE        (1ull << 13)
#define CR4_SMXE        (1ull << 14)
#define CR4_PCIDE       (1ull << 17)
#define CR4_OSXSAVE     (1ull << 18)
#define CR4_SMAP        (1ull << 21)
#define CR4_PKE         (1ull << 22)

/*
 * VMCS Exit Reason Structure
 */
typedef union {
	struct {
		uint32_t basic_reason:16;
		uint32_t resv_16_29:14;
		uint32_t failed_vm_exit:1;
		uint32_t failed_vm_entry:1;
	} bits;
	uint32_t uint32;
} vmx_exit_reason_t;

/* enable non-standard bitfield */
/*
 * VMCS Exit Qualification
 */
typedef union {
	struct {
		uint64_t size:3;           /* 0=1 byte, 1=2 byte, 3=4 byte */
		uint64_t direction:1;      /* 0=Out, 1=In */
		uint64_t string:1;         /* 0=Not String, 1=String */
		uint64_t rep:1;            /* 0=Not REP, 1=REP */
		uint64_t op_encoding:1;    /* 0=DX, 1=Immediate */
		uint64_t resv_7_15:9;
		uint64_t port_number:16;
		uint64_t resv_32_63:32;
	} io_instruction;

	struct {
		uint64_t number:4;        /* CR#.  0 for CLTS and LMSW */
		uint64_t access_type:2;   /* 0=Move to CR, 1=Move from CR, 2=CLTS, 3=LMSW */
		uint64_t operand_type:1;  /* LMSW operand type: 0=Register 1=memory. For CLTS and MOV CR cleared to 0 */
		uint64_t resv_7:1;
		uint64_t move_gpr:4;      /* 0 for CLTR and LMSW.  0=EAX, 1=ECX, 2=EDX, 3=EBX, 4=ESP, 5=EBP, 6=ESI, 7=EDI */
		uint64_t reserved_3:4;
		uint64_t lmsw_data:16;    /* 0 for CLTS and Move to/from CR# */
		uint64_t resv_32_63:32;
	} cr_access;

	struct {
		uint64_t number:3;      /* DR# */
		uint64_t resv_3:1;
		uint64_t direction:1;   /* 0=Move to DR, 1= Move from DR */
		uint64_t resv_5_7:3;
		uint64_t move_gpr:4;    /* 0=EAX, 1=ECX, 2=EDX, 3=EBX, 4=ESP, 5=EBP, 6=ESI, 7=EDI */
		uint64_t resv_12_31:20;
		uint64_t resv_32_63:32;
	} dr_access;

	struct {
		uint64_t tss_selector:16;
		uint64_t resv_16_29:14;
		uint64_t source:2;        /* 0=CALL, 1=IRET, 2=JMP, 3=Task gate in IDT */
		uint64_t resv_32_63:32;
	} task_switch;

	struct {
		uint64_t vector:8;
		uint64_t resv_8_31:24;
		uint64_t resv_32_63:32;
	} sipi;

	struct {
		/* 1=Unsupported Sleep State,
		 * 2=PDPTR loading problem,
		 * 3=NMI injection problem,
		 * 4=Bad guest working VMCS pointer */
		uint64_t info;
	} failed_vmenter_guest_state;

	struct {
		uint64_t entry;
	} failed_vmenter_msr_loading;

	struct {
		/* 1=Storing Guest MSR,
		 * 2=Loading PDPTR,
		 * 3=Attempt to load null CS, SS, TR selector,
		 * 4=Loading host MSR */
		uint64_t info;
	} failed_vm_exit;

	struct {
		uint64_t r:1;
		uint64_t w:1;
		uint64_t x:1;
		uint64_t ept_r:1;
		uint64_t ept_w:1;
		uint64_t ept_x:1;
		uint64_t resv_6:1;
		uint64_t gaw_violation:1;
		uint64_t gla_validity:1;
		uint64_t resv_9_11:3;
		uint64_t nmi_unblocking:1;
		uint64_t resv_13_31:19;
		uint64_t resv_32_63:32;
	} ept_violation;

	struct {
		uint64_t break_points:4;
		uint64_t resv_4_12:9;
		uint64_t dbg_reg_access:1;
		uint64_t single_step:1;  /* Breakpoint on Single Instruction or Branch taken */
		uint64_t resv_15_31:17;
		uint64_t resv_32_63:32;
	} dbg_exception;

	struct {
		uint64_t scale:2;        /* Memory access index scale 0=1, 1=2, 2=4, 3=8 */
		uint64_t resv_2:1;   /* cleared to 0 */
		uint64_t reg1:4;         /* Memory access reg1 0=RAX, 1=RCX, 2=RDX, 3=RBX, 4=RSP, 5=RBP, 6=RSI, 7=RDI, 8-15=R8=R15 */
		uint64_t address_size:3; /* Memory access address size 0=16bit, 1=32bit, 2=64bit */
		uint64_t mem_reg:1;      /* 0=memory access 1=register access */
		uint64_t resv_11_14:4;   /* cleared to 0 */
		uint64_t seg_reg:3;      /* Memory access segment register 0=ES, 1=CS, 2=SS, 3=DS, 4=FS, 5=GS */
		uint64_t index_reg:4;    /* Memory access index register. Encoded like reg1 */
		uint64_t index_reg_invalid:1;  /* Memory access - index_reg is invalid (0=valid, 1=invalid) */
		uint64_t base_reg:4;     /* Memory access base register. Encoded like reg1 */
		uint64_t base_reg_invalid:1;   /* Memory access - base_reg is invalid (0=valid, 1=invalid) */
		uint64_t reg2:4;         /* Encoded like reg1. Undef on VMCLEAR, VMPTRLD, VMPTRST, and VMXON. */
		uint64_t resv_32_63:32;
	} vmx_instruction;

	struct {
		uint64_t offset:12; /* Offset of access within the APIC page */
		/* 0 = data read during instruction execution
		 * 1 = data write during instruction execution
		 * 2 = instruction fetch
		 * 3 = access (read or write) during event delivery */
		uint64_t access_type:4;
		uint64_t resv_16_31:16;
		uint64_t resv_32_63:32;
	} apic_access;

	uint64_t uint64;
} vmx_exit_qualification_t;

#define TASK_SWITCH_TYPE_CALL        0
#define TASK_SWITCH_TYPE_IRET        1
#define TASK_SWITCH_TYPE_JMP         2
#define TASK_SWITCH_TYPE_IDT         3

/*
 * VMCS VM Enter Interrupt Information
 */
typedef union {
	struct {
		uint32_t vector:8;
		/* 0=Ext Int, 1=Rsvd, 2=NMI, 3=Exception, 4=Soft INT,
		 * 5=Priv Soft Trap, 6=Unpriv Soft Trap, 7=Other */
		uint32_t interrupt_type:3;
		uint32_t deliver_code:1;  /* 0=Do not deliver, 1=Deliver */
		uint32_t resv_12_30:19;
		uint32_t valid:1;         /* 0=Not valid, 1=Valid. Must be checked first */
	} bits;
	uint32_t uint32;
} vmx_entry_info_t;

typedef union {
	struct {
		uint32_t vector:8;
		/* 0=Ext Int, 1=Rsvd, 2=NMI, 3=Exception, 4=Soft INT,
		 * 5=Priv Soft Trap, 6=Unpriv Soft Trap, 7=Other */
		uint32_t interrupt_type:3;
		/* 0=Not valid,
		 * 1=VM_EXIT_INFO_EXCEPTION_ERROR_CODE valid */
		uint32_t error_code_valid:1;
		/* 1=VmExit occured while executing IRET, with no IDT Vectoring */
		uint32_t nmi_unblocking_due_to_iret:1;
		uint32_t must_be_zero:18;
		uint32_t valid:1;
	} bits;
	uint32_t uint32;
} vmx_exit_interrupt_info_t;

/*
 * VMCS VM Exit IDT Vectoring
 */
typedef union {
	struct {
		uint32_t vector:8;
		/* 0=Ext Int, 1=Rsvd, 2=NMI, 3=Exception, 4=Soft INT,
		 * 5=Priv Soft Trap, 6=Unpriv Soft Trap, 7=Other */
		uint32_t interrupt_type:3;
		/* 0=Not valid,
		 * 1=VM_EXIT_INFO_IDT_VECTORING_ERROR_CODE valid */
		uint32_t error_code_valid:1;
		uint32_t must_be_zero:19;
		/* 0=Not valid, 1=Valid. Must be checked first. */
		uint32_t valid:1;
	} bits;
	uint32_t uint32;
} vmx_exit_idt_info_t;

typedef enum {
	VECTOR_TYPE_EXT_INT = 0,
	VECTOR_TYPE_RES = 1,
	VECTOR_TYPE_NMI = 2,
	VECTOR_TYPE_HW_EXCEPTION = 3,
	VECTOR_TYPE_SW_INT = 4,
	VECTOR_TYPE_PRI_SW_INT = 5,
	VECTOR_TYPE_SW_EXCEPTION = 6,
	VECTOR_TYPE_OTHER_EVE = 7
} vmx_vector_type_t;

/*
 * VMCS VM Exit Instruction Information
 */
typedef union {
	struct {
		uint32_t resv_0_6:7;   /* Undefined */
		uint32_t addr_size:3;    /* 0=16bit, 1=32bit, 2=64bit, other invalid */
		uint32_t resv_10_14:5;   /* Undefined */
		uint32_t seg_reg:3;      /* 0=ES, 1=CS, 2=SS, 3=DS, 4=FS, 5=GS, other invalid. Undef for INS */
		uint32_t resv_18_31:14;  /* Undefined */
	} ins_outs_instr;

	uint32_t uint32;
} vmx_exit_instr_info_t;

/*
 * Interruptibility state
 */
#define INTR_BLK_BY_STI           (1u << 0)
#define INTR_BLK_BY_MOV_SS        (1u << 1)
#define INTR_BLK_BY_SMI           (1u << 2)
#define INTR_BLK_BY_NMI           (1u << 3)
#define INTR_ENCLAVE_MODE         (1u << 4)

/*
 * VMCS Guest Sleep State
 */
typedef enum {
	ACTIVITY_STATE_ACTIVE = 0,
	ACTIVITY_STATE_HLT = 1,
	ACTIVITY_STATE_SHUTDOWN = 2,
	ACTIVITY_STATE_WAIT_FOR_SIPI = 3,
	ACTIVITY_STATE_COUNT
} activity_state_t;

/* Exceptions/Interrupts */
enum {
	EXCEPTION_DE = 0,	/*  0: Divide by zero*/
	EXCEPTION_DB,		/*  1: Debug Exception */
	EXCEPTION_NMI,		/*  2: Nonmaskable Interrupt */
	EXCEPTION_BP,		/*  3: Breakpoint */
	EXCEPTION_OF,		/*  4: Overflow */
	EXCEPTION_BR,		/*  5: Bound Range Exceeded */
	EXCEPTION_UD,		/*  6: Invalid Opcode */
	EXCEPTION_NM,		/*  7: Device Not Available */
	EXCEPTION_DF,		/*  8: Double Fault */
	EXCEPTION_09,		/*  9: Coprocessor Segment Overrun */
	EXCEPTION_TS,		/* 10: Invalid TSS */
	EXCEPTION_NP,		/* 11: Segment Not Present */
	EXCEPTION_SS,		/* 12: Stack Segment Fault */
	EXCEPTION_GP,		/* 13: General Protection Fault */
	EXCEPTION_PF,		/* 14: Page Fault */
	EXCEPTION_15,		/* 15: Reserved */
	EXCEPTION_MF,		/* 16: x87 FPU Floating-Point Error */
	EXCEPTION_AC,		/* 17: Alignment Check */
	EXCEPTION_MC,		/* 18: Machine Check */
	EXCEPTION_XM,		/* 19: SIMD Floating-Point Exception */
	EXCEPTION_VE,		/* 20: Virtualization Exception */
	EXCEPTION_COUNT
};

typedef enum {
	/* GP */
	REG_RAX = 0,
	REG_RCX,
	REG_RDX,
	REG_RBX,
	REG_RSP,
	REG_RBP,
	REG_RSI,
	REG_RDI,
	REG_R8,
	REG_R9,
	REG_R10,
	REG_R11,
	REG_R12,
	REG_R13,
	REG_R14,
	REG_R15,

	/* the count of GP registers */
	REG_GP_COUNT
} gp_reg_t;

typedef enum {
	/* general segments */
	SEG_CS = 0,
	SEG_DS,
	SEG_SS,
	SEG_ES,
	SEG_FS,
	SEG_GS,
	SEG_LDTR,
	SEG_TR,
	/* the count of general segments */
	SEG_COUNT
} seg_id_t;

typedef struct {
	uint64_t	base;           /* for real mode it should be selector << 4 */
	uint32_t	limit;
	uint32_t	attributes;     /* vmm_segment_attributes_t */
	uint16_t	selector;       /* for real mode this is the segment value */
	uint16_t	reserved[3];
} segment_t;

inline void fill_segment(segment_t *seg, uint64_t base, uint32_t limit, uint32_t attr, uint16_t sel)
{
	seg->base = base;
	seg->limit = limit;
	seg->attributes = attr;
	seg->selector = sel;
}

typedef uint8_t cache_type_t;

#define CACHE_TYPE_UC 0x0            /* Strong Uncacheable */
#define CACHE_TYPE_WC 0x1            /* Write Combining */
#define CACHE_TYPE_WT 0x4            /* Write Through */
#define CACHE_TYPE_WP 0x5            /* Write Protected */
#define CACHE_TYPE_WB 0x6            /* Write Back */
#define CACHE_TYPE_UC_MINUS 0x7      /* Uncacheable */
#define CACHE_TYPE_UNDEFINED 0xFF

/* Defined for mtrr precedences check */
#define CACHE_TYPE_ERROR 0xFE

/*
 * Standard MSR Indexes
 */
#define MSR_INVALID_INDEX          ((uint32_t)0xffffffff)
#define MSR_TIME_STAMP_COUNTER     ((uint32_t)0x010)
#define MSR_APIC_BASE              (0x01B)
#define MSR_FEATURE_CONTROL        ((uint32_t)0x03A)
#define MSR_TSC_ADJUST             ((uint32_t)0x03B)
#define MSR_TSC_DEADLINE           ((uint32_t)0x6E0)
#define MSR_SYSENTER_CS            ((uint32_t)0x174)
#define MSR_SYSENTER_ESP           ((uint32_t)0x175)
#define MSR_SYSENTER_EIP           ((uint32_t)0x176)
#define MSR_MISC_ENABLE            ((uint32_t)0x1A0)
#define MSR_DEBUGCTL               ((uint32_t)0x1D9)
#define MSR_PAT                    ((uint32_t)0x277)
#define MSR_PERF_GLOBAL_CTRL       ((uint32_t)0x38F)
#define MSR_EFER                   ((uint32_t)0xC0000080)

/*
 * MSRs for Side Channal Attack Dectection and Mitigation
 */
#define MSR_SPEC_CTRL              ((uint32_t)0x48)
#define MSR_PRED_CMD               ((uint32_t)0x49)
#define MSR_ARCH_CAP               ((uint32_t)0x10A)
#define MSR_FLUSH_CMD              ((uint32_t)0x10B)

/* IA-32 MSR Register SPEC_CTRL(0x48) */
#define SPEC_CTRL_IBRS             (1ull << 0)

/* IA-32 MSR Register PRED_CMD(0x49) */
#define PRED_CMD_IBPB              (1ull << 0)

/* IA-32 MSR Register ARCH_CAPABILITIES(0x10A) */
#define RDCL_NO                    (1ull << 0)
#define SKIP_L1DFL_VMENTRY         (1ull << 3)

/* IA-32 MSR Register FLUSH_CMD(0x10B) */
#define L1D_FLUSH                  (1ull << 0)

/*
 * IA-32 MSR Register EFER (0xC0000080)
 */
#define EFER_SCE        (1ull << 0)
#define EFER_LME        (1ull << 8)
#define EFER_LMA        (1ull << 10)
#define EFER_NXE        (1ull << 11)

#define MSR_STAR                   ((uint32_t)0xc0000081)	/* System Call Target Address */
#define MSR_LSTAR                  ((uint32_t)0xc0000082)	/* IA-32e Mode System Call Target Address */
#define MSR_FMASK                  ((uint32_t)0xc0000084)	/* System Call Flag Mask */

#define MSR_FS_BASE                ((uint32_t)0xC0000100)	/* Map of BASE address of FS */
#define MSR_GS_BASE                ((uint32_t)0xC0000101)	/* Map of BASE address of GS */
#define MSR_KERNEL_GS_BASE         ((uint32_t)0xc0000102)	/* Swap Target of BASE address of GS */

/*
 * VMCS MSR Entry Structure
 */
typedef struct {
	uint32_t	msr_index;
	uint32_t	reserved;
	uint64_t	msr_data;
} msr_list_t;

typedef union {
	struct {
		uint32_t type:4;                 /* bits 3:0   */
		uint32_t s_bit:1;                /* bit  4     */
		uint32_t dpl:2;                  /* bit2 6:5   */
		uint32_t p_bit:1;                /* bit  7     */
		uint32_t reserved_11_8:4;        /* bits 11:8  */
		uint32_t avl_bit:1;              /* bit  12    */
		uint32_t l_bit:1;                /* bit  13    */
		uint32_t db_bit:1;               /* bit  14    */
		uint32_t g_bit:1;                /* bit  15    */
		uint32_t null_bit:1;             /* bit  16    */
		uint32_t reserved_31_17:15;      /* bits 31:17 */
	} bits;
	uint32_t u32;
} seg_ar_t;

#endif
