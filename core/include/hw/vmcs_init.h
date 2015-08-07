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

#ifndef _VMCS_INIT_H_
#define _VMCS_INIT_H_

#include "mon_defs.h"
#include "vmx_ctrl_msrs.h"
#include "em64t_defs.h"
#include "vmcs_api.h"

/**************************************************************************
*
* Initialization of the VMCS hardware region
*
**************************************************************************/

typedef struct {
	/* 1 for each bit that may be 1 */
	pin_based_vm_execution_controls_t	may1_pin_based_exec_ctrl;
	/* 0 for each bit that may be 0 */
	pin_based_vm_execution_controls_t	may0_pin_based_exec_ctrl;
	/* 1 for each bit that may be 1 */
	processor_based_vm_execution_controls_t
						may1_processor_based_exec_ctrl;
	/* 0 for each bit that may be 0 */
	processor_based_vm_execution_controls_t
						may0_processor_based_exec_ctrl;
	/* 1 for each bit that may be 1 */
	processor_based_vm_execution_controls2_t
						may1_processor_based_exec_ctrl2;
	/* 0 for each bit that may be 0 */
	processor_based_vm_execution_controls2_t
						may0_processor_based_exec_ctrl2;
	vmexit_controls_t			may1_vm_exit_ctrl;              /* 1 for each bit that may be 1 */
	vmexit_controls_t			may0_vm_exit_ctrl;              /* 0 for each bit that may be 0 */
	vmentry_controls_t			may1_vm_entry_ctrl;             /* 1 for each bit that may be 1 */
	vmentry_controls_t			may0_vm_entry_ctrl;             /* 0 for each bit that may be 0 */
	em64t_cr0_t				may1_cr0;                       /* 1 for each bit that may be 1 */
	em64t_cr0_t				may0_cr0;                       /* 0 for each bit that may be 0 */
	em64t_cr4_t				may1_cr4;                       /* 1 for each bit that may be 1 */
	em64t_cr4_t				may0_cr4;                       /* 0 for each bit that may be 0 */

	uint32_t
						number_of_cr3_target_values;
	uint32_t
						max_msr_lists_size_in_bytes;
	uint32_t				vmx_timer_length;         /* in TSC ticks */
	uint32_t				vmcs_revision;
	uint32_t				mseg_revision_id;
	boolean_t
						vm_entry_in_halt_state_supported;
	boolean_t
						vm_entry_in_shutdown_state_supported;
	boolean_t
						vm_entry_in_wait_for_sipi_state_supported;
	boolean_t
						processor_based_exec_ctrl2_supported;
	boolean_t				ept_supported;
	boolean_t
						unrestricted_guest_supported;
	boolean_t				vpid_supported;

	boolean_t				vmfunc_supported;
	boolean_t				eptp_switching_supported;

	boolean_t				ve_supported;

	ia32_vmx_ept_vpid_cap_t			ept_vpid_capabilities;
} vmcs_hw_constraints_t;

typedef struct {
	/* 1 for each fixed 1 bit */
	pin_based_vm_execution_controls_t
				fixed_1_pin_based_exec_ctrl;
	/* 0 for each fixed 0 bit */
	pin_based_vm_execution_controls_t
				fixed_0_pin_based_exec_ctrl;
	/* 1 for each fixed 1 bit */
	processor_based_vm_execution_controls_t
				fixed_1_processor_based_exec_ctrl;
	/* 0 for each fixed 0 bit */
	processor_based_vm_execution_controls_t
				fixed_0_processor_based_exec_ctrl;
	/* 1 for each fixed 1 bit */
	processor_based_vm_execution_controls2_t
				fixed_1_processor_based_exec_ctrl2;
	/* 0 for each fixed 0 bit */
	processor_based_vm_execution_controls2_t
				fixed_0_processor_based_exec_ctrl2;
	vmexit_controls_t	fixed_1_vm_exit_ctrl;                           /* 1 for each fixed 1 bit */
	vmexit_controls_t	fixed_0_vm_exit_ctrl;                           /* 0 for each fixed 0 bit */
	vmentry_controls_t	fixed_1_vm_entry_ctrl;                          /* 1 for each fixed 1 bit */
	vmentry_controls_t	fixed_0_vm_entry_ctrl;                          /* 0 for each fixed 0 bit */
	em64t_cr0_t		fixed_1_cr0;                                    /* 1 for each fixed 1 bit */
	em64t_cr0_t		fixed_0_cr0;                                    /* 0 for each fixed 0 bit */
	em64t_cr4_t		fixed_1_cr4;                                    /* 1 for each fixed 1 bit */
	em64t_cr4_t		fixed_0_cr4;                                    /* 0 for each fixed 0 bit */
} vmcs_hw_fixed_t;

/* global */
extern vmcs_hw_fixed_t *gp_vmx_fixed;
extern uint64_t g_vmx_fixed_1_cr0_save;

/*-------------------------------------------------------------------------
 *
 * Init
 *
 *------------------------------------------------------------------------- */
void vmcs_hw_init(void);

/*-------------------------------------------------------------------------
 *
 * Check that current CPU is VMX-capable
 *
 *------------------------------------------------------------------------- */
boolean_t vmcs_hw_is_cpu_vmx_capable(void);

/*-------------------------------------------------------------------------
 *
 * Enable VT on the current CPU
 *
 *------------------------------------------------------------------------- */
void vmcs_hw_vmx_on(void);

/*-------------------------------------------------------------------------
 *
 * Disable VT on the current CPU
 *
 *------------------------------------------------------------------------- */
void vmcs_hw_vmx_off(void);

/*-------------------------------------------------------------------------
 *
 * Allocate and initialize VMCS region
 *
 * Returns 2 pointers:
 * Pointer to the allocated VMCS region (HVA)
 * Ponter to the same region (HPA)
 *
 *------------------------------------------------------------------------- */
hva_t vmcs_hw_allocate_region(hpa_t *hpa);

/*-------------------------------------------------------------------------
 *
 * Allocate and initialize vmxon regions for all the processors
 * (called once only on BSP)
 *
 *------------------------------------------------------------------------- */
boolean_t vmcs_hw_allocate_vmxon_regions(uint16_t max_host_cpus);

/*-------------------------------------------------------------------------
 *
 * Get constraint values for various VMCS fields
 *
 *------------------------------------------------------------------------- */
const vmcs_hw_constraints_t *mon_vmcs_hw_get_vmx_constraints(void);

/*-------------------------------------------------------------------------
 *
 * Make hw compliant
 *
 * Ensure, that all bits that are 0 in may0 are also 0 in actual
 * Ensure, that all bits that are 1 in may1 are also 1 in actual
 *
 *------------------------------------------------------------------------- */
INLINE uint32_t vmcs_hw_make_compliant_pin_based_exec_ctrl(uint32_t value)
{
	value &= gp_vmx_fixed->fixed_0_pin_based_exec_ctrl.uint32;
	value |= gp_vmx_fixed->fixed_1_pin_based_exec_ctrl.uint32;
	return value;
}

INLINE uint32_t vmcs_hw_make_compliant_processor_based_exec_ctrl(uint32_t value)
{
	value &= gp_vmx_fixed->fixed_0_processor_based_exec_ctrl.uint32;
	value |= gp_vmx_fixed->fixed_1_processor_based_exec_ctrl.uint32;
	return value;
}

INLINE uint32_t vmcs_hw_make_compliant_processor_based_exec_ctrl2(uint32_t value)
{
	value &= gp_vmx_fixed->fixed_0_processor_based_exec_ctrl2.uint32;
	value |= gp_vmx_fixed->fixed_1_processor_based_exec_ctrl2.uint32;
	return value;
}

INLINE uint32_t vmcs_hw_make_compliant_vm_exit_ctrl(uint32_t value)
{
	value &= gp_vmx_fixed->fixed_0_vm_exit_ctrl.uint32;
	value |= gp_vmx_fixed->fixed_1_vm_exit_ctrl.uint32;
	return value;
}

INLINE uint32_t vmcs_hw_make_compliant_vm_entry_ctrl(uint32_t value)
{
	value &= gp_vmx_fixed->fixed_0_vm_entry_ctrl.uint32;
	value |= gp_vmx_fixed->fixed_1_vm_entry_ctrl.uint32;
	return value;
}

INLINE uint64_t vmcs_hw_make_compliant_cr0(uint64_t value)
{
	value &= gp_vmx_fixed->fixed_0_cr0.uint64;
	value |= gp_vmx_fixed->fixed_1_cr0.uint64;
	return value;
}

INLINE uint64_t vmcs_hw_make_compliant_cr4(uint64_t value)
{
	value &= gp_vmx_fixed->fixed_0_cr4.uint64;
	value |= gp_vmx_fixed->fixed_1_cr4.uint64;
	return value;
}

INLINE uint64_t vmcs_hw_make_compliant_cr8(uint64_t value)
{
	value &= EM64T_CR8_VALID_BITS_MASK;
	return value;
}

#endif                          /* _VMCS_INIT_H_ */
