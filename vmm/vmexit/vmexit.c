/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "heap.h"
#include "scheduler.h"
#include "vmx_asm.h"
#include "vmcs.h"
#include "guest.h"
#include "vmexit_cpuid.h"
#include "vmexit.h"
#include "dbg.h"
#include "host_cpu.h"
#include "gcpu.h"
#include "gcpu_switch.h"
#include "gcpu_inject_event.h"
#include "vmexit_handler.h"
#include "vmexit_cr_access.h"
#include "isr.h"
#include "ept.h"
#include "event.h"

static vmexit_handler_t g_vmexit_handlers[] = {
	NULL,                                     // REASON_00_NMI_EXCEPTION
	NULL,                                     // REASON_01_EXT_INT
	vmexit_triple_fault,                      // REASON_02_TRIPLE_FAULT
	NULL,                                     // REASON_03_INT_EVENT
	vmexit_sipi_event,                        // REASON_04_SIPI_EVENT
	NULL,                                     // REASON_05_IO_SMI
	NULL,                                     // REASON_06_OTHER_SMI
	vmexit_intr_window,                       // REASON_07_INT_WINDOW
	vmexit_nmi_window,                        // REASON_08_NMI_WINDOW
	NULL,                                     // REASON_09_TASK_SWITCH
	vmexit_cpuid_instruction,                 // REASON_10_CPUID_INSTR
	NULL,                                     // REASON_11_GETSEC_INSTR
	NULL,                                     // REASON_12_HLT_INSTR
	vmexit_invd,                              // REASON_13_INVD_INSTR
	NULL,                                     // REASON_14_INVLPG_INSTR
	NULL,                                     // REASON_15_RDPMC_INSTR
	NULL,                                     // REASON_16_RDTSC_INSTR
	NULL,                                     // REASON_17_RSM_INSTR
	NULL,                                     // REASON_18_VMCALL_INSTR
	vmexit_invalid_instruction,               // REASON_19_VMCLEAR_INSTR
	vmexit_invalid_instruction,               // REASON_20_VMLUNCH_INSTR
	vmexit_invalid_instruction,               // REASON_21_VMPTRLD_INSTRN
	vmexit_invalid_instruction,               // REASON_22_VMPTRST_INSTR
	vmexit_invalid_instruction,               // REASON_23_VMREAD_INSTR
	vmexit_invalid_instruction,               // REASON_24_VMRESUME_INSTR
	vmexit_invalid_instruction,               // REASON_25_VMWRITE_INSTR
	vmexit_invalid_instruction,               // REASON_26_VMXOFF_INSTR
	vmexit_invalid_instruction,               // REASON_27_VMXON_INSTR
	vmexit_cr_access,                         // REASON_28_CR_ACCESS
	NULL,                                     // REASON_29_DR_ACCESS
	NULL,                                     // REASON_30_IO_INSTR
	vmexit_msr_read,                          // REASON_31_MSR_READ
	vmexit_msr_write,                         // REASON_32_MSR_WRITE
	NULL,                                     // REASON_33_ENTRY_FAIL_GUEST
	NULL,                                     // REASON_34_ENTRY_FAIL_MSR
	NULL,                                     // REASON_35
	NULL,                                     // REASON_36_MWAIT_INSTR
	NULL,                                     // REASON_37_MONITOR_TRAP
	NULL,                                     // REASON_38
	NULL,                                     // REASON_39_MONITOR_INSTR
	NULL,                                     // REASON_40_PAUSE_INSTR
	NULL,                                     // REASON_41_ENTRY_FAIL_MC
	NULL,                                     // REASON_42
	NULL,                                     // REASON_43_TPR_BELOW
	NULL,                                     // REASON_44_APIC_ACCESS
	NULL,                                     // REASON_45_VIRTAL_EOI
	NULL,                                     // REASON_46_GDTR_LDTR_ACCESS
	NULL,                                     // REASON_47_LDTR_TR_ACCESS
	vmexit_ept_violation,                     // REASON_48_EPT_VIOLATION
	vmexit_ept_misconfiguration,              // REASON_49_EPT_MISCONFG
	vmexit_invalid_instruction,               // REASON_50_INVEPT_INSTR
	NULL,                                     // REASON_51_RDTSCP_INSTR
	NULL,                                     // REASON_52_PREEMP_TIMER
	vmexit_invalid_instruction,               // REASON_53_INVVPID_INSTR
	NULL,                                     // REASON_54_WBINVD_INSTR
	vmexit_xsetbv,                            // REASON_55_XSETBV_INSTR
	NULL,                                     // REASON_56_APIC_WRITE
	NULL,                                     // REASON_57_RDRAND_INSTR
	NULL,                                     // REASON_58_WBINVD_INSTR
	NULL,                                     // REASON_59_VMFUNC_INSTR
	NULL,                                     // REASON_60_ENCLS_INSTR
	NULL,                                     // REASON_61_RDSEED_INSTR
	NULL,                                     // REASON_62_PAGE_MOD_LOG
	NULL,                                     // REASON_63_XSAVES_INSTR
	NULL                                      // REASON_64_XRSTORS_INSTR
};

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_install_handler
*  PURPOSE  : Install specific VMEXIT handler
*  ARGUMENTS: vmexit_handler_t  handler
*           : uint32_t          reason
*  RETURNS  : void
*--------------------------------------------------------------------------*/
void vmexit_install_handler(vmexit_handler_t handler,
				    uint32_t reason)
{
	VMM_ASSERT_EX((reason < REASON_EXIT_COUNT),
		"CPU%d: Error: VMEXIT Reason(%d) exceeds supported limit(%d)\n",
		host_cpu_id(), reason, REASON_EXIT_COUNT);
	VMM_ASSERT_EX((g_vmexit_handlers[reason] == NULL),
		"reason %d registered twice\n", reason);

	g_vmexit_handlers[reason] = handler;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_common_handler()
*  PURPOSE  : Called by vmexit_func() upon each VMEXIT
*  ARGUMENTS: void
*  RETURNS  : void
*--------------------------------------------------------------------------*/
void vmexit_common_handler(void)
{
	guest_cpu_handle_t gcpu;
	guest_cpu_handle_t next_gcpu;
	vmcs_obj_t vmcs;
	vmx_exit_reason_t reason;
	event_profile_t profile;

	/* Raise event to overwrite RSB to prevent Spectre Side-channel attack
	   at the very beginning of VM Exit. This Event must be raised before any
	   RET instruction. */
	event_raise(NULL, EVENT_RSB_OVERWRITE, NULL);

	profile.vmexit_tsc = asm_rdtsc();

	gcpu = get_current_gcpu();
	D(VMM_ASSERT(gcpu));

	/* clear guest cpu cache data. in fact it clears all VMCS caches too. */
	vmcs_clear_cache(gcpu->vmcs);

	gcpu_reflect_idt_vectoring_info(gcpu);
	gcpu_check_nmi_iret(gcpu);

	/* read VMEXIT reason */
	vmcs = gcpu->vmcs;
	reason.uint32 = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_REASON);

	VMM_ASSERT_EX((reason.bits.basic_reason < REASON_EXIT_COUNT),
		"CPU%d: Error: VMEXIT Reason(%d) exceeds supported limit(%d)\n",
		host_cpu_id(), reason.bits.basic_reason, REASON_EXIT_COUNT);
	VMM_ASSERT_EX((g_vmexit_handlers[reason.bits.basic_reason] != NULL),
		"reason %d is not registered\n", reason.bits.basic_reason);

	/* call reason-specific VMEXIT handler */
	g_vmexit_handlers[reason.bits.basic_reason](gcpu);

	/* select guest for execution */
	next_gcpu = get_current_gcpu();

	D(VMM_ASSERT(next_gcpu));

	profile.next_gcpu = next_gcpu;
	event_raise(gcpu, EVENT_MODULE_PROFILE, (void *)&profile);

	gcpu_resume(next_gcpu);
}
