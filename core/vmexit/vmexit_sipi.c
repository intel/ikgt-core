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

#include "mon_defs.h"
#include "hw_utils.h"
#include "vmcs_api.h"
#include "guest_cpu.h"
#include "ipc.h"
#include "mon_events_data.h"
#include "vmx_trace.h"
#include "pat_manager.h"
#include "hw_interlocked.h"

/* BSP Core */
uint32_t g_guest_num_of_cpus = 1;
extern uint32_t g_s3_resume_flag;

/*-------------------------------------------------------------------------*
*  FUNCTION : vmexit_sipi_event()
*  PURPOSE  : Configure VMCS register-state with CPU Real Mode state.
*           : and launch emulator.
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : void
*  NOTE     : The hard-coded values used for VMCS-registers initialization
*           : are the values CPU sets its registers with after RESET.
*           : See Intel(R)64 and IA-32 Architectures Software Developer's
*           : Manual Volume 3A: System Programming Guide, Part 1
*           : Table 9-1. IA-32 Processor States Following Power-up, Reset, or
*           : INIT
*-------------------------------------------------------------------------*/
vmexit_handling_status_t vmexit_sipi_event(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ia32_vmx_exit_qualification_t qualification;
	uint16_t real_mode_segment;
	vmexit_handling_status_t ret_status = VMEXIT_NOT_HANDLED;

	/* single-execution loop */
	do {
		/* Check if this is IPC SIPI signal. */
		if (ipc_sipi_vmexit_handler(gcpu)) {
			/* We're already in Wait for SIPI state. Nothing to do */
			ret_status = VMEXIT_HANDLED;
			break;
		}

		/* AP currently is in Wait for SIPI state, gets guest SIPI
		 * (vector not 0xff) and activates the AP */
		if (g_s3_resume_flag == 0) {
			/* No S3 resume, count guest AP cores */
			hw_interlocked_increment((int32_t *)&(
					g_guest_num_of_cpus));
		}

		MON_LOG(mask_anonymous, level_trace,
			"CPU-%d Leave SIPI State: Guest Core count is %d.\n",
			hw_cpu_id(), g_guest_num_of_cpus);

		MON_DEBUG_CODE(mon_trace(gcpu, "[sipi] Leave SIPI State\n"));

		/* emulator configures guest with host state, and setup emulator
		 * context to real mode, thus we have to configure the guest with the
		 * values of Real Mode, i.e. those values, CPU sets to registers after
		 * RESET, though we never launch guest in that way */

		/*------------------ Set Control Registers ------------------*/

		gcpu_set_guest_visible_control_reg(gcpu,
			IA32_CTRL_CR0,
			0x60000010);
		gcpu_set_control_reg(gcpu, IA32_CTRL_CR0, 0x60000010);

		gcpu_set_guest_visible_control_reg(gcpu, IA32_CTRL_CR3, 0);
		gcpu_set_control_reg(gcpu, IA32_CTRL_CR3, 0);

		gcpu_set_guest_visible_control_reg(gcpu, IA32_CTRL_CR4, 0);
		gcpu_set_control_reg(gcpu, IA32_CTRL_CR4, 0);

		gcpu_set_control_reg(gcpu, IA32_CTRL_CR2, 0);
		gcpu_set_control_reg(gcpu, IA32_CTRL_CR8, 0);

		/*------------------ Set Segment Registers ------------------*/

		qualification.uint64 = mon_vmcs_read(vmcs,
			VMCS_EXIT_INFO_QUALIFICATION);
		real_mode_segment = (uint16_t)qualification.sipi.vector << 8;

		gcpu_set_segment_reg(gcpu,
			IA32_SEG_CS,
			real_mode_segment,
			real_mode_segment << 4, 0xFFFF, 0x9B);

		/* Attribute set bits: Present, R/W, Accessed */
		gcpu_set_segment_reg(gcpu, IA32_SEG_DS, 0, 0, 0xFFFF, 0x93);
		gcpu_set_segment_reg(gcpu, IA32_SEG_ES, 0, 0, 0xFFFF, 0x93);
		gcpu_set_segment_reg(gcpu, IA32_SEG_FS, 0, 0, 0xFFFF, 0x93);
		gcpu_set_segment_reg(gcpu, IA32_SEG_GS, 0, 0, 0xFFFF, 0x93);
		gcpu_set_segment_reg(gcpu, IA32_SEG_SS, 0, 0, 0xFFFF, 0x93);

		/* IA Manual 3B: 23.3.1.2 For TR bits 3:0 (Type).  If the guest will
		 * not be IA-32e mode, the Type must be 3 or 11.  If the guest will be
		 * IA-32e mode, the Type must be 11 Using Type 11 here */
		/* CIRT uses 8Bh */
		gcpu_set_segment_reg(gcpu, IA32_SEG_TR, 0, 0, 0xFFFF, 0x8B);
		/* CIRT uses 10082h */
		gcpu_set_segment_reg(gcpu, IA32_SEG_LDTR, 0, 0, 0xFFFF, 0x82);

		/*------------------ Set Memory Mgmt Registers ------------------*/

		gcpu_set_gdt_reg(gcpu, 0, 0xFFFF);
		gcpu_set_idt_reg(gcpu, 0, 0xFFFF);

		/*------------------ Set Debug Registers ------------------*/

		gcpu_set_debug_reg(gcpu, IA32_REG_DR0, 0);
		gcpu_set_debug_reg(gcpu, IA32_REG_DR1, 0);
		gcpu_set_debug_reg(gcpu, IA32_REG_DR2, 0);
		gcpu_set_debug_reg(gcpu, IA32_REG_DR3, 0);
		gcpu_set_debug_reg(gcpu, IA32_REG_DR6, 0xFFFF0FF0);
		gcpu_set_debug_reg(gcpu, IA32_REG_DR7, 0x00000400);

		/*------------------ Set General Purpose Registers ------------------*/
		gcpu_set_gp_reg(gcpu, IA32_REG_RAX, 0);
		gcpu_set_gp_reg(gcpu, IA32_REG_RBX, 0);
		gcpu_set_gp_reg(gcpu, IA32_REG_RCX, 0);
		gcpu_set_gp_reg(gcpu, IA32_REG_RDX, 0xF00);
		gcpu_set_gp_reg(gcpu, IA32_REG_RDI, 0);
		gcpu_set_gp_reg(gcpu, IA32_REG_RSI, 0);
		gcpu_set_gp_reg(gcpu, IA32_REG_RBP, 0);
		/* CIRT uses FFFCh ?? */
		gcpu_set_gp_reg(gcpu, IA32_REG_RSP, 0);
		gcpu_set_gp_reg(gcpu, IA32_REG_RIP, 0);
		/* CIRT uses 46h ?? */
		gcpu_set_gp_reg(gcpu, IA32_REG_RFLAGS, 2);

		gcpu_set_msr_reg(gcpu, IA32_MON_MSR_EFER, 0);

		gcpu_set_activity_state(gcpu,
			IA32_VMX_VMCS_GUEST_SLEEP_STATE_ACTIVE);
		/* set state in vmenter control fields */
		gcpu_set_vmenter_control(gcpu);

		ret_status = VMEXIT_HANDLED;
	} while (0);

	return ret_status;
}
