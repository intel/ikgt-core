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
 * IA32 VMX Read Only MSR Definitions */

#ifndef _VMX_CTRL_MSRS_H_
#define _VMX_CTRL_MSRS_H_

#include "mon_defs.h"
#include "em64t_defs.h"

/*
 * VMX Capabilities are declared in bit 5 of ECX retured from CPUID
 */
#define IA32_CPUID_ECX_VMX                                    0x20

/*
 * VMX MSR Indexes
 */
#define IA32_MSR_OPT_IN_INDEX                                 0x3A
#define IA32_MSR_MSEG_INDEX                                   0x9B
#define IA32_MSR_VMX_BASIC_INDEX                              0x480
#define IA32_MSR_PIN_BASED_VM_EXECUTION_CONTROLS_INDEX        0x481
#define IA32_MSR_PROCESSOR_BASED_VM_EXECUTION_CONTROLS_INDEX  0x482
#define IA32_MSR_PROCESSOR_BASED_VM_EXECUTION_CONTROLS2_INDEX 0x48B
#define IA32_MSR_VM_EXIT_CONTROLS_INDEX                       0x483
#define IA32_MSR_VM_ENTRY_CONTROLS_INDEX                      0x484
#define IA32_MSR_MISCELLANEOUS_DATA_INDEX                     0x485
#define IA32_MSR_CR0_ALLOWED_ZERO_INDEX                       0x486
#define IA32_MSR_CR0_ALLOWED_ONE_INDEX                        0x487
#define IA32_MSR_CR4_ALLOWED_ZERO_INDEX                       0x488
#define IA32_MSR_CR4_ALLOWED_ONE_INDEX                        0x489
#define IA32_MSR_VMX_VMCS_ENUM                                0x48A
#define IA32_MSR_EPT_VPID_CAP_INDEX                           0x48C
#define IA32_MSR_TRUE_PINBASED_CTLS_INDEX                     0x48D
#define IA32_MSR_TRUE_PROCBASED_CTLS_INDEX                    0x48E
#define IA32_MSR_TRUE_EXIT_CTLS_INDEX                         0x48F
#define IA32_MSR_TRUE_ENTRY_CTLS_INDEX                        0x490
#define IA32_MSR_VMX_VMFUNC_CTRL                              0x491

#define IA32_MSR_VMX_FIRST                              0x480

#define IA32_MSR_VMX_LAST                               0x491

/* synonyms */
#define IA32_MSR_VMCS_REVISION_IDENTIFIER_INDEX IA32_MSR_VMX_BASIC_INDEX


/*
 * VMX MSR Structure - IA32_MSR_OPT_IN_INDEX - Index 0x3A
 */
typedef union {
	struct {
		uint32_t lock:1;                     /* 0=Unlocked, 1=Locked */
		uint32_t enable_vmxon_in_smx:1;      /* 0=Disabled, 1=Enabled */
		uint32_t enable_vmxon_outside_smx:1; /* 0=Disabled, 1=Enabled */
		uint32_t reserved_0:5;
		uint32_t senter_enables:8;
		uint32_t reserved_1:16;
		uint32_t reserved_2:32;
	} PACKED bits;
	struct {
		uint32_t lower;
		uint32_t upper;
	} PACKED uint32;
	uint64_t uint64;
} PACKED ia32_msr_opt_in_t;

/*
 * VMX MSR Structure - IA32_MSR_MSEG_INDEX - Index 0x9B
 */
typedef union {
	struct {
		uint32_t valid:1; /* 0=Invalid, 1=Valid */
		uint32_t reserved_0:11;
		uint32_t mseg_base_address:20;
		uint32_t reserved_1:32;
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_msr_mseg_t;

/*
 * VMX MSR Structure - IA32_MSR_VMCS_REVISION_IDENTIFIER_INDEX - Index 0x480
 */
typedef union {
	struct {
		uint32_t revision_identifier:32;                       /* bits 0-31 */
		uint32_t vmcs_region_size:13;                          /* bits 32-44 */
		uint32_t reserved1_0:3;                                /* bits 45-47 */
		uint32_t physical_address_width:1;                     /* bit 48 */
		uint32_t dual_monitor_system_management_interrupts:1;  /* bit 49 */
		uint32_t vmcs_memory_type:4;                           /* bits 50:53 */
		uint32_t vmcs_instruction_info_field_on_io_is_valid:1; /* bit 54 */
		uint32_t reserved2_0:9;                                /* bits 55-63 */
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_msr_vmcs_revision_identifier_t;

/*
 * VMX MSR Structure - IA32_MSR_PIN_BASED_VM_EXECUTION_CONTROLS_INDEX - Index
 * 0x481
 */
typedef union {
	struct {
		uint32_t external_interrupt:1; /* 0=No VmExit from ext int */
		uint32_t host_interrupt:1;
		uint32_t init:1;
		uint32_t nmi:1;
		uint32_t sipi:1;
		uint32_t virtual_nmi:1;
		uint32_t vmx_timer:1;
		uint32_t reserved_1:25;
	} PACKED bits;
	uint32_t uint32;
} PACKED pin_based_vm_execution_controls_t;

typedef union {
	struct {
		/* bits, that have 0 values may be set to 0 in VMCS */
		pin_based_vm_execution_controls_t	may_be_set_to_zero;
		/* bits, that have 1 values may be set to 1 in VMCS */
		pin_based_vm_execution_controls_t	may_be_set_to_one;
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_msr_pin_based_vm_execution_controls_t;

/*
 * VMX MSR Structure - IA32_MSR_PROCESSOR_BASED_VM_EXECUTION_CONTROLS_INDEX -
 * Index 0x482
 */
typedef union {
	struct {
		uint32_t software_interrupt:1;
		uint32_t triple_fault:1;
		uint32_t virtual_interrupt:1; /* InterruptWindow */
		uint32_t use_tsc_offsetting:1;
		uint32_t task_switch:1;
		uint32_t cpuid:1;
		uint32_t get_sec:1;
		uint32_t hlt:1;
		uint32_t invd:1;
		uint32_t invlpg:1;
		uint32_t mwait:1;
		uint32_t rdpmc:1;
		uint32_t rdtsc:1;
		uint32_t rsm:1;
		uint32_t vm_instruction:1;
		uint32_t cr3_load:1;
		uint32_t cr3_store:1;
		uint32_t use_cr3_mask:1;
		uint32_t use_cr3_read_shadow:1;
		uint32_t cr8_load:1;
		uint32_t cr8_store:1;
		uint32_t tpr_shadow:1;
		uint32_t nmi_window:1;
		uint32_t mov_dr:1;
		uint32_t unconditional_io:1;
		uint32_t activate_io_bitmaps:1;
		uint32_t msr_protection:1;
		uint32_t monitor_trap_flag:1;
		uint32_t use_msr_bitmaps:1;
		uint32_t monitor:1;
		uint32_t pause:1;
		uint32_t secondary_controls:1;
	} PACKED bits;
	uint32_t uint32;
} PACKED processor_based_vm_execution_controls_t;

typedef union {
	struct {
		/* bits, that have 0 values may be set to 0 in VMCS */
		processor_based_vm_execution_controls_t may_be_set_to_zero;
		/* bits, that have 1 values may be set to 1 in VMCS */
		processor_based_vm_execution_controls_t may_be_set_to_one;
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_msr_processor_based_vm_execution_controls_t;

/*
 * VMX MSR Structure - IA32_MSR_PROCESSOR_BASED_VM_EXECUTION_CONTROLS2_INDEX -
 * Index 0x48B
 */
typedef union {
	struct {
		uint32_t virtualize_apic:1;
		uint32_t enable_ept:1;
		uint32_t descriptor_table_exiting:1;
		uint32_t enable_rdtscp:1;
		uint32_t shadow_apic_msrs:1;
		uint32_t enable_vpid:1;
		uint32_t wbinvd:1;
		uint32_t unrestricted_guest:1;
		uint32_t reserved_0:4;
		uint32_t enable_invpcid:1;
		uint32_t vmfunc:1;     /* bit 13 */
		uint32_t reserved_1:4;
		uint32_t ve:1;         /* bit 18 */
		uint32_t reserved_2:13;
	} PACKED bits;
	uint32_t uint32;
} PACKED processor_based_vm_execution_controls2_t;

typedef union {
	struct {
		/* bits, that have 0 values may be set to 0 in VMCS */
		processor_based_vm_execution_controls2_t
			may_be_set_to_zero;
		/* bits, that have 1 values may be set to 1 in VMCS */
		processor_based_vm_execution_controls2_t
			may_be_set_to_one;
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_msr_processor_based_vm_execution_controls2_t;

/*
 * VMX MSR Structure - IA32_MSR_VM_EXIT_CONTROLS_INDEX - Index 0x483
 */
typedef union {
	struct {
		uint32_t save_cr0_and_cr4:1;
		uint32_t save_cr3:1;
		uint32_t save_debug_controls:1;
		uint32_t save_segment_registers:1;
		uint32_t save_esp_eip_eflags:1;
		uint32_t save_pending_debug_exceptions:1;
		uint32_t save_interruptibility_information:1;
		uint32_t save_activity_state:1;
		uint32_t save_working_vmcs_pointer:1;
		uint32_t ia32e_mode_host:1;
		uint32_t load_cr0_and_cr4:1;
		uint32_t load_cr3:1;
		uint32_t load_ia32_perf_global_ctrl:1;
		uint32_t load_segment_registers:1;
		uint32_t load_esp_eip:1;
		uint32_t acknowledge_interrupt_on_exit:1;
		uint32_t save_sys_enter_msrs:1;
		uint32_t load_sys_enter_msrs:1;
		uint32_t save_pat:1;
		uint32_t load_pat:1;
		uint32_t save_efer:1;
		uint32_t load_efer:1;
		uint32_t save_vmx_timer:1;
		uint32_t reserved_2:9;
	} PACKED bits;
	uint32_t uint32;
} PACKED vmexit_controls_t;

typedef union {
	struct {
		/* bits, that have 0 values may be set to 0 in VMCS */
		vmexit_controls_t	may_be_set_to_zero;
		/* bits, that have 1 values may be set to 1 in VMCS */
		vmexit_controls_t	may_be_set_to_one;
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_msr_vmexit_controls_t;

/*
 * VMX MSR Structure - IA32_MSR_VM_ENTRY_CONTROLS_INDEX - Index 0x484
 */
typedef union {
	struct {
		uint32_t load_cr0_and_cr4:1;
		uint32_t load_cr3:1;
		uint32_t load_debug_controls:1;
		uint32_t load_segment_registers:1;
		uint32_t load_esp_eip_eflags:1;
		uint32_t load_pending_debug_exceptions:1;
		uint32_t load_interruptibility_information:1;
		uint32_t load_activity_state:1;
		uint32_t load_working_vmcs_pointer:1;
		uint32_t ia32e_mode_guest:1;
		uint32_t entry_to_smm:1;
		uint32_t tear_down_smm_monitor:1;
		uint32_t load_sys_enter_msrs:1;
		uint32_t load_ia32_perf_global_ctrl:1;
		uint32_t load_pat:1;
		uint32_t load_efer:1;
		uint32_t reserved_1:16;
	} PACKED bits;
	uint32_t uint32;
} PACKED vmentry_controls_t;

typedef union {
	struct {
		/* bits, that have 0 values may be set to 0 in VMCS */
		vmentry_controls_t	may_be_set_to_zero;
		/* bits, that have 1 values may be set to 1 in VMCS */
		vmentry_controls_t	may_be_set_to_one;
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_msr_vmentry_controls_t;

/*
 * VMX MSR Structure - IA32_MSR_MISCELLANEOUS_DATA_INDEX - Index 0x485
 */
typedef union {
	struct {
		uint32_t preemption_timer_length:5; /* in TSC ticks */
		uint32_t reserved_0:1;
		uint32_t entry_in_halt_state_supported:1;
		uint32_t entry_in_shutdown_state_supported:1;
		uint32_t entry_in_wait_for_sipi_state_supported:1;
		uint32_t reserved_1:7;
		uint32_t number_of_cr3_target_values:9;
		uint32_t msr_lists_max_size:3; /* If this value is N, the max supported
						  msr list is 512*(N+1) */
		uint32_t reserved_2:4;
		uint32_t mseg_revision_identifier:32;
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_msr_miscellaneous_data_t;

typedef union {
	struct {
		/* RWX support */
		uint32_t x_only:1;
		uint32_t w_only:1;
		uint32_t w_and_x_only:1;
		/* gaw support */
		uint32_t gaw_21_bit:1;
		uint32_t gaw_30_bit:1;
		uint32_t gaw_39_bit:1;
		uint32_t gaw_48_bit:1;
		uint32_t gaw_57_bit:1;
		/* EMT support */
		uint32_t uc:1;
		uint32_t wc:1;
		uint32_t reserved0:2;
		uint32_t wt:1;
		uint32_t wp:1;
		uint32_t wb:1;
		uint32_t reserved1:1;
		/* SP support */
		uint32_t sp_21_bit:1;
		uint32_t sp_30_bit:1;
		uint32_t sp_39_bit:1;
		uint32_t sp_48_bit:1;

		uint32_t invept_supported:1;
		uint32_t reserved2:3;
		/* INVEPT Support */
		uint32_t invept_individual_address:1;
		uint32_t invept_context_wide:1;
		uint32_t invept_all_contexts:1;
		uint32_t reserved3:5;

		uint32_t invvpid_supported:1;
		uint32_t reserved4:7;
		/* INVVPID Support */
		uint32_t invvpid_individual_address:1;
		uint32_t invvpid_context_wide:1;
		uint32_t invvpid_all_contexts:1;
		uint32_t invvpid_all_contexts_preserving_globals:1;
		uint32_t reserved5:4;

		uint32_t reserved6:16;
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_vmx_ept_vpid_cap_t;

/*
 * VMX MSR Structure - IA32_MSR_CR0_ALLOWED_ZERO_INDEX,
 * IA32_MSR_CR0_ALLOWED_ONE_INDEX - Index 0x486, 0x487
 */
typedef em64t_cr0_t ia32_msr_cr0_t;

/*
 * VMX MSR Structure - IA32_MSR_CR4_ALLOWED_ZERO_INDEX,
 * IA32_MSR_CR4_ALLOWED_ONE_INDEX - Index 0x488, 0x489
 */
typedef em64t_cr4_t ia32_msr_cr4_t;

/*
 * VMX MSR Structure - ia32_msr_vmfunc_ctrl_t - Index 0x491
 */
typedef union {
	struct {
		uint32_t eptp_switching:1;
		uint32_t reserved_0:31;
		uint32_t reserved_1:32;
	} PACKED bits;
	uint64_t uint64;
} PACKED ia32_msr_vmfunc_ctrl_t;

typedef enum {
	EPTP_SWITCHING_BIT = 0,
} vmfunc_bits_t;


/*
 * Structure containing the complete set of VMX MSR Values
 */
typedef struct {
	ia32_msr_vmcs_revision_identifier_t	vmcs_revision_identifier;
	ia32_msr_pin_based_vm_execution_controls_t
						pin_based_vm_execution_controls;
	ia32_msr_processor_based_vm_execution_controls_t
						processor_based_vm_execution_controls;
	ia32_msr_processor_based_vm_execution_controls2_t
						processor_based_vm_execution_controls2;
	ia32_msr_vmexit_controls_t		vm_exit_controls;
	ia32_msr_vmentry_controls_t		vm_entry_controls;
	ia32_msr_miscellaneous_data_t		miscellaneous_data;
	ia32_msr_cr0_t				cr0_may_be_set_to_zero;
	ia32_msr_cr0_t				cr0_may_be_set_to_one;
	ia32_msr_cr4_t				cr4_may_be_set_to_zero;
	ia32_msr_cr4_t				cr4_may_be_set_to_one;
	ia32_vmx_ept_vpid_cap_t			ept_vpid_capabilities;
	ia32_msr_vmfunc_ctrl_t			vm_func_controls;
} ia32_vmx_capabilities_t;

#endif                          /* _VMX_CTRL_MSRS_H_ */
