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

/*
 * IA32 VMX Virtual Machine Control Structure Definitions
 */
#ifndef _VMX_VMCS_H_
#define _VMX_VMCS_H_

#include "mon_defs.h"


/*
 * VMCS bit positions for
 * Primary Processor-Based VM-Execution Controls
 */
#define PPBVMX_CTL_UNCONDITION_IO_EXIT    24
#define PPBVMX_CTL_USE_IO_BITMAPS         25

/*
 * VMCS Register Indexes
 */
#define VM_X_VPID                               0x00000000
#define VM_X_EPTP_INDEX                         0x00000004
#define VM_X_CONTROL_VECTOR_PIN_EVENTS          0x00004000
#define VM_X_CONTROL_VECTOR_PROCESSOR_EVENTS    0x00004002
#define VM_X_CONTROL2_VECTOR_PROCESSOR_EVENTS   0x0000401E
#define VM_X_EXCEPTION_BITMAP                   0x00004004
#define VM_X_CR3_TARGET_COUNT                   0x0000400A
#define VM_X_CR0_MASK                           0x00006000
#define VM_X_CR3_MASK                           0x00006208
#define VM_X_CR4_MASK                           0x00006002
#define VM_X_CR0_READ_SHADOW                    0x00006004
#define VM_X_CR3_READ_SHADOW                    0x0000620C
#define VM_X_CR4_READ_SHADOW                    0x00006006
#define VM_X_CR3_TARGET_VAL_BASE                0x00006008  /* 6008-6206 */
#define VM_X_CR3_TARGET_VALUE(_x)               \
	(VM_X_CR3_TARGET_VAL_BASE + (_x) * 2)
#define VM_X_PAGE_FAULT_ERROR_CODE_MASK         0x00004006
#define VM_X_PAGE_FAULT_ERROR_CODE_MATCH        0x00004008
#define VM_EXIT_CONTROL_VECTOR                  0x0000400C
#define VM_EXIT_TPR_THRESHOLD                   0x0000401C
#define VM_EXIT_MSR_STORE_COUNT                 0x0000400E
#define VM_EXIT_MSR_LOAD_COUNT                  0x00004010
#define VM_ENTER_CONTROL_VECTOR                 0x00004012
#define VM_ENTER_MSR_LOAD_COUNT                 0x00004014
#define VM_ENTER_INTERRUPT_INFO                 0x00004016
#define VM_ENTER_EXCEPTION_ERROR_CODE           0x00004018
#define VM_ENTER_INSTRUCTION_LENGTH             0x0000401A
#define VM_X_IO_BITMAP_ADDRESS_A                0x00002000
#define VM_X_IO_BITMAP_ADDRESS_A_HIGH           0x00002001
#define VM_X_IO_BITMAP_ADDRESS_B                0x00002002
#define VM_X_IO_BITMAP_ADDRESS_B_HIGH           0x00002003
#define VM_X_MSR_BITMAP_ADDRESS                 0x00002004
#define VM_X_MSR_BITMAP_ADDRESS_HIGH            0x00002005
#define VM_EXIT_MSR_STORE_ADDRESS               0x00002006
#define VM_EXIT_MSR_STORE_ADDRESS_HIGH          0x00002007
#define VM_EXIT_MSR_LOAD_ADDRESS                0x00002008
#define VM_EXIT_MSR_LOAD_ADDRESS_HIGH           0x00002009
#define VM_ENTER_MSR_LOAD_ADDRESS               0x0000200A
#define VM_ENTER_MSR_LOAD_ADDRESS_HIGH          0x0000200B
#define VM_X_OSV_CONTROLLING_VMCS_ADDRESS       0x0000200C
#define VM_X_OSV_CONTROLLING_VMCS_ADDRESS_HIGH  0x0000200D
#define VM_X_TSC_OFFSET                         0x00002010
#define VM_X_TSC_OFFSET_HIGH                    0x00002011
#define VM_X_VIRTUAL_APIC_ADDRESS               0x00002012
#define VM_X_VIRTUAL_APIC_ADDRESS_HIGH          0x00002013
#define VM_X_APIC_ACCESS_ADDRESS                0x00002014
#define VM_X_APIC_ACCESS_ADDRESS_HIGH           0x00002015

#define VM_X_VMFUNC_CONTROL                     0x00002018
#define VM_X_VMFUNC_CONTROL_HIGH                0x00002019
#define VM_X_VMFUNC_EPTP_LIST_ADDRESS           0x00002024
#define VM_X_VMFUNC_EPTP_LIST_ADDRESS_HIGH      0x00002025

#define VM_X_VE_INFO_ADDRESS                    0x0000202A
#define VM_X_VE_INFO_ADDRESS_HIGH               0x0000202B
#define VM_X_EPTP_ADDRESS                       0x0000201A
#define VM_X_EPTP_ADDRESS_HIGH                  0x0000201B
#define VM_X_PREEMTION_TIMER                    0x0000482E
#define VM_EXIT_PHYSICAL_ADDRESS                0x00002400
#define VM_EXIT_PHYSICAL_ADDRESS_HIGH           0x00002401
#define VM_EXIT_INFO_INSTRUCTION_ERROR_CODE     0x00004400
#define VM_EXIT_INFO_REASON                     0x00004402
#define VM_EXIT_INFO_EXCEPTION_INFO             0x00004404
#define VM_EXIT_INFO_EXCEPTION_ERROR_CODE       0x00004406
#define VM_EXIT_INFO_IDT_VECTORING              0x00004408
#define VM_EXIT_INFO_IDT_VECTORING_ERROR_CODE   0x0000440A
#define VM_EXIT_INFO_INSTRUCTION_LENGTH         0x0000440C
#define VM_EXIT_INFO_INSTRUCTION_INFO           0x0000440E
#define VM_EXIT_INFO_QUALIFICATION              0x00006400
#define VM_EXIT_INFO_IO_RCX                     0x00006402
#define VM_EXIT_INFO_IO_RSI                     0x00006404
#define VM_EXIT_INFO_IO_RDI                     0x00006406
#define VM_EXIT_INFO_IO_RIP                     0x00006408
#define VM_EXIT_INFO_GUEST_LINEAR_ADDRESS       0x0000640A
#define GUEST_CR0                               0x00006800
#define GUEST_CR3                               0x00006802
#define GUEST_CR4                               0x00006804
#define GUEST_DR7                               0x0000681A
#define GUEST_ES_SELECTOR                       0x00000800
#define GUEST_ES_BASE                           0x00006806
#define GUEST_ES_LIMIT                          0x00004800
#define GUEST_ES_AR                             0x00004814
#define GUEST_CS_SELECTOR                       0x00000802
#define GUEST_CS_BASE                           0x00006808
#define GUEST_CS_LIMIT                          0x00004802
#define GUEST_CS_AR                             0x00004816
#define GUEST_SS_SELECTOR                       0x00000804
#define GUEST_SS_BASE                           0x0000680A
#define GUEST_SS_LIMIT                          0x00004804
#define GUEST_SS_AR                             0x00004818
#define GUEST_DS_SELECTOR                       0x00000806
#define GUEST_DS_BASE                           0x0000680C
#define GUEST_DS_LIMIT                          0x00004806
#define GUEST_DS_AR                             0x0000481A
#define GUEST_FS_SELECTOR                       0x00000808
#define GUEST_FS_BASE                           0x0000680E
#define GUEST_FS_LIMIT                          0x00004808
#define GUEST_FS_AR                             0x0000481C
#define GUEST_GS_SELECTOR                       0x0000080A
#define GUEST_GS_BASE                           0x00006810
#define GUEST_GS_LIMIT                          0x0000480A
#define GUEST_GS_AR                             0x0000481E
#define GUEST_LDTR_SELECTOR                     0x0000080C
#define GUEST_LDTR_BASE                         0x00006812
#define GUEST_LDTR_LIMIT                        0x0000480C
#define GUEST_LDTR_AR                           0x00004820
#define GUEST_TR_SELECTOR                       0x0000080E
#define GUEST_TR_BASE                           0x00006814
#define GUEST_TR_LIMIT                          0x0000480E
#define GUEST_TR_AR                             0x00004822
#define GUEST_GDTR_BASE                         0x00006816
#define GUEST_GDTR_LIMIT                        0x00004810
#define GUEST_IDTR_BASE                         0x00006818
#define GUEST_IDTR_LIMIT                        0x00004812
#define GUEST_ESP                               0x0000681C
#define GUEST_EIP                               0x0000681E
#define GUEST_EFLAGS                            0x00006820
#define GUEST_PEND_DBE                          0x00006822
#define GUEST_WORKING_VMCS_PTR                  0x00002800
#define GUEST_WORKING_VMCS_PTR_HIGH             0x00002801
#define GUEST_DEBUG_CONTROL                     0x00002802
#define GUEST_DEBUG_CONTROL_HIGH                0x00002803
#define GUEST_INTERRUPTIBILITY                  0x00004824
#define GUEST_SLEEP_STATE                       0x00004826
#define GUEST_SMBASE                            0x00004828
#define GUEST_SYSENTER_CS                       0x0000482A
#define GUEST_SYSENTER_ESP                      0x00006824
#define GUEST_SYSENTER_EIP                      0x00006826
#define GUEST_PAT                               0x00002804
#define GUEST_PAT_HIGH                          0x00002805
#define GUEST_EFER                              0x00002806
#define GUEST_EFER_HIGH                         0x00002807
#define GUEST_IA32_PERF_GLOBAL_CTRL             0x00002808
#define GUEST_IA32_PERF_GLOBAL_CTRL_HIGH        0x00002809
#define GUEST_PDPTR0                            0x0000280A
#define GUEST_PDPTR0_HIGH                       0x0000280B
#define GUEST_PDPTR1                            0x0000280C
#define GUEST_PDPTR1_HIGH                       0x0000280D
#define GUEST_PDPTR2                            0x0000280E
#define GUEST_PDPTR2_HIGH                       0x0000280F
#define GUEST_PDPTR3                            0x00002810
#define GUEST_PDPTR3_HIGH                       0x00002811
#define HOST_CR0                                0x00006C00
#define HOST_CR3                                0x00006C02
#define HOST_CR4                                0x00006C04
#define HOST_ES_SELECTOR                        0x00000C00
#define HOST_CS_SELECTOR                        0x00000C02
#define HOST_SS_SELECTOR                        0x00000C04
#define HOST_DS_SELECTOR                        0x00000C06
#define HOST_FS_SELECTOR                        0x00000C08
#define HOST_FS_BASE                            0x00006C06
#define HOST_GS_SELECTOR                        0x00000C0A
#define HOST_GS_BASE                            0x00006C08
#define HOST_TR_SELECTOR                        0x00000C0C
#define HOST_TR_BASE                            0x00006C0A
#define HOST_GDTR_BASE                          0x00006C0C
#define HOST_IDTR_BASE                          0x00006C0E
#define HOST_ESP                                0x00006C14
#define HOST_EIP                                0x00006C16
#define HOST_SYSENTER_CS                        0x00004C00
#define HOST_SYSENTER_ESP                       0x00006C10
#define HOST_SYSENTER_EIP                       0x00006C12
#define HOST_PAT                                0x00002C00
#define HOST_PAT_HIGH                           0x00002C01
#define HOST_EFER                               0x00002C02
#define HOST_EFER_HIGH                          0x00002C03
#define HOST_IA32_PERF_GLOBAL_CTRL              0x00002C04
#define HOST_IA32_PERF_GLOBAL_CTRL_HIGH         0x00002C05
#define VMCS_NO_COMPONENT                       0x0000FFFF

/*
 * VMX Error Codes
 */
#define VMX_ARCH_NO_INSTRUCTION_ERROR                        0                  /* VMxxxxx */
#define VMX_ARCH_VMCALL_IN_ROOT_ERROR                        1                  /* VMCALL */
#define VMX_ARCH_VMCLEAR_INVALID_PHYSICAL_ADDRESS_ERROR      2                  /* VMCLEAR */
#define VMX_ARCH_VMCLEAR_WITH_CURRENT_CONTROLLING_PTR_ERROR  3                  /* VMCLEAR */
#define VMX_ARCH_VMLAUNCH_WITH_NON_CLEAR_VMCS_ERROR          4                  /* VMLAUNCH */
#define VMX_ARCH_VMRESUME_WITH_NON_LAUNCHED_VMCS_ERROR       5                  /* VMRESUME */
#define VMX_ARCH_VMRESUME_WITH_NON_CHILD_VMCS_ERROR          6                  /* VMRESUME */
#define VMX_ARCH_VMENTER_BAD_CONTROL_FIELD_ERROR             7                  /* VMENTER */
#define VMX_ARCH_VMENTER_BAD_MONITOR_STATE_ERROR             8                  /* VMENTER */
#define VMX_ARCH_VMPTRLD_INVALID_PHYSICAL_ADDRESS_ERROR      9                  /* VMPTRLD */
#define VMX_ARCH_VMPTRLD_WITH_CURRENT_CONTROLLING_PTR_ERROR  10                 /* VMPTRLD */
#define VMX_ARCH_VMPTRLD_WITH_BAD_REVISION_ID_ERROR          11                 /* VMPTRLD */
#define VMX_ARCH_VMREAD_OR_VMWRITE_OF_UNSUPPORTED_COMPONENT_ERROR 12            /* VMREAD */
#define VMX_ARCH_VMWRITE_OF_READ_ONLY_COMPONENT_ERROR        13                 /* VMWRITE */
#define VMX_ARCH_VMWRITE_INVALID_FIELD_VALUE_ERROR           14                 /* VMWRITE */
#define VMX_ARCH_VMXON_IN_VMX_ROOT_OPERATION_ERROR           15                 /* VMXON */
#define VMX_ARCH_VMENTRY_WITH_BAD_OSV_CONTROLLING_VMCS_ERROR 16                 /* VMENTER */
#define VMX_ARCH_VMENTRY_WITH_NON_LAUNCHED_OSV_CONTROLLING_VMCS_ERROR 17        /* VMENTER */
#define VMX_ARCH_VMENTRY_WITH_NON_ROOT_OSV_CONTROLLING_VMCS_ERROR 18            /* VMENTER */
#define VMX_ARCH_VMCALL_WITH_NON_CLEAR_VMCS_ERROR            19                 /* VMCALL */
#define VMX_ARCH_VMCALL_WITH_BAD_VMEXIT_FIELDS_ERROR         20                 /* VMCALL */
#define VMX_ARCH_VMCALL_WITH_INVALID_MSEG_MSR_ERROR          21                 /* VMCALL */
#define VMX_ARCH_VMCALL_WITH_INVALID_MSEG_REVISION_ERROR     22                 /* VMCALL */
#define VMX_ARCH_VMXOFF_WITH_CONFIGURED_SMM_MONITOR_ERROR    23                 /* VMXOFF */
#define VMX_ARCH_VMCALL_WITH_BAD_SMM_MONITOR_FEATURES_ERROR  24                 /* VMCALL */
#define VMX_ARCH_RETURN_FROM_SMM_WITH_BAD_VM_EXECUTION_CONTROLS_ERROR 25        /* Return from SMM */
#define VMX_ARCH_VMENTRY_WITH_EVENTS_BLOCKED_BY_MOV_SS_ERROR 26                 /* VMENTER */
#define VMX_ARCH_BAD_ERROR_CODE                              27                 /* Bad error code */
#define VMX_ARCH_INVALIDATION_WITH_INVALID_OPERAND           28                 /* INVEPT, INVVPID */

/*
 * Exception bitmap
 */
typedef union {
	struct {
		uint32_t de:1;                 /* Divide Error */
		uint32_t db:1;                 /* Debug */
		uint32_t nmi:1;                /* Non-Maskable Interrupt */
		uint32_t bp:1;                 /* Breakpoint */
		uint32_t of:1;                 /* Overflow */
		uint32_t br:1;                 /* BOUND Range Exceeded */
		uint32_t ud:1;                 /* Undefined Opcode */
		uint32_t nm:1;                 /* No Math Coprocessor */
		uint32_t df:1;                 /* Double Fault */
		uint32_t reserved_0:1;         /* reserved */
		uint32_t ts:1;                 /* Invalid TSS (Task Segment Selector) */
		uint32_t np:1;                 /* Segment Not Present */
		uint32_t ss:1;                 /* Stack Segment Fault */
		uint32_t gp:1;                 /* General Protection Fault */
		uint32_t pf:1;                 /* Page Fault */
		uint32_t reserved_1:1;         /* reserved */
		uint32_t mf:1;                 /* Math Fault */
		uint32_t ac:1;                 /* Alignment Check */
		uint32_t mc:1;                 /* Machine Check */
		uint32_t xf:1;                 /* SIMD Floating Point Numeric Error */
		uint32_t ve:1;                 /* Virtualization Exception */
		uint32_t reserved_2:11;        /* reserved */
	} PACKED bits;
	uint32_t uint32;
} PACKED ia32_vmcs_exception_bitmap_t;

/*
 * MSR bitmap offsets
 */

/* one bit per MSRs 0x00000000 - 0x00001FFF
 * bit == 1 - VmExit */
#define IA32_VMCS_MSR_BITMAP_READ_LOW_MSRS_OFFSET         0
#define IA32_VMCS_MSR_BITMAP_WRITE_LOW_MSRS_OFFSET        1024

/* one bit per MSRs 0xC0000000 - 0xC0001FFF
 * bit == 1 - VmExit */
#define IA32_VMCS_MSR_BITMAP_READ_HIGH_MSRS_OFFSET         2048
#define IA32_VMCS_MSR_BITMAP_WRITE_HIGH_MSRS_OFFSET        3072

/*
 * Maximum number of MSRs loaded or stored on VmEntry/VmExit
 */
#define IA32_VMCS_MAX_MSRS  32

/*
 * VMCS Data Structure
 */
typedef struct {
	uint32_t	revision_identifier;
	uint32_t	abort_indicator;
} PACKED ia32_vmx_vmcs_t;

/*
 * VMCS MSR Entry Structure
 */
typedef struct {
	uint32_t	msr_index;
	uint32_t	reserved;
	uint64_t	msr_data;
} PACKED ia32_vmx_msr_entry_t;

/*
 * VMCS Exit Reason Structure
 */
typedef union {
	struct {
		uint32_t basic_reason:16;
		uint32_t reserved:14;
		uint32_t failed_vm_exit:1;
		uint32_t failed_vm_entry:1;
	} PACKED bits;
	uint32_t uint32;
} PACKED ia32_vmx_exit_reason_t;

/*
 * VMCS Exit Reason - Basic Reason
 * If change this enum, please also change
 * gunnison\ia32emulator\emulator_private_types.h
 */
typedef enum {
	IA32_VMX_EXIT_BASIC_REASON_SOFTWARE_INTERRUPT_EXCEPTION_NMI = 0,
	IA32_VMX_EXIT_BASIC_REASON_HARDWARE_INTERRUPT = 1,
	IA32_VMX_EXIT_BASIC_REASON_TRIPLE_FAULT = 2,
	IA32_VMX_EXIT_BASIC_REASON_INIT_EVENT = 3,
	IA32_VMX_EXIT_BASIC_REASON_SIPI_EVENT = 4,
	IA32_VMX_EXIT_BASIC_REASON_SMI_IO_EVENT = 5,
	IA32_VMX_EXIT_BASIC_REASON_SMI_OTHER_EVENT = 6,
	IA32_VMX_EXIT_BASIC_REASON_PENDING_INTERRUPT = 7,
	IA32_VMX_EXIT_NMI_WINDOW = 8,
	IA32_VMX_EXIT_BASIC_REASON_TASK_SWITCH = 9,
	IA32_VMX_EXIT_BASIC_REASON_CPUID_INSTRUCTION = 10,
	IA32_VMX_EXIT_BASIC_REASON_GETSEC_INSTRUCTION = 11,
	IA32_VMX_EXIT_BASIC_REASON_HLT_INSTRUCTION = 12,
	IA32_VMX_EXIT_BASIC_REASON_INVD_INSTRUCTION = 13,
	IA32_VMX_EXIT_BASIC_REASON_INVLPG_INSTRUCTION = 14,
	IA32_VMX_EXIT_BASIC_REASON_RDPMC_INSTRUCTION = 15,
	IA32_VMX_EXIT_BASIC_REASON_RDTSC_INSTRUCTION = 16,
	IA32_VMX_EXIT_BASIC_REASON_RSM_INSTRUCTION = 17,
	IA32_VMX_EXIT_BASIC_REASON_VMCALL_INSTRUCTION = 18,
	IA32_VMX_EXIT_BASIC_REASON_VMCLEAR_INSTRUCTION = 19,
	IA32_VMX_EXIT_BASIC_REASON_VMLAUNCH_INSTRUCTION = 20,
	IA32_VMX_EXIT_BASIC_REASON_VMPTRLD_INSTRUCTION = 21,
	IA32_VMX_EXIT_BASIC_REASON_VMPTRST_INSTRUCTION = 22,
	IA32_VMX_EXIT_BASIC_REASON_VMREAD_INSTRUCTION = 23,
	IA32_VMX_EXIT_BASIC_REASON_VMRESUME_INSTRUCTION = 24,
	IA32_VMX_EXIT_BASIC_REASON_VMWRITE_INSTRUCTION = 25,
	IA32_VMX_EXIT_BASIC_REASON_VMXOFF_INSTRUCTION = 26,
	IA32_VMX_EXIT_BASIC_REASON_VMXON_INSTRUCTION = 27,
	IA32_VMX_EXIT_BASIC_REASON_CR_ACCESS = 28,
	IA32_VMX_EXIT_BASIC_REASON_DR_ACCESS = 29,
	IA32_VMX_EXIT_BASIC_REASON_IO_INSTRUCTION = 30,
	IA32_VMX_EXIT_BASIC_REASON_MSR_READ = 31,
	IA32_VMX_EXIT_BASIC_REASON_MSR_WRITE = 32,
	IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_GUEST_STATE = 33,
	IA32_VMX_EXIT_BASIC_REASON_FAILED_VMENTER_MSR_LOADING = 34,
	IA32_VMX_EXIT_BASIC_REASON_FAILED_VMEXIT = 35,
	IA32_VMX_EXIT_BASIC_REASON_MWAIT_INSTRUCTION = 36,
	IA32_VMX_EXIT_BASIC_REASON_MONITOR_TRAP_FLAG = 37,
	IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_38 = 38,
	IA32_VMX_EXIT_BASIC_REASON_MONITOR = 39,
	IA32_VMX_EXIT_BASIC_REASON_PAUSE = 40,
	IA32_VMX_EXIT_BASIC_REASON_FAILURE_DUE_MACHINE_CHECK = 41,
	IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_42 = 42,
	IA32_VMX_EXIT_BASIC_REASON_TPR_BELOW_THRESHOLD = 43,
	IA32_VMX_EXIT_BASIC_REASON_APIC_ACCESS = 44,
	IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_45 = 45,
	IA32_VMX_EXIT_BASIC_REASON_GDTR_LDTR_ACCESS = 46,
	IA32_VMX_EXIT_BASIC_REASON_LDTR_TR_ACCESS = 47,
	IA32_VMX_EXIT_BASIC_REASON_EPT_VIOLATION = 48,
	IA32_VMX_EXIT_BASIC_REASON_EPT_MISCONFIGURATION = 49,
	IA32_VMX_EXIT_BASIC_REASON_INVEPT_INSTRUCTION = 50,
	IA32_VMX_EXIT_BASIC_REASON_RDTSCP_INSTRUCTION = 51,
	IA32_VMX_EXIT_BASIC_REASON_PREEMPTION_TIMER_EXPIRED = 52,
	IA32_VMX_EXIT_BASIC_REASON_INVVPID_INSTRUCTION = 53,
	IA32_VMX_EXIT_BASIC_REASON_INVALID_VMEXIT_REASON_54 = 54,
	IA32_VMX_EXIT_BASIC_REASON_XSETBV_INSTRUCTION = 55,

	IA32_VMX_EXIT_BASIC_REASON_PLACE_HOLDER_1 = 56,
	IA32_VMX_EXIT_BASIC_REASON_PLACE_HOLDER_2 = 57,
	IA32_VMX_EXIT_BASIC_REASON_PLACE_HOLDER_3 = 58,
	IA32_VMX_EXIT_BASIC_REASON_INVALID_VMFUNC = 59,

	IA32_VMX_EXIT_BASIC_REASON_COUNT = 60,
} ia32_vmx_exit_basic_reason_t;

/* enable non-standard bitfield */
/*
 * VMCS Exit Qualification
 */
typedef union {
	struct {
		uint32_t size:3;           /* 0=1 byte, 1=2 byte, 3=4 byte */
		uint32_t direction:1;      /* 0=Out, 1=In */
		uint32_t string:1;         /* 0=Not String, 1=String */
		uint32_t rep:1;            /* 0=Not REP, 1=REP */
		uint32_t op_encoding:1;    /* 0=DX, 1=Immediate */
		uint32_t reserved_9:9;
		uint32_t port_number:16;
		uint32_t reserved_32_64:32;
	} PACKED io_instruction;

	struct {
		uint32_t number:4;        /* CR#.  0 for CLTS and LMSW */
		uint32_t access_type:2;   /* 0=Move to CR, 1=Move from CR, 2=CLTS, 3=LMSW */
		uint32_t operand_type:1;  /* LMSW operand type: 0=Register 1=memory. For CLTS and MOV CR cleared to 0 */
		uint32_t reserved_2:1;
		uint32_t move_gpr:4;      /* 0 for CLTR and LMSW.  0=EAX, 1=ECX, 2=EDX, 3=EBX, 4=ESP, 5=EBP, 6=ESI, 7=EDI */
		uint32_t reserved_3:4;
		uint32_t lmsw_data:16;    /* 0 for CLTS and Move to/from CR# */
		uint32_t reserved_32_64;
	} PACKED cr_access;

	struct {
		uint32_t number:3;      /* DR# */
		uint32_t reserved_4:1;
		uint32_t direction:1;   /* 0=Move to DR, 1= Move from DR */
		uint32_t reserved_5:3;
		uint32_t move_gpr:4;    /* 0=EAX, 1=ECX, 2=EDX, 3=EBX, 4=ESP, 5=EBP, 6=ESI, 7=EDI */
		uint32_t reserved_12_31:20;
		uint32_t reserved_32_63;
	} PACKED dr_access;

	struct {
		uint32_t tss_selector:16;
		uint32_t reserved_7:14;
		uint32_t source:2;        /* 0=CALL, 1=IRET, 2=JMP, 3=Task gate in IDT */
		uint32_t reserved_32_63;
	} PACKED task_switch;

	struct {
		uint32_t vector:8;
		uint32_t reserved_8_31:24;
		uint32_t reserved_32_63;
	} PACKED sipi;

	struct {
		uint64_t address;
	} PACKED invlpg_instruction;

	struct {
		uint64_t address;
	} PACKED page_fault;

	struct {
		/* 1=Unsupported Sleep State,
		 * 2=PDPTR loading problem,
		 * 3=NMI injection problem,
		 * 4=Bad guest working VMCS pointer */
		uint64_t info;
	} PACKED failed_vmenter_guest_state;

	struct {
		uint64_t entry;
	} PACKED failed_vmenter_msr_loading;

	struct {
		/* 1=Storing Guest MSR,
		 * 2=Loading PDPTR,
		 * 3=Attempt to load null CS, SS, TR selector,
		 * 4=Loading host MSR */
		uint64_t info;
	} PACKED failed_vm_exit;

	struct {
		uint32_t r:1;
		uint32_t w:1;
		uint32_t x:1;
		uint32_t ept_r:1;
		uint32_t ept_w:1;
		uint32_t ept_x:1;
		uint32_t reserved_6:1;
		uint32_t gaw_violation:1;
		uint32_t gla_validity:1;
		uint32_t reserved_9_11:3;
		uint32_t nmi_unblocking:1;
		uint32_t reserved_13_31:19;
		uint32_t reserved_32_64;
	} PACKED ept_violation;

	struct {
		uint32_t break_points:4;
		uint32_t reserved:9;
		uint32_t dbg_reg_access:1;
		uint32_t single_step:1;  /* Breakpoint on Single Instruction or Branch taken */
		uint32_t reserved2:17;
		uint32_t reserved3;
	} PACKED dbg_exception;

	struct {
		uint32_t scale:2;        /* Memory access index scale 0=1, 1=2, 2=4, 3=8 */
		uint32_t reserved_0:1;   /* cleared to 0 */
		uint32_t reg1:4;         /* Memory access reg1 0=RAX, 1=RCX, 2=RDX, 3=RBX, 4=RSP, 5=RBP, 6=RSI, 7=RDI, 8-15=R8=R15 */
		uint32_t address_size:3; /* Memory access address size 0=16bit, 1=32bit, 2=64bit */
		uint32_t mem_reg:1;      /* 0=memory access 1=register access */
		uint32_t reserved_1:4;   /* cleared to 0 */
		uint32_t seg_reg:3;      /* Memory access segment register 0=ES, 1=CS, 2=SS, 3=DS, 4=FS, 5=GS */
		uint32_t index_reg:4;    /* Memory access index register. Encoded like reg1 */
		uint32_t index_reg_invalid:1;  /* Memory access - index_reg is invalid (0=valid, 1=invalid) */
		uint32_t base_reg:4;     /* Memory access base register. Encoded like reg1 */
		uint32_t base_reg_invalid:1;   /* Memory access - base_reg is invalid (0=valid, 1=invalid) */
		uint32_t reg2:4;         /* Encoded like reg1. Undef on VMCLEAR, VMPTRLD, VMPTRST, and VMXON. */
		uint32_t reserved_32_64:32;
	} PACKED vmx_instruction;

	struct {
		uint32_t offset:12; /* Offset of access within the APIC page */
		/* 0 = data read during instruction execution
		 * 1 = data write during instruction execution
		 * 2 = instruction fetch
		 * 3 = access (read or write) during event delivery */
		uint32_t access_type:4;
		uint32_t reserved1:16;
		uint32_t reserved2;
	} PACKED apic_access;

	uint64_t uint64;
} PACKED ia32_vmx_exit_qualification_t;

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
		uint32_t reserved:19;
		uint32_t valid:1;         /* 0=Not valid, 1=Valid. Must be checked first */
	} PACKED bits;
	uint32_t uint32;
} PACKED ia32_vmx_vmcs_vmenter_interrupt_info_t;

/*
 * VMCS VM Exit Interrupt Information
 */
typedef enum {
	/* 1,4,5,7 are not used */
	VMEXIT_INTERRUPT_TYPE_EXTERNAL_INTERRUPT = 0,
	VMEXIT_INTERRUPT_TYPE_NMI = 2,
	VMEXIT_INTERRUPT_TYPE_EXCEPTION = 3,
	VMEXIT_INTERRUPT_TYPE_SOFTWARE_EXCEPTION = 6,
} ia32_vmx_vmcs_vmexit_info_interrupt_info_interrupt_type_t;

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
	} PACKED bits;
	uint32_t uint32;
} PACKED ia32_vmx_vmcs_vmexit_info_interrupt_info_t;

/*
 * VMCS VM Enter Interrupt Information
 */
typedef enum {
	VMENTER_INTERRUPT_TYPE_EXTERNAL_INTERRUPT = 0,
	VMENTER_INTERRUPT_TYPE_RESERVED = 1,
	VMENTER_INTERRUPT_TYPE_NMI = 2,
	VMENTER_INTERRUPT_TYPE_HARDWARE_EXCEPTION = 3,
	VMENTER_INTERRUPT_TYPE_SOFTWARE_INTERRUPT = 4,
	VMENTER_INTERRUPT_TYPE_PRIVILEGED_SOFTWARE_INTERRUPT = 5,
	VMENTER_INTERRUPT_TYPE_SOFTWARE_EXCEPTION = 6,
	VMENTER_INTERRUPT_TYPE_OTHER_EVENT = 7
} ia32_vmx_vmcs_vmenter_info_interrupt_info_interrupt_type_t;

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
	} PACKED bits;
	uint32_t uint32;
} PACKED ia32_vmx_vmcs_vmexit_info_idt_vectoring_t;

typedef enum {
	/* 1, 5, 7 Not used */
	IDT_VECTORING_INTERRUPT_TYPE_EXTERNAL_INTERRUPT = 0,
	IDT_VECTORING_INTERRUPT_TYPE_NMI = 2,
	IDT_VECTORING_INTERRUPT_TYPE_EXCEPTION = 3,
	IDT_VECTORING_INTERRUPT_TYPE_SOFTWARE_INTERRUPT = 4,
	IDT_VECTORING_INTERRUPT_TYPE_PRIVILEGED_SOFTWARE_INTERRUPT = 5,
	IDT_VECTORING_INTERRUPT_TYPE_SOFTWARE_EXCEPTION = 6,
} ia32_vmx_vmcs_idt_vectoring_info_interrupt_info_interrupt_type_t;

#define IS_SOFTWARE_VECTOR(vec) \
	( \
		(vec.bits.valid != 0) && \
		((vec.bits.interrupt_type == \
		  IDT_VECTORING_INTERRUPT_TYPE_SOFTWARE_INTERRUPT) || \
		 (vec.bits.interrupt_type == \
		  IDT_VECTORING_INTERRUPT_TYPE_PRIVILEGED_SOFTWARE_INTERRUPT) || \
		 (vec.bits.interrupt_type == \
		  IDT_VECTORING_INTERRUPT_TYPE_SOFTWARE_EXCEPTION) \
		) \
	)

/*
 * Error Code for page fault
 */
typedef union {
	struct {
		uint32_t present:1;
		uint32_t is_write:1;
		uint32_t is_user:1;
		uint32_t is_rsvd:1;
		uint32_t instruction_fetch:1;
		uint32_t reserved:27;
	} PACKED bits;
	uint32_t uint32;
} PACKED ia32_vmx_vmcs_vmexit_error_code_t;

/*
 * VMCS VM Exit Instruction Information
 */
typedef union {
	struct {
		uint32_t scaling:2;                 /* 0=None, 1=By 2, 2=By 4, 3=By 8. Must be 0 */
		uint32_t reserved_0:1;              /* Must be 0 */
		uint32_t register1:4;               /* 0=EAX, 1=ECX, 2=EDX, 3=EBX, 4=ESP, 5=EBP, 6=ESI, 7=EDI, 8-15=R8-R15 */
		uint32_t address_size:3;            /* 0=16-bit, 1=32-bit */
		uint32_t register_memory:1;         /* 0=Memory, 1=Register */
		uint32_t operand_size:2;            /* 0=16-bit, 1=32-bit, 2=64-bit */
		uint32_t reserved_2:2;              /* Must be 0 */
		uint32_t segment:3;                 /* 0=ES, 1=CS, 2=SS, 3=DS, 4=FS, 5=GS */
		uint32_t index_register:4;          /* 0=EAX, 1=ECX, 2=EDX, 3=EBX, 4=ESP, 5=EBP, 6=ESI, 7=EDI, 8-15=R8-R15 */
		uint32_t index_register_invalid:1;  /* 0=Valid, 1=Invalid */
		uint32_t base_register:4;           /* 0=EAX, 1=ECX, 2=EDX, 3=EBX, 4=ESP, 5=EBP, 6=ESI, 7=EDI, 8-15=R8-R15 */
		uint32_t base_register_invalid:1;   /* 0=Valid, 1=Invalid */
		uint32_t register2:4;               /* 0=EAX, 1=ECX, 2=EDX, 3=EBX, 4=ESP, 5=EBP, 6=ESI, 7=EDI, 8-15=R8-R15 */
	} PACKED bits;

	struct {
		uint32_t reserved_0:7;   /* Undefined */
		uint32_t addr_size:3;    /* 0=16bit, 1=32bit, 2=64bit, other invalid */
		uint32_t reserved_1:5;   /* Undefined */
		uint32_t seg_reg:3;      /* 0=ES, 1=CS, 2=SS, 3=DS, 4=FS, 5=GS, other invalid. Undef for INS */
		uint32_t reserved_2:14;  /* Undefined */
	} PACKED ins_outs_instruction;

	uint32_t uint32;
} PACKED ia32_vmx_vmcs_vmexit_info_instruction_info_t;

/*
 * VMCS Guest AR
 */
typedef union {
	struct {
		uint32_t segment_type:4;
		uint32_t descriptor_type:1; /* 0=System, 1=Code/Data */
		uint32_t descriptor_privilege_level:2;
		uint32_t segment_present:1;
		uint32_t reserved_0:4;
		uint32_t available:1;
		uint32_t reserved_1:1;
		uint32_t default_operation_size:1; /* 0=16-bit segment, 1=32-bit segment */
		uint32_t granularity:1;
		uint32_t null:1;
		uint32_t reserved_2:15;
	} PACKED bits;
	uint32_t uint32;
} PACKED ia32_vmx_vmcs_guest_ar_t;

/*
 * VMCS Guest Interruptibility
 */
typedef union {
	struct {
		uint32_t block_next_instruction:1;
		uint32_t block_stack_segment:1;
		uint32_t block_smi:1;
		uint32_t block_nmi:1;
		uint32_t reserved_0:28;
	} PACKED bits;
	uint32_t uint32;
} PACKED ia32_vmx_vmcs_guest_interruptibility_t;

/*
 * VMCS Guest Sleep State
 */
typedef enum {
	IA32_VMX_VMCS_GUEST_SLEEP_STATE_ACTIVE = 0,
	IA32_VMX_VMCS_GUEST_SLEEP_STATE_HLT = 1,
	IA32_VMX_VMCS_GUEST_SLEEP_STATE_TRIPLE_FAULT_SHUTDOWN = 2,
	IA32_VMX_VMCS_GUEST_SLEEP_STATE_WAIT_FOR_SIPI = 3
} ia32_vmx_vmcs_guest_sleep_state_t;

/*
 * IA32 MSEG header
 */
typedef struct {
	uint32_t	revision;
	uint32_t	smm_monitor_features;
	uint32_t	gdtr_limit;
	uint32_t	gdtr_base_offset;
	uint32_t	cs;
	uint32_t	eip;
	uint32_t	esp;
	uint32_t	cr3_offset;
} PACKED ia32_mseg_header_t;


#endif   /* _VMX_VMCS_H_ */
