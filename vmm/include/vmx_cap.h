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

#ifndef _VMX_CAP_H_
#define _VMX_CAP_H_

#include "vmm_base.h"
#include "vmcs.h"

/*-------------------------------------------------------------------------
 *
 * pin_base define
 *
 *------------------------------------------------------------------------- */
#define PIN_EXINT_EXIT        (1u << 0)
#define PIN_NMI_EXIT          (1u << 3)
#define PIN_VIRTUAL_NMI       (1u << 5)
#define PIN_PREEMPTION_TIMER  (1u << 6)
#define PIN_PROC_POSTED_INT   (1u << 7)

uint32_t get_init_pin(void);
uint32_t get_pinctl_cap(uint32_t* p_may0);

/*-------------------------------------------------------------------------
 *
 * proc control define
 *
 *------------------------------------------------------------------------- */
#define PROC_INT_WINDOW_EXIT     (1u << 2)
#define PROC_TSC_OFFSET          (1u << 3)
#define PROC_HLT_EXIT            (1u << 7)
#define PROC_INVLPG_EXI          (1u << 9)
#define PROC_MWAIT_EXIT          (1u << 10)
#define PROC_RDPMC_EXIT          (1u << 11)
#define PROC_RDTSC_EXIT          (1u << 12)
#define PROC_CR3_LOAD_EXIT       (1u << 15)
#define PROC_CR3_STORE_EXIT      (1u << 16)
#define PROC_CR8_LOAD_EXIT       (1u << 19)
#define PROC_CR8_STORE_EXIT      (1u << 20)
#define PROC_TPR_SHADOW          (1u << 21)
#define PROC_NMI_WINDOW_EXIT     (1u << 22)
#define PROC_MOV_DR_EXIT         (1u << 23)
#define PROC_UNCONDITION_IO_EXIT (1u << 24)
#define PROC_IO_BITMAPS          (1u << 25)
#define PROC_MONITOR_TRAP_FLAG   (1u << 27)
#define PROC_MSR_BITMAPS         (1u << 28)
#define PROC_MONITOR_EXIT        (1u << 29)
#define PROC_PAUSE_EXIT          (1u << 30)
#define PROC_SECONDARY_CTRL      (1u << 31)

uint32_t get_init_proc1(void);
uint32_t get_proctl1_cap(uint32_t* p_may0);

/*-------------------------------------------------------------------------
 *
 * proc2 control define
 *
 *------------------------------------------------------------------------- */
#define PROC2_VAPIC_ACCESSES      (1u << 0)
#define PRO2C_ENABLE_EPT          (1u << 1)
#define PROC2_DESCRIPTOR_EXIT     (1u << 2)
#define PROC2_ENABLE_RDTSCP       (1u << 3)
#define PROC2_VX2APIC_MODE        (1u << 4)
#define PROC2_ENABLEC_VPID        (1u << 5)
#define PROC2_WBINVD_EXIT         (1u << 6)
#define PROC2_UNRESTRICTED_GUEST  (1u << 7)
#define PROC2_APIC_REG_VIRTUALIZE (1u << 8)
#define PROC2_VINT_DELIVERY       (1u << 9)
#define PROC2_PAUSE_LOOP_EXIT     (1u << 10)
#define PROC2_RDRAND_EXIT         (1u << 11)
#define PROC2_ENABLE_INVPCID      (1u << 12)
#define PROC2_ENABLE_VM_FUNC      (1u << 13)
#define PROC2_VMCS_SHADOW         (1u << 14)
#define PROC2_ENABLE_ENCLS_EXIT   (1u << 15)
#define PROC2_RDSEED_EXIT         (1u << 16)
#define PROC2_ENABLE_PML          (1u << 17)
#define PROC2_EPT_VIOLATION       (1u << 18)
#define PROC2_VMX_CONCEAL         (1u << 19)
#define PROC2_ENABLE_XSAVE        (1u << 20)
#define PROC2_TSC_SCALING         (1u << 25)

uint32_t get_init_proc2(void);
uint32_t get_proctl2_cap(uint32_t* p_may0);

/*-------------------------------------------------------------------------
 *
 * exit control define
 *
 *------------------------------------------------------------------------- */
#define EXIT_SAVE_DBUG_CTRL      (1u << 2)
#define EXIT_HOST_ADDR_SPACE     (1u << 9)
#define EXIT_LOAD_IA32_PERF_CTRL (1u << 12)
#define EXIT_ACK_INT_EXIT        (1u << 15)
#define EXIT_SAVE_IA32_PAT       (1u << 18)
#define EXIT_LOAD_IA32_PAT       (1u << 19)
#define EXIT_SAVE_IA32_EFER      (1u << 20)
#define EXIT_LOAD_IA32_EFER      (1u << 21)
#define EXIT_SAVE_PREE_TIME      (1u << 22)
#define EXIT_CLR_IA32_BNDCFGS    (1u << 23)
#define EXIT_VM_EXIT_CONCEAL     (1u << 24)

uint32_t get_init_exit(void);
uint32_t get_exitctl_cap(uint32_t* p_may0);

/*-------------------------------------------------------------------------
 *
 * entry control define
 *
 *------------------------------------------------------------------------- */
#define ENTRY_LOAD_DBUG_CTRL        (1u << 2)
#define ENTRY_GUEST_IA32E_MODE      (1u << 9)
#define ENTRY_TO_SMM                (1u << 10)
#define ENTRY_DE_2MONITOR_TREATMENT (1u << 11)
#define ENTRY_LOAD_IA32_PERF_CTRL   (1u << 13)
#define ENTRY_LOAD_IA32_PAT         (1u << 14)
#define ENTRY_LOAD_IA32_EFER        (1u << 15)
#define ENTRY_LOAD_IA32_BNDCFGS     (1u << 16)
#define ENTRY_VM_ENTRY_CONCEAL      (1u << 17)

uint32_t get_init_entry(void);
uint32_t get_entryctl_cap(uint32_t* p_may0);

/*-------------------------------------------------------------------------
 *
 * CR control define
 *
 *------------------------------------------------------------------------- */
uint64_t get_cr0_cap(uint64_t* p_may0);
uint64_t get_cr4_cap(uint64_t* p_may0);
uint64_t get_init_cr0(void);
uint64_t get_init_cr4(void);

/*-------------------------------------------------------------------------
 *
 * EPT control define
 *
 *------------------------------------------------------------------------- */
typedef union {
	struct {
		uint64_t x_only:1;
		uint64_t rsvd_1_5:5;
		uint64_t page_walk_len_4:1;
		uint64_t rsvd_7:1;
		uint64_t uc:1;
		uint64_t rsvd_9_13:5;
		uint64_t wb:1;
		uint64_t rsvd_15:1;
		uint64_t paging_2m:1;
		uint64_t paging_1g:1;
		uint64_t rsvd_18_19:2;
		uint64_t invept:1;
		uint64_t dirty_flag:1;
		uint64_t rsvd_22_24:3;
		uint64_t invept_single_ctx:1;
		uint64_t invept_all_ctx:1;
		uint64_t rsvd_27_31:5;
		uint64_t invvpid:1;
		uint64_t rsvd_33_39:7;
		uint64_t invvpid_individual_addr:1;
		uint64_t invvpid_single_ctx:1;
		uint64_t invvpid_all_ctx:1;
		uint64_t invvpid_retain_globals:1;
		uint64_t rsvd_44_63:20;
	}  bits;
	uint64_t uint64;
} vmx_ept_vpid_cap_t;
uint64_t get_ept_vpid_cap();

/*-------------------------------------------------------------------------
 *
 * misc data control define
 *
 *------------------------------------------------------------------------- */
typedef union {
	struct {
		uint64_t vmx_timer_scale:5; /* in TSC ticks */
		uint64_t save_guest_mode:1;
		uint64_t halt:1;
		uint64_t shutdown:1;
		uint64_t sipi:1;
		uint64_t rsvd_9_14:6;
		uint64_t rdmsr_in_smm:1;
		uint64_t max_cr3_target_num:9;
		uint64_t max_msr_list_size:3; /* If this value is N, the max supported
						  msr list is 512*(N+1) */
		uint64_t vmoff_block_smi:1;
		uint64_t vmwrite_all_vmcs:1;
		uint64_t instr_len_allow_zero:1;
		uint64_t rsvd_31:1;
		uint64_t mseg_rev_id:32;
	} bits;
	uint64_t uint64;
} msr_misc_data_t;

uint64_t get_misc_data_cap();

typedef union {
	struct {
		uint64_t rev_id:31;                       /* bits 0-30 */
		uint64_t rsvd_31:1;                       /* bits 31 always be 0*/
		uint64_t region_size:13;                  /* bits 32-44 */
		uint64_t rsvd_45_47:3;                    /* bits 45-47 */
		uint64_t phy_addr_width:1;                /* bit 48 */
		uint64_t dual_monitor:1;                   /* bit 49 */
		uint64_t memory_type:4;                   /* bits 50:53 */
		uint64_t instr_info_valid:1;              /* bit 54 */
		uint64_t use_true_msr:1;                /* bit 55 */
		uint64_t rsvd_56_63:8;                   /* bits 56-63 */
	} bits;
	uint64_t uint64;
}  basic_info_t;

uint64_t get_basic_cap();

#define MSR_VMX_FIRST               0x480
#define MSR_VMX_LAST                0x491

/*-------------------------------------------------------------------------
 *
 * Init
 *
 *------------------------------------------------------------------------- */
void vmx_cap_init(void);

/*-------------------------------------------------------------------------
 *
 * Allocate VMCS region
 *
 * Returns HPA:
 *
 *------------------------------------------------------------------------- */
uint64_t vmcs_alloc(void);

#if DEBUG
/*-------------------------------------------------------------------------
 *
 * vmx_cap_check
 *
 * Returns void:
 *
 *------------------------------------------------------------------------- */
void vmx_cap_check(void);
#endif

#endif                          /* _VMCS_INIT_H_ */
