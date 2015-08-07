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

#include "vmcs_init.h"
#include "vmx_ctrl_msrs.h"
#include "vmx_vmcs.h"
#include "mon_phys_mem_types.h"
#include "hw_utils.h"
#include "heap.h"
#include "libc.h"
#include "gpm_api.h"
#include "host_memory_manager_api.h"
#include "host_cpu.h"
#include "hw_vmx_utils.h"
#include "mon_dbg.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMCS_INIT_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMCS_INIT_C, __condition)

#define MAX_32BIT_NUMBER 0x0FFFFFFFF
#define MASK_PE_PG_OFF_UNRESTRICTED_GUEST 0xFFFFFFFF7FFFFFFE

/****************************************************************************
*
* Initialization of the VMCS
*
****************************************************************************/

/*------------------------------ types ------------------------------------ */
/*------------------------------ globals ---------------------------------- */

static ia32_vmx_capabilities_t g_vmx_capabilities;
static vmcs_hw_constraints_t g_vmx_constraints;
static vmcs_hw_fixed_t g_vmx_fixed;
vmcs_hw_fixed_t *gp_vmx_fixed = &g_vmx_fixed;
uint64_t g_vmx_fixed_1_cr0_save;

static boolean_t g_init_done = FALSE;

static void print_vmx_capabilities(void);

/*------------------------------ macros            ------------------------ */
#define VMCS_REGION_SIZE                                                        \
	(g_vmx_capabilities.vmcs_revision_identifier.bits.vmcs_region_size)

#define VMCS_ABOVE_4G_SUPPORTED                                                 \
	(g_vmx_capabilities.vmcs_revision_identifier.bits.physical_address_width \
	 != 1)

#define VMCS_MAX_SIZE_OF_MSR_LISTS                                              \
	((g_vmx_capabilities.miscellaneous_data.bits.msr_lists_max_size + \
	 1) * 512)

#define VMCS_MEMORY_TYPE    vmcs_memory_type()

#define VMCS_REVISION                                                           \
	(g_vmx_capabilities.vmcs_revision_identifier.bits.revision_identifier)

#ifdef DEBUG
#define VMCS_FIXED_BIT_2_CHAR(field_name, bit_name)                           \
	fx_bit_2_char(g_vmx_fixed.fixed_0_ ## field_name.bits.bit_name,        \
	g_vmx_fixed.fixed_1_ ## field_name.bits.bit_name)

INLINE char fx_bit_2_char(uint32_t mb0, uint32_t mb1)
{
	return (mb0 != mb1) ? 'X' : (mb0 == 0) ? '0' : '1';
}
#endif

#define CAP_BIT_TO_CHAR(field_name, bit_name) \
	((g_vmx_capabilities.field_name).bits.bit_name + '0')

/*------------------------------ internal functions------------------------ */
static void fill_vmx_capabilities(void)
{
	g_vmx_capabilities.vmcs_revision_identifier.uint64 =
		hw_read_msr(IA32_MSR_VMCS_REVISION_IDENTIFIER_INDEX);
	g_vmx_capabilities.pin_based_vm_execution_controls.uint64 =
		hw_read_msr(IA32_MSR_PIN_BASED_VM_EXECUTION_CONTROLS_INDEX);
	g_vmx_capabilities.processor_based_vm_execution_controls.uint64 =
		hw_read_msr(IA32_MSR_PROCESSOR_BASED_VM_EXECUTION_CONTROLS_INDEX);
	g_vmx_capabilities.ept_vpid_capabilities.uint64 = 0;

	if (g_vmx_capabilities.processor_based_vm_execution_controls.bits.
	    may_be_set_to_one.bits.secondary_controls) {
		g_vmx_capabilities.processor_based_vm_execution_controls2.uint64
			=
				hw_read_msr(
					IA32_MSR_PROCESSOR_BASED_VM_EXECUTION_CONTROLS2_INDEX);

		if (g_vmx_capabilities.processor_based_vm_execution_controls2.
		    bits.may_be_set_to_one.bits.enable_ept
		    || g_vmx_capabilities.processor_based_vm_execution_controls2
		    .
		    bits.may_be_set_to_one.bits.enable_vpid) {
			g_vmx_capabilities.ept_vpid_capabilities.uint64 =
				hw_read_msr(IA32_MSR_EPT_VPID_CAP_INDEX);
		}
	}

	g_vmx_capabilities.vm_exit_controls.uint64 =
		hw_read_msr(IA32_MSR_VM_EXIT_CONTROLS_INDEX);
	g_vmx_capabilities.vm_entry_controls.uint64 =
		hw_read_msr(IA32_MSR_VM_ENTRY_CONTROLS_INDEX);
	g_vmx_capabilities.miscellaneous_data.uint64 =
		hw_read_msr(IA32_MSR_MISCELLANEOUS_DATA_INDEX);
	g_vmx_capabilities.cr0_may_be_set_to_zero.uint64 =
		hw_read_msr(IA32_MSR_CR0_ALLOWED_ZERO_INDEX);
	g_vmx_capabilities.cr0_may_be_set_to_one.uint64 =
		hw_read_msr(IA32_MSR_CR0_ALLOWED_ONE_INDEX);
	g_vmx_capabilities.cr4_may_be_set_to_zero.uint64 =
		hw_read_msr(IA32_MSR_CR4_ALLOWED_ZERO_INDEX);
	g_vmx_capabilities.cr4_may_be_set_to_one.uint64 =
		hw_read_msr(IA32_MSR_CR4_ALLOWED_ONE_INDEX);

	MON_ASSERT(VMCS_REGION_SIZE != 0);

	g_vmx_constraints.may1_pin_based_exec_ctrl.uint32 =
		g_vmx_capabilities.pin_based_vm_execution_controls.bits.
		may_be_set_to_one.uint32;
	g_vmx_constraints.may0_pin_based_exec_ctrl.uint32 =
		g_vmx_capabilities.pin_based_vm_execution_controls.bits.
		may_be_set_to_zero.uint32;

	g_vmx_constraints.may1_processor_based_exec_ctrl.uint32 =
		g_vmx_capabilities.processor_based_vm_execution_controls.bits.
		may_be_set_to_one.uint32;
	g_vmx_constraints.may0_processor_based_exec_ctrl.uint32 =
		g_vmx_capabilities.processor_based_vm_execution_controls.
		bits.may_be_set_to_zero.uint32;

	g_vmx_constraints.may1_processor_based_exec_ctrl2.uint32 =
		g_vmx_capabilities.processor_based_vm_execution_controls2.
		bits.may_be_set_to_one.uint32;
	g_vmx_constraints.may0_processor_based_exec_ctrl2.uint32 =
		g_vmx_capabilities.processor_based_vm_execution_controls2.
		bits.may_be_set_to_zero.uint32;

	g_vmx_constraints.may1_vm_exit_ctrl.uint32 =
		g_vmx_capabilities.vm_exit_controls.bits.may_be_set_to_one.
		uint32;
	g_vmx_constraints.may0_vm_exit_ctrl.uint32 =
		g_vmx_capabilities.vm_exit_controls.bits.may_be_set_to_zero.
		uint32;

	g_vmx_constraints.may1_vm_entry_ctrl.uint32 =
		g_vmx_capabilities.vm_entry_controls.bits.may_be_set_to_one.
		uint32;
	g_vmx_constraints.may0_vm_entry_ctrl.uint32 =
		g_vmx_capabilities.vm_entry_controls.bits.may_be_set_to_zero.
		uint32;

	g_vmx_constraints.may1_cr0.uint64 =
		g_vmx_capabilities.cr0_may_be_set_to_one.uint64;
	g_vmx_constraints.may0_cr0.uint64 =
		g_vmx_capabilities.cr0_may_be_set_to_zero.uint64;

	g_vmx_constraints.may1_cr4.uint64 =
		g_vmx_capabilities.cr4_may_be_set_to_one.uint64;
	g_vmx_constraints.may0_cr4.uint64 =
		g_vmx_capabilities.cr4_may_be_set_to_zero.uint64;

	g_vmx_constraints.number_of_cr3_target_values =
		g_vmx_capabilities.miscellaneous_data.bits.
		number_of_cr3_target_values;
	g_vmx_constraints.max_msr_lists_size_in_bytes =
		VMCS_MAX_SIZE_OF_MSR_LISTS;
	g_vmx_constraints.vmx_timer_length =
		g_vmx_capabilities.miscellaneous_data.bits.
		preemption_timer_length;
	g_vmx_constraints.vmcs_revision = VMCS_REVISION;
	g_vmx_constraints.mseg_revision_id =
		g_vmx_capabilities.miscellaneous_data.bits.
		mseg_revision_identifier;
	g_vmx_constraints.vm_entry_in_halt_state_supported =
		g_vmx_capabilities.miscellaneous_data.bits.
		entry_in_halt_state_supported;
	g_vmx_constraints.vm_entry_in_shutdown_state_supported =
		g_vmx_capabilities.miscellaneous_data.bits.
		entry_in_shutdown_state_supported;
	g_vmx_constraints.vm_entry_in_wait_for_sipi_state_supported =
		g_vmx_capabilities.miscellaneous_data.
		bits.entry_in_wait_for_sipi_state_supported;
	g_vmx_constraints.processor_based_exec_ctrl2_supported =
		g_vmx_capabilities.processor_based_vm_execution_controls.bits.
		may_be_set_to_one.bits.secondary_controls;
	g_vmx_constraints.ept_supported =
		g_vmx_constraints.processor_based_exec_ctrl2_supported
		&& g_vmx_capabilities.processor_based_vm_execution_controls2.
		bits.may_be_set_to_one.bits.enable_ept;
	g_vmx_constraints.unrestricted_guest_supported =
		g_vmx_constraints.processor_based_exec_ctrl2_supported
		&& g_vmx_capabilities.processor_based_vm_execution_controls2.
		bits.may_be_set_to_one.bits.unrestricted_guest;
	g_vmx_constraints.vpid_supported =
		g_vmx_constraints.processor_based_exec_ctrl2_supported
		&& g_vmx_capabilities.processor_based_vm_execution_controls2.
		bits.may_be_set_to_one.bits.enable_vpid;

	g_vmx_constraints.vmfunc_supported =
		g_vmx_constraints.processor_based_exec_ctrl2_supported
		&& g_vmx_capabilities.processor_based_vm_execution_controls2.
		bits.may_be_set_to_one.bits.vmfunc;
	if (g_vmx_constraints.vmfunc_supported) {
		g_vmx_capabilities.vm_func_controls.uint64 =
			hw_read_msr(IA32_MSR_VMX_VMFUNC_CTRL);
		MON_LOG(mask_anonymous, level_trace,
			"VmFuncCtrl                                   = %18P\n",
			g_vmx_capabilities.vm_func_controls.uint64);
		if (g_vmx_capabilities.vm_func_controls.bits.eptp_switching) {
			g_vmx_constraints.eptp_switching_supported =
				g_vmx_constraints.vmfunc_supported
				&& g_vmx_capabilities.vm_func_controls.bits.
				eptp_switching;
			MON_LOG(mask_anonymous,
				level_trace,
				"EPTP switching supported...");
		}
	}

	g_vmx_constraints.ve_supported =
		g_vmx_constraints.processor_based_exec_ctrl2_supported
		&& g_vmx_capabilities.processor_based_vm_execution_controls2.
		bits.may_be_set_to_one.bits.ve;
	if (g_vmx_constraints.ve_supported) {
		MON_LOG(mask_anonymous, level_trace, "ve supported...\n");
	}

	g_vmx_constraints.ept_vpid_capabilities =
		g_vmx_capabilities.ept_vpid_capabilities;

	/* determine fixed values */
	g_vmx_fixed.fixed_1_pin_based_exec_ctrl.uint32 =
		g_vmx_constraints.may0_pin_based_exec_ctrl.uint32 &
		g_vmx_constraints.may1_pin_based_exec_ctrl.uint32;
	g_vmx_fixed.fixed_0_pin_based_exec_ctrl.uint32 =
		g_vmx_constraints.may0_pin_based_exec_ctrl.uint32 |
		g_vmx_constraints.may1_pin_based_exec_ctrl.uint32;

	g_vmx_fixed.fixed_1_processor_based_exec_ctrl.uint32 =
		g_vmx_constraints.may0_processor_based_exec_ctrl.uint32 &
		g_vmx_constraints.may1_processor_based_exec_ctrl.uint32;
	g_vmx_fixed.fixed_0_processor_based_exec_ctrl.uint32 =
		g_vmx_constraints.may0_processor_based_exec_ctrl.uint32 |
		g_vmx_constraints.may1_processor_based_exec_ctrl.uint32;

	g_vmx_fixed.fixed_1_processor_based_exec_ctrl2.uint32 =
		g_vmx_constraints.may0_processor_based_exec_ctrl2.uint32 &
		g_vmx_constraints.may1_processor_based_exec_ctrl2.uint32;
	g_vmx_fixed.fixed_0_processor_based_exec_ctrl2.uint32 =
		g_vmx_constraints.may0_processor_based_exec_ctrl2.uint32 |
		g_vmx_constraints.may1_processor_based_exec_ctrl2.uint32;

	g_vmx_fixed.fixed_1_vm_exit_ctrl.uint32 =
		g_vmx_constraints.may0_vm_exit_ctrl.
		uint32 & g_vmx_constraints.may1_vm_exit_ctrl.uint32;
	g_vmx_fixed.fixed_0_vm_exit_ctrl.uint32 =
		g_vmx_constraints.may0_vm_exit_ctrl.
		uint32 | g_vmx_constraints.may1_vm_exit_ctrl.uint32;

	g_vmx_fixed.fixed_1_vm_entry_ctrl.uint32 =
		g_vmx_constraints.may0_vm_entry_ctrl.
		uint32 & g_vmx_constraints.may1_vm_entry_ctrl.uint32;
	g_vmx_fixed.fixed_0_vm_entry_ctrl.uint32 =
		g_vmx_constraints.may0_vm_entry_ctrl.
		uint32 | g_vmx_constraints.may1_vm_entry_ctrl.uint32;

	g_vmx_fixed.fixed_1_cr0.uint64 = g_vmx_constraints.may0_cr0.uint64 &
					 g_vmx_constraints.may1_cr0.uint64;
	/* Is unrestricted guest is supported than FIXED1 value should
	 * not have PG and PE */
	if (g_vmx_constraints.unrestricted_guest_supported) {
		g_vmx_fixed.fixed_1_cr0.uint64 &=
			MASK_PE_PG_OFF_UNRESTRICTED_GUEST;
	}
	g_vmx_fixed.fixed_0_cr0.uint64 = g_vmx_constraints.may0_cr0.uint64 |
					 g_vmx_constraints.may1_cr0.uint64;

	g_vmx_fixed.fixed_1_cr4.uint64 = g_vmx_constraints.may0_cr4.uint64 &
					 g_vmx_constraints.may1_cr4.uint64;
	g_vmx_fixed.fixed_0_cr4.uint64 = g_vmx_constraints.may0_cr4.uint64 |
					 g_vmx_constraints.may1_cr4.uint64;

	MON_DEBUG_CODE(print_vmx_capabilities());
}

INLINE mon_phys_mem_type_t vmcs_memory_type(void)
{
	switch (g_vmx_capabilities.vmcs_revision_identifier.bits.
		vmcs_memory_type) {
	case 0:
		return MON_PHYS_MEM_UNCACHABLE;
	case 6:
		return MON_PHYS_MEM_WRITE_BACK;
	default:
		break;
	}

	MON_LOG(mask_anonymous, level_trace,
		"FATAL: Unsupported memory type for VMCS region"
		" in ia32_vmx_capabilities_t\n");
	MON_ASSERT(FALSE);
	return MON_PHYS_MEM_UNDEFINED;
}

#ifdef DEBUG
/*
 * Print capabilities
 */
static void print_vmx_capabilities(void)
{
	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"---------------- Discovered VMX capabilities --------------\n");
	MON_LOG(mask_anonymous, level_trace, "Legend:\n");
	MON_LOG(mask_anonymous, level_trace, "    X - may  be set to 0 or 1\n");
	MON_LOG(mask_anonymous, level_trace, "    0 - must be set to 0\n");
	MON_LOG(mask_anonymous, level_trace, "    1 - must be set to 1\n");

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace, "Raw data values\n");
	MON_LOG(mask_anonymous, level_trace,
		"====================================================="
		"==========================\n");
	MON_LOG(mask_anonymous,
		level_trace,
		"vmcs_revision_identifier                              = %18P\n",
		g_vmx_capabilities.vmcs_revision_identifier.uint64);
	MON_LOG(mask_anonymous,
		level_trace,
		"pin_based_vm_execution_controls - may be set to 0       = %P\n",
		g_vmx_constraints.may0_pin_based_exec_ctrl.uint32);
	MON_LOG(mask_anonymous,
		level_trace,
		"pin_based_vm_execution_controls - may be set to 1       = %P\n",
		g_vmx_constraints.may1_pin_based_exec_ctrl.uint32);
	MON_LOG(mask_anonymous,
		level_trace,
		"processor_based_vm_execution_controls - may be set to 0 = %P\n",
		g_vmx_constraints.may0_processor_based_exec_ctrl.uint32);
	MON_LOG(mask_anonymous,
		level_trace,
		"processor_based_vm_execution_controls - may be set to 1 = %P\n",
		g_vmx_constraints.may1_processor_based_exec_ctrl.uint32);
	MON_LOG(mask_anonymous,
		level_trace,
		"processor_based_vm_execution_controls2 - may be set to 0= %P\n",
		g_vmx_constraints.may0_processor_based_exec_ctrl2.uint32);
	MON_LOG(mask_anonymous,
		level_trace,
		"processor_based_vm_execution_controls2 - may be set to 1= %P\n",
		g_vmx_constraints.may1_processor_based_exec_ctrl2.uint32);
	MON_LOG(mask_anonymous, level_trace,
		"vm_exit_controls - may be set to 0                    = %P\n",
		g_vmx_constraints.may0_vm_exit_ctrl.uint32);
	MON_LOG(mask_anonymous, level_trace,
		"vm_exit_controls - may be set to 1                    = %P\n",
		g_vmx_constraints.may1_vm_exit_ctrl.uint32);
	MON_LOG(mask_anonymous, level_trace,
		"vm_entry_controls - may be set to 0                   = %P\n",
		g_vmx_constraints.may0_vm_entry_ctrl.uint32);
	MON_LOG(mask_anonymous, level_trace,
		"vm_entry_controls - may be set to 1                   = %P\n",
		g_vmx_constraints.may1_vm_entry_ctrl.uint32);
	MON_LOG(mask_anonymous, level_trace,
		"miscellaneous_data                                   = %18P\n",
		g_vmx_capabilities.miscellaneous_data.uint64);
	MON_LOG(mask_anonymous,
		level_trace,
		"cr0_may_be_set_to_zero                                   = %18P\n",
		g_vmx_capabilities.cr0_may_be_set_to_zero.uint64);
	MON_LOG(mask_anonymous,
		level_trace,
		"cr0_may_be_set_to_one                                    = %18P\n",
		g_vmx_capabilities.cr0_may_be_set_to_one.uint64);
	MON_LOG(mask_anonymous,
		level_trace,
		"cr4_may_be_set_to_zero                                   = %18P\n",
		g_vmx_capabilities.cr4_may_be_set_to_zero.uint64);
	MON_LOG(mask_anonymous,
		level_trace,
		"cr4_may_be_set_to_one                                    = %18P\n",
		g_vmx_capabilities.cr4_may_be_set_to_one.uint64);
	MON_LOG(mask_anonymous, level_trace,
		"EptVPIDCapabilities                                 = %18P\n",
		g_vmx_capabilities.ept_vpid_capabilities.uint64);

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"Global data                                 Values\n");
	MON_LOG(mask_anonymous,
		level_trace,
		"========================================= ===================\n");
	MON_LOG(mask_anonymous,
		level_trace,
		"VMCS revision Identifier                  = 0x%08X\n",
		g_vmx_capabilities.vmcs_revision_identifier.bits.revision_identifier);
	MON_LOG(mask_anonymous,
		level_trace,
		"VMCS Region size                          = %d bytes\n",
		g_vmx_capabilities.vmcs_revision_identifier.bits.vmcs_region_size);
	MON_LOG(mask_anonymous, level_trace,
		"Physical address Width                    = %d\n",
		g_vmx_capabilities.vmcs_revision_identifier.
		bits.physical_address_width);
	MON_LOG(mask_anonymous, level_trace,
		"Dual Monitor SMIs                         = %d\n",
		g_vmx_capabilities.vmcs_revision_identifier.
		bits.dual_monitor_system_management_interrupts);
	MON_LOG(mask_anonymous, level_trace,
		"VMCS memory type                          = %s (%d)\n",
		(vmcs_memory_type() == MON_PHYS_MEM_UNCACHABLE) ? "UC" : "WB",
		vmcs_memory_type());
	MON_LOG(mask_anonymous, level_trace,
		"VMCS Instr Info on IO is valid            = %c\n",
		g_vmx_capabilities.vmcs_revision_identifier.
		bits.vmcs_instruction_info_field_on_io_is_valid + '0');
	MON_LOG(mask_anonymous, level_trace,
		"VMX Timer length                          = %d TSC ticks\n",
		1 << g_vmx_capabilities.miscellaneous_data.
			bits.preemption_timer_length);
	MON_LOG(mask_anonymous, level_trace,
		"VmEntry in HLT State Supported            = %c\n",
		g_vmx_constraints.vm_entry_in_halt_state_supported + '0');
	MON_LOG(mask_anonymous, level_trace,
		"VmEntry in SHUTDOWN State Supported       = %c\n",
		g_vmx_constraints.vm_entry_in_shutdown_state_supported + '0');
	MON_LOG(mask_anonymous,
		level_trace,
		"VmEntry in Wait-For-SIPI State Supported  = %c\n",
		g_vmx_constraints.vm_entry_in_wait_for_sipi_state_supported +
		'0');
	MON_LOG(mask_anonymous, level_trace,
		"number of CR3 Target Values               = %d\n",
		g_vmx_constraints.number_of_cr3_target_values);
	MON_LOG(mask_anonymous, level_trace,
		"Max size of MSR Lists                     = %d bytes\n",
		g_vmx_constraints.max_msr_lists_size_in_bytes);
	MON_LOG(mask_anonymous,
		level_trace,
		"MSEG revision Identifier                  = 0x%08x\n",
		g_vmx_capabilities.miscellaneous_data.bits.mseg_revision_identifier);

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"Pin-Based VM Execution Controls        Value\n");
	MON_LOG(mask_anonymous, level_trace,
		"=====================================  =====\n");
	MON_LOG(mask_anonymous, level_trace,
		"external_interrupt                        %c\n",
		VMCS_FIXED_BIT_2_CHAR(pin_based_exec_ctrl, external_interrupt));
	MON_LOG(mask_anonymous, level_trace,
		"host_interrupt                            %c\n",
		VMCS_FIXED_BIT_2_CHAR(pin_based_exec_ctrl, host_interrupt));
	MON_LOG(mask_anonymous, level_trace,
		"Init                                     %c\n",
		VMCS_FIXED_BIT_2_CHAR(pin_based_exec_ctrl, init));
	MON_LOG(mask_anonymous, level_trace,
		"nmi                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(pin_based_exec_ctrl, nmi));
	MON_LOG(mask_anonymous, level_trace,
		"sipi                                     %c\n",
		VMCS_FIXED_BIT_2_CHAR(pin_based_exec_ctrl, sipi));
	MON_LOG(mask_anonymous, level_trace,
		"virtual_nmi                               %c\n",
		VMCS_FIXED_BIT_2_CHAR(pin_based_exec_ctrl, virtual_nmi));
	MON_LOG(mask_anonymous, level_trace,
		"vmx_timer                                 %c\n",
		VMCS_FIXED_BIT_2_CHAR(pin_based_exec_ctrl, vmx_timer));

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"Processor-Based VM Execution Controls  Value\n");
	MON_LOG(mask_anonymous, level_trace,
		"=====================================  =====\n");
	MON_LOG(mask_anonymous, level_trace,
		"software_interrupt                        %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			software_interrupt));
	MON_LOG(mask_anonymous, level_trace,
		"triple_fault                              %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, triple_fault));
	MON_LOG(mask_anonymous, level_trace,
		"virtual_interrupt                         %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			virtual_interrupt));
	MON_LOG(mask_anonymous, level_trace,
		"use_tsc_offsetting                         %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			use_tsc_offsetting));
	MON_LOG(mask_anonymous, level_trace,
		"task_switch                               %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, task_switch));
	MON_LOG(mask_anonymous, level_trace,
		"cpuid                                    %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, cpuid));
	MON_LOG(mask_anonymous, level_trace,
		"get_sec                                   %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, get_sec));
	MON_LOG(mask_anonymous, level_trace,
		"hlt                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, hlt));
	MON_LOG(mask_anonymous, level_trace,
		"invd                                     %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, invd));
	MON_LOG(mask_anonymous, level_trace,
		"invlpg                                   %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, invlpg));
	MON_LOG(mask_anonymous, level_trace,
		"mwait                                    %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, mwait));
	MON_LOG(mask_anonymous, level_trace,
		"rdpmc                                    %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, rdpmc));
	MON_LOG(mask_anonymous, level_trace,
		"rdtsc                                    %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, rdtsc));
	MON_LOG(mask_anonymous, level_trace,
		"rsm                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, rsm));
	MON_LOG(mask_anonymous, level_trace,
		"vm_instruction                            %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			vm_instruction));
	MON_LOG(mask_anonymous, level_trace,
		"cr3_load                                  %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, cr3_load));
	MON_LOG(mask_anonymous, level_trace,
		"cr3_store                                 %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, cr3_store));
	MON_LOG(mask_anonymous, level_trace,
		"use_cr3_mask                               %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, use_cr3_mask));
	MON_LOG(mask_anonymous, level_trace,
		"use_cr3_read_shadow                         %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			use_cr3_read_shadow));
	MON_LOG(mask_anonymous, level_trace,
		"cr8_load                                  %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, cr8_load));
	MON_LOG(mask_anonymous, level_trace,
		"cr8_store                                 %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, cr8_store));
	MON_LOG(mask_anonymous, level_trace,
		"tpr_shadow                                %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, tpr_shadow));
	MON_LOG(mask_anonymous, level_trace,
		"nmi_window                                %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, nmi_window));
	MON_LOG(mask_anonymous, level_trace,
		"mov_dr                                    %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, mov_dr));
	MON_LOG(mask_anonymous, level_trace,
		"unconditional_io                          %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			unconditional_io));
	MON_LOG(mask_anonymous, level_trace,
		"activate_io_bitmaps                        %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			activate_io_bitmaps));
	MON_LOG(mask_anonymous, level_trace,
		"msr_protection                            %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			msr_protection));
	MON_LOG(mask_anonymous, level_trace,
		"monitor_trap_flag                          %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			monitor_trap_flag));
	MON_LOG(mask_anonymous, level_trace,
		"use_msr_bitmaps                            %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			use_msr_bitmaps));
	MON_LOG(mask_anonymous, level_trace,
		"monitor                                  %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, monitor));
	MON_LOG(mask_anonymous, level_trace,
		"pause                                    %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl, pause));
	MON_LOG(mask_anonymous, level_trace,
		"secondary_controls                        %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl,
			secondary_controls));

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"Processor-Based VM Execution Controls2  Value\n");
	MON_LOG(mask_anonymous, level_trace,
		"(Valid only if secondary_controls is not fixed 0)\n");
	MON_LOG(mask_anonymous, level_trace,
		"=====================================  =====\n");
	MON_LOG(mask_anonymous, level_trace,
		"virtualize_apic                           %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl2,
			virtualize_apic));
	MON_LOG(mask_anonymous, level_trace,
		"enable_ept                                %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl2, enable_ept));
	MON_LOG(mask_anonymous, level_trace,
		"Unrestricted Guest                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl2,
			unrestricted_guest));
	MON_LOG(mask_anonymous, level_trace,
		"descriptor_table_exiting                   %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl2,
			descriptor_table_exiting));
	MON_LOG(mask_anonymous, level_trace,
		"enable_rdtscp                             %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl2,
			enable_rdtscp));
	MON_LOG(mask_anonymous, level_trace,
		"enable_invpcid                            %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl2,
			enable_invpcid));
	MON_LOG(mask_anonymous, level_trace,
		"shadow_apic_msrs                           %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl2,
			shadow_apic_msrs));
	MON_LOG(mask_anonymous, level_trace,
		"enable_vpid                               %c\n",
		VMCS_FIXED_BIT_2_CHAR(processor_based_exec_ctrl2, enable_vpid));

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"VM Exit Controls                       Value\n");
	MON_LOG(mask_anonymous, level_trace,
		"=====================================  =====\n");
	MON_LOG(mask_anonymous, level_trace,
		"save_cr0_and_cr4                            %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_cr0_and_cr4));
	MON_LOG(mask_anonymous, level_trace,
		"save_cr3                                  %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_cr3));
	MON_LOG(mask_anonymous, level_trace,
		"save_debug_controls                        %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_debug_controls));
	MON_LOG(mask_anonymous, level_trace,
		"save_segment_registers                     %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_segment_registers));
	MON_LOG(mask_anonymous, level_trace,
		"save_esp_eip_eflags                         %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_esp_eip_eflags));
	MON_LOG(mask_anonymous, level_trace,
		"save_pending_debug_exceptions               %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl,
			save_pending_debug_exceptions));
	MON_LOG(mask_anonymous, level_trace,
		"save_interruptibility_information          %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl,
			save_interruptibility_information));
	MON_LOG(mask_anonymous, level_trace,
		"save_activity_state                        %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_activity_state));
	MON_LOG(mask_anonymous, level_trace,
		"save_working_vmcs_pointer                   %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_working_vmcs_pointer));
	MON_LOG(mask_anonymous, level_trace,
		"ia32e_mode_host                            %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, ia32e_mode_host));
	MON_LOG(mask_anonymous, level_trace,
		"load_cr0_and_cr4                            %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, load_cr0_and_cr4));
	MON_LOG(mask_anonymous, level_trace,
		"load_cr3                                  %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, load_cr3));
	MON_LOG(mask_anonymous, level_trace,
		"load_segment_registers                     %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, load_segment_registers));
	MON_LOG(mask_anonymous, level_trace,
		"load_esp_eip                               %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, load_esp_eip));
	MON_LOG(mask_anonymous, level_trace,
		"acknowledge_interrupt_on_exit               %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl,
			acknowledge_interrupt_on_exit));
	MON_LOG(mask_anonymous, level_trace,
		"save_sys_enter_msrs                         %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_sys_enter_msrs));
	MON_LOG(mask_anonymous, level_trace,
		"load_sys_enter_msrs                         %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, load_sys_enter_msrs));
	MON_LOG(mask_anonymous, level_trace,
		"save_pat                                  %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_pat));
	MON_LOG(mask_anonymous, level_trace,
		"load_pat                                  %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, load_pat));
	MON_LOG(mask_anonymous, level_trace,
		"save_efer                                 %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_efer));
	MON_LOG(mask_anonymous, level_trace,
		"load_efer                                 %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, load_efer));
	MON_LOG(mask_anonymous, level_trace,
		"save_vmx_timer                             %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_exit_ctrl, save_vmx_timer));

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"VM Entry Controls                      Value\n");
	MON_LOG(mask_anonymous, level_trace,
		"=====================================  =====\n");
	MON_LOG(mask_anonymous, level_trace,
		"load_cr0_and_cr4                            %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, load_cr0_and_cr4));
	MON_LOG(mask_anonymous, level_trace,
		"load_cr3                                  %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, load_cr3));
	MON_LOG(mask_anonymous, level_trace,
		"load_debug_controls                        %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, load_debug_controls));
	MON_LOG(mask_anonymous, level_trace,
		"load_segment_registers                     %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, load_segment_registers));
	MON_LOG(mask_anonymous, level_trace,
		"load_esp_eip_eflags                         %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, load_esp_eip_eflags));
	MON_LOG(mask_anonymous, level_trace,
		"load_pending_debug_exceptions               %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl,
			load_pending_debug_exceptions));
	MON_LOG(mask_anonymous, level_trace,
		"load_interruptibility_information          %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl,
			load_interruptibility_information));
	MON_LOG(mask_anonymous, level_trace,
		"load_activity_state                        %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, load_activity_state));
	MON_LOG(mask_anonymous, level_trace,
		"load_working_vmcs_pointer                   %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl,
			load_working_vmcs_pointer));
	MON_LOG(mask_anonymous, level_trace,
		"ia32e_mode_guest                           %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, ia32e_mode_guest));
	MON_LOG(mask_anonymous, level_trace,
		"entry_to_smm                               %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, entry_to_smm));
	MON_LOG(mask_anonymous, level_trace,
		"tear_down_smm_monitor                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, tear_down_smm_monitor));
	MON_LOG(mask_anonymous, level_trace,
		"load_sys_enter_msrs                         %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, load_sys_enter_msrs));
	MON_LOG(mask_anonymous, level_trace,
		"load_pat                                  %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, load_pat));
	MON_LOG(mask_anonymous, level_trace,
		"load_efer                                 %c\n",
		VMCS_FIXED_BIT_2_CHAR(vm_entry_ctrl, load_efer));

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"Cr0 bits                               Value\n");
	MON_LOG(mask_anonymous, level_trace,
		"=====================================  =====\n");
	MON_LOG(mask_anonymous, level_trace,
		"pe                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, pe));
	MON_LOG(mask_anonymous, level_trace,
		"mp                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, mp));
	MON_LOG(mask_anonymous, level_trace,
		"em                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, em));
	MON_LOG(mask_anonymous, level_trace,
		"ts                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, ts));
	MON_LOG(mask_anonymous, level_trace,
		"et                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, et));
	MON_LOG(mask_anonymous, level_trace,
		"ne                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, ne));
	MON_LOG(mask_anonymous, level_trace,
		"WP                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, wp));
	MON_LOG(mask_anonymous, level_trace,
		"am                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, am));
	MON_LOG(mask_anonymous, level_trace,
		"nw                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, nw));
	MON_LOG(mask_anonymous, level_trace,
		"cd                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, cd));
	MON_LOG(mask_anonymous, level_trace,
		"pg                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr0, pg));

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"cr4 bits                               Value\n");
	MON_LOG(mask_anonymous, level_trace,
		"=====================================  =====\n");
	MON_LOG(mask_anonymous, level_trace,
		"vme                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, vme));
	MON_LOG(mask_anonymous, level_trace,
		"pvi                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, pvi));
	MON_LOG(mask_anonymous, level_trace,
		"tsd                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, tsd));
	MON_LOG(mask_anonymous, level_trace,
		"de                                       %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, de));
	MON_LOG(mask_anonymous, level_trace,
		"pse                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, pse));
	MON_LOG(mask_anonymous, level_trace,
		"pae                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, pae));
	MON_LOG(mask_anonymous, level_trace,
		"mce                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, mce));
	MON_LOG(mask_anonymous, level_trace,
		"pge                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, pge));
	MON_LOG(mask_anonymous, level_trace,
		"pce                                      %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, pce));
	MON_LOG(mask_anonymous, level_trace,
		"osfxsr                                   %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, osfxsr));
	MON_LOG(mask_anonymous, level_trace,
		"osxmmexcpt                               %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, osxmmexcpt));
	MON_LOG(mask_anonymous, level_trace,
		"vmxe                                     %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, vmxe));
	MON_LOG(mask_anonymous, level_trace,
		"smxe                                     %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, smxe));
	MON_LOG(mask_anonymous, level_trace,
		"osxsave                                  %c\n",
		VMCS_FIXED_BIT_2_CHAR(cr4, osxsave));

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"EPT & VPID Capabilities                Value\n");
	MON_LOG(mask_anonymous, level_trace,
		"(Valid only if enable_ept or enable_vpid is not fixed 0)\n");
	MON_LOG(mask_anonymous, level_trace,
		"=====================================  =====\n");
	MON_LOG(mask_anonymous, level_trace,
		"x_only                                   %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, x_only));
	MON_LOG(mask_anonymous, level_trace,
		"w_only                                   %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, w_only));
	MON_LOG(mask_anonymous, level_trace,
		"w_and_x_only                             %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, w_and_x_only));
	MON_LOG(mask_anonymous, level_trace,
		"gaw_21_bit                               %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, gaw_21_bit));
	MON_LOG(mask_anonymous, level_trace,
		"gaw_30_bit                               %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, gaw_30_bit));
	MON_LOG(mask_anonymous, level_trace,
		"gaw_39_bit                               %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, gaw_39_bit));
	MON_LOG(mask_anonymous, level_trace,
		"gaw_48_bit                               %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, gaw_48_bit));
	MON_LOG(mask_anonymous, level_trace,
		"gaw_57_bit                               %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, gaw_57_bit));
	MON_LOG(mask_anonymous, level_trace,
		"UC                                       %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, uc));
	MON_LOG(mask_anonymous, level_trace,
		"WC                                       %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, wc));
	MON_LOG(mask_anonymous, level_trace,
		"WT                                       %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, wt));
	MON_LOG(mask_anonymous, level_trace,
		"WP                                       %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, wp));
	MON_LOG(mask_anonymous, level_trace,
		"WB                                       %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, wb));
	MON_LOG(mask_anonymous, level_trace,
		"sp_21_bit                                %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, sp_21_bit));
	MON_LOG(mask_anonymous, level_trace,
		"sp_30_bit                                %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, sp_30_bit));
	MON_LOG(mask_anonymous, level_trace,
		"sp_39_bit                                %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, sp_39_bit));
	MON_LOG(mask_anonymous, level_trace,
		"sp_48_bit                                %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, sp_48_bit));
	MON_LOG(mask_anonymous, level_trace,
		"invept_supported                          %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, invept_supported));
	MON_LOG(mask_anonymous, level_trace,
		"invept_individual_address                  %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities,
			invept_individual_address));
	MON_LOG(mask_anonymous, level_trace,
		"invept_context_wide                        %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, invept_context_wide));
	MON_LOG(mask_anonymous, level_trace,
		"invept_all_contexts                        %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, invept_all_contexts));
	MON_LOG(mask_anonymous, level_trace,
		"invvpid_supported                         %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, invvpid_supported));
	MON_LOG(mask_anonymous, level_trace,
		"invvpid_individual_address                 %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities,
			invvpid_individual_address));
	MON_LOG(mask_anonymous, level_trace,
		"invvpid_context_wide                       %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, invvpid_context_wide));
	MON_LOG(mask_anonymous, level_trace,
		"invvpid_all_contexts                       %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities, invvpid_all_contexts));
	MON_LOG(mask_anonymous, level_trace,
		"invvpid_all_contexts_preserving_globals      %c\n",
		CAP_BIT_TO_CHAR(ept_vpid_capabilities,
			invvpid_all_contexts_preserving_globals));

	MON_LOG(mask_anonymous, level_trace, "\n");
	MON_LOG(mask_anonymous, level_trace,
		"------------- End of Discovered VMX capabilities -----------\n");
	MON_LOG(mask_anonymous, level_trace, "\n");
}
#endif                          /* DEBUG */

/*------------------------------ interface functions----------------------- */

void vmcs_hw_init(void)
{
	if (g_init_done) {
		return;
	}

	mon_memset(&g_vmx_capabilities, 0, sizeof(g_vmx_capabilities));
	mon_memset(&g_vmx_constraints, 0, sizeof(g_vmx_constraints));
	mon_memset(&g_vmx_fixed, 0, sizeof(g_vmx_fixed));

	g_init_done = TRUE;

	fill_vmx_capabilities();
}

/*
 * Allocate VMCS region
 */
hva_t vmcs_hw_allocate_region(hpa_t *hpa)
{
	hva_t hva = 0;
	ia32_vmx_vmcs_t *vmcs = 0;

	MON_ASSERT(hpa);

	/* allocate the VMCS area */
	/* the area must be 4K page aligned and zeroed */
	hva = (hva_t)mon_memory_alloc(VMCS_REGION_SIZE);

	MON_ASSERT(hva);

	if (!mon_hmm_hva_to_hpa(hva, hpa)) {
		MON_LOG(mask_anonymous, level_trace,
			"%s:(%d):ASSERT: hva_t to hpa_t conversion failed\n",
			__FUNCTION__, __LINE__);
		MON_DEADLOOP();
	}

	MON_DEBUG_CODE(
		if (!VMCS_ABOVE_4G_SUPPORTED) {
			if (*hpa > (hpa_t)MAX_32BIT_NUMBER) {
				MON_LOG(mask_anonymous, level_trace,
					"Heap allocated VMCS object above 4G. Current CPU"
					" does not support this\n");
				MON_ASSERT(*hpa < (hpa_t)MAX_32BIT_NUMBER);
			}
		}
		)

	/* check VMCS memory type */
	MON_ASSERT(hmm_does_memory_range_have_specified_memory_type
			(*hpa, VMCS_REGION_SIZE, VMCS_MEMORY_TYPE) == TRUE);

	vmcs = (ia32_vmx_vmcs_t *)hva;

	vmcs->revision_identifier = VMCS_REVISION;

	/* unmap VMCS region from the host memory */
	if (!hmm_unmap_hpa
		    (*hpa,
		    ALIGN_FORWARD(VMCS_REGION_SIZE, PAGE_4KB_SIZE), FALSE)) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ERROR: failed to unmap VMCS\n");
		MON_DEADLOOP();
	}

	return hva;
}

/*
 * allocate vmxon regions for all processors at once
 * must be called once only on BSP before vmx_on on any APs.
 * */
boolean_t vmcs_hw_allocate_vmxon_regions(uint16_t max_host_cpus)
{
	hva_t vmxon_region_hva = 0;
	hpa_t vmxon_region_hpa = 0;
	uint16_t cpu_idx = 0;

	MON_ASSERT(max_host_cpus);

	for (cpu_idx = 0; cpu_idx < max_host_cpus; cpu_idx++) {
		vmxon_region_hva = vmcs_hw_allocate_region(&vmxon_region_hpa);
		host_cpu_set_vmxon_region(vmxon_region_hva,
			vmxon_region_hpa,
			cpu_idx);
	}

	return TRUE;
}

/*
 * get constraints
 */
const vmcs_hw_constraints_t *mon_vmcs_hw_get_vmx_constraints(void)
{
	if (!g_init_done) {
		vmcs_hw_init();
	}

	return &g_vmx_constraints;
}

/*-------------------------------------------------------------------------
 *
 * Check that current CPU is VMX-capable
 *
 *------------------------------------------------------------------------- */
boolean_t vmcs_hw_is_cpu_vmx_capable(void)
{
	cpuid_info_struct_t cpuid_info;
	ia32_msr_opt_in_t opt_in;
	boolean_t ok = FALSE;

	/* 1. CPUID[EAX=1] should have VMX feature == 1
	 * 2. OPT_IN (FEATURE_CONTROL) MSR should have
	 * either enable_vmxon_outside_smx == 1 or
	 * Lock == 0 */

	cpuid(&cpuid_info, 1);

	if ((CPUID_VALUE_ECX(cpuid_info) & IA32_CPUID_ECX_VMX) == 0) {
		MON_LOG(mask_anonymous, level_trace,
			"ASSERT: CPUID[EAX=1] indicates that Host CPU #%d"
			" does not support VMX!\n",
			hw_cpu_id());
		return FALSE;
	}

	opt_in.uint64 = hw_read_msr(IA32_MSR_OPT_IN_INDEX);

	ok =
		((opt_in.bits.enable_vmxon_outside_smx == 1) ||
	 (opt_in.bits.lock == 0));

	MON_DEBUG_CODE({
			if (!ok) {
				MON_LOG(mask_anonymous, level_trace,
					"ASSERT: OPT_IN (FEATURE_CONTROL) MSR indicates"
					" that somebody locked-out VMX on Host CPU #%d\n",
					hw_cpu_id());
			}
		}
		)

	return ok;
}

/*-------------------------------------------------------------------------
 *
 * Enable VT on the current CPU
 *
 *------------------------------------------------------------------------- */
void vmcs_hw_vmx_on(void)
{
	ia32_msr_opt_in_t opt_in;
	em64t_cr4_t cr4;
	hva_t vmxon_region_hva = 0;
	hpa_t vmxon_region_hpa = 0;
	hw_vmx_ret_value_t vmx_ret;

	/* Enable VMX in CR4 */
	cr4.uint64 = hw_read_cr4();
	cr4.bits.vmxe = 1;
	hw_write_cr4(cr4.uint64);

	/* Enable VMX outside SMM in OPT_IN (FEATURE_CONTROL) MSR and lock it */
	opt_in.uint64 = hw_read_msr(IA32_MSR_OPT_IN_INDEX);

	MON_ASSERT((opt_in.bits.lock == 0)
		|| (opt_in.bits.enable_vmxon_outside_smx == 1));

	if (opt_in.bits.lock == 0) {
		opt_in.bits.enable_vmxon_outside_smx = 1;
		opt_in.bits.lock = 1;

		hw_write_msr(IA32_MSR_OPT_IN_INDEX, opt_in.uint64);
	}

	vmxon_region_hva = host_cpu_get_vmxon_region(&vmxon_region_hpa);
	if (!vmxon_region_hva || !vmxon_region_hpa) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ASSERT: VMXON failed with getting vmxon_region address\n");
		MON_DEADLOOP();
	}

	vmx_ret = hw_vmx_on(&vmxon_region_hpa);

	switch (vmx_ret) {
	case HW_VMX_SUCCESS:
		host_cpu_set_vmx_state(TRUE);
		break;

	case HW_VMX_FAILED_WITH_STATUS:
		MON_LOG(mask_anonymous,
			level_trace,
			"ASSERT: VMXON failed with HW_VMX_FAILED_WITH_STATUS error\n");
		MON_DEADLOOP();
		MON_BREAKPOINT();

	case HW_VMX_FAILED:
	default:
		MON_LOG(mask_anonymous, level_trace,
			"ASSERT: VMXON failed with HW_VMX_FAILED error\n");
		MON_DEADLOOP();
		MON_BREAKPOINT();
	}
}

/*-------------------------------------------------------------------------
 *
 * Disable VT on the current CPU
 *
 *------------------------------------------------------------------------- */
void vmcs_hw_vmx_off(void)
{
	if (host_cpu_get_vmx_state() == FALSE) {
		return;
	}

	hw_vmx_off();
	host_cpu_set_vmx_state(FALSE);
}
