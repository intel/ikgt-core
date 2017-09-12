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

#ifndef _VMEXIT_H_
#define _VMEXIT_H_

#include "vmm_objects.h"
/*
 * VMCS Exit Reason - Basic Reason
 * If change this enum, please also change
 * g_vmexit_handlers[] in vmexit.c
 */
enum {
	REASON_00_NMI_EXCEPTION,
	REASON_01_EXT_INT,
	REASON_02_TRIPLE_FAULT,
	REASON_03_INIT_EVENT,
	REASON_04_SIPI_EVENT,
	REASON_05_IO_SMI,
	REASON_06_OTHER_SMI,
	REASON_07_INT_WINDOW,
	REASON_08_NMI_WINDOW,
	REASON_09_TASK_SWITCH,
	REASON_10_CPUID_INSTR,
	REASON_11_GETSEC_INSTR,
	REASON_12_HLT_INSTR,
	REASON_13_INVD_INSTR,
	REASON_14_INVLPG_INSTR,
	REASON_15_RDPMC_INSTR,
	REASON_16_RDTSC_INSTR,
	REASON_17_RSM_INSTR,
	REASON_18_VMCALL_INSTR,
	REASON_19_VMCLEAR_INSTR,
	REASON_20_VMLUNCH_INSTR,
	REASON_21_VMPTRLD_INSTRN,
	REASON_22_VMPTRST_INSTR,
	REASON_23_VMREAD_INSTR,
	REASON_24_VMRESUME_INSTR,
	REASON_25_VMWRITE_INSTR,
	REASON_26_VMXOFF_INSTR,
	REASON_27_VMXON_INSTR,
	REASON_28_CR_ACCESS,
	REASON_29_DR_ACCESS,
	REASON_30_IO_INSTR,
	REASON_31_MSR_READ,
	REASON_32_MSR_WRITE,
	REASON_33_ENTRY_FAIL_GUEST,
	REASON_34_ENTRY_FAIL_MSR,
	REASON_35,                    // not defined in IA32 spec
	REASON_36_MWAIT_INSTR,
	REASON_37_MONITOR_TRAP,
	REASON_38,                    // not defined in IA32 spec
	REASON_39_MONITOR_INSTR,
	REASON_40_PAUSE_INSTR,
	REASON_41_ENTRY_FAIL_MC,
	REASON_42,                    // not defined in IA32 spec
	REASON_43_TPR_BELOW,
	REASON_44_APIC_ACCESS,
	REASON_45_VIRTAL_EOI,
	REASON_46_GDTR_LDTR_ACCESS,
	REASON_47_LDTR_TR_ACCESS,
	REASON_48_EPT_VIOLATION,
	REASON_49_EPT_MISCONFG,
	REASON_50_INVEPT_INSTR,
	REASON_51_RDTSCP_INSTR,
	REASON_52_PREEMP_TIMER,
	REASON_53_INVVPID_INSTR,
	REASON_54_WBINVD_INSTR,
	REASON_55_XSETBV_INSTR,
	REASON_56_APIC_WRITE,
	REASON_57_RDRAND_INSTR,
	REASON_58_WBINVD_INSTR,
	REASON_59_VMFUNC_INSTR,
	REASON_60_ENCLS_INSTR,
	REASON_61_RDSEED_INSTR,
	REASON_62_PAGE_MOD_LOG,
	REASON_63_XSAVES_INSTR,
	REASON_64_XRSTORS_INSTR,
	REASON_EXIT_COUNT
};

typedef void (*vmexit_handler_t) (guest_cpu_handle_t);

/*------------------------------------------------------------------------*
*  FUNCTION : vmexit_install_handler
*  PURPOSE  : Install specific VMEXIT handler
*  ARGUMENTS: vmexit_handler_t  handler
*           : uint32_t          reason
*  RETURNS  : void
*------------------------------------------------------------------------*/
void vmexit_install_handler(vmexit_handler_t handler,
				    uint32_t reason);

/*------------------------------------------------------------------------*
*  FUNCTION : vmexit_common_handler()
*  PURPOSE  : Called by vmexit_func() upon each VMEXIT
*  ARGUMENTS: void
*  RETURNS  : void
*------------------------------------------------------------------------*/
void vmexit_common_handler(void);

#endif                          /* _VMEXIT_H_ */
