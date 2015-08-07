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

#ifndef _MON_API_H
#define _MON_API_H

#include "mon_defs.h"
#include "host_memory_manager_api.h"
#include "memory_address_mapper_api.h"
#include "list.h"
#include "mon_dbg.h"
#include "lock.h"
#include "fvs.h"
#include "ve.h"
#include "msr_defs.h"
#include "ipc.h"

/* Control States */
typedef enum {
	MON_VPID = 0,                    /* read/w directly */
	MON_EPTP_INDEX,
	MON_CONTROL_VECTOR_PIN_EVENTS,
	/* Special case - nmi_window cannot be updated using this value. Use
	 * special APIs to update nmi_window setting */
	MON_CONTROL_VECTOR_PROCESSOR_EVENTS,
	MON_CONTROL2_VECTOR_PROCESSOR_EVENTS,
	MON_EXCEPTION_BITMAP,
	MON_CR3_TARGET_COUNT,
	MON_CR0_MASK,
	MON_CR4_MASK,
	MON_CR0_READ_SHADOW,
	MON_CR4_READ_SHADOW,
	MON_PAGE_FAULT_ERROR_CODE_MASK,
	MON_PAGE_FAULT_ERROR_CODE_MATCH,
	MON_EXIT_CONTROL_VECTOR,
	MON_EXIT_MSR_STORE_COUNT,
	MON_EXIT_MSR_LOAD_COUNT,
	MON_ENTER_CONTROL_VECTOR,
	MON_ENTER_INTERRUPT_INFO,
	MON_ENTER_EXCEPTION_ERROR_CODE,
	MON_ENTER_INSTRUCTION_LENGTH,
	MON_ENTER_MSR_LOAD_COUNT,
	MON_IO_BITMAP_ADDRESS_A,
	MON_IO_BITMAP_ADDRESS_B,
	MON_MSR_BITMAP_ADDRESS,
	MON_EXIT_MSR_STORE_ADDRESS,
	MON_EXIT_MSR_LOAD_ADDRESS,
	MON_ENTER_MSR_LOAD_ADDRESS,
	MON_OSV_CONTROLLING_VMCS_ADDRESS,
	MON_TSC_OFFSET,
	MON_EXIT_INFO_GUEST_PHYSICAL_ADDRESS, /* read only */
	MON_EXIT_INFO_INSTRUCTION_ERROR_CODE,
	MON_EXIT_INFO_REASON,
	MON_EXIT_INFO_EXCEPTION_INFO,
	MON_EXIT_INFO_EXCEPTION_ERROR_CODE,
	MON_EXIT_INFO_IDT_VECTORING,
	MON_EXIT_INFO_IDT_VECTORING_ERROR_CODE,
	MON_EXIT_INFO_INSTRUCTION_LENGTH,
	MON_EXIT_INFO_INSTRUCTION_INFO,
	MON_EXIT_INFO_QUALIFICATION,
	MON_EXIT_INFO_IO_RCX,
	MON_EXIT_INFO_IO_RSI,
	MON_EXIT_INFO_IO_RDI,
	MON_EXIT_INFO_IO_RIP,
	MON_EXIT_INFO_GUEST_LINEAR_ADDRESS, /* read only */
	MON_VIRTUAL_APIC_ADDRESS,
	MON_APIC_ACCESS_ADDRESS,
	MON_EXIT_TPR_THRESHOLD,
	MON_EPTP_ADDRESS,
	MON_CR3_TARGET_VALUE_0,
	MON_CR3_TARGET_VALUE_1,
	MON_CR3_TARGET_VALUE_2,
	MON_CR3_TARGET_VALUE_3,

	MON_VMFUNC_CONTROL,
	MON_VMFUNC_EPTP_LIST_ADDRESS,

	/* last */
	NUM_OF_MON_CONTROL_STATE
} mon_control_state_t;

/* Guest States */
typedef enum {
	/* START: GPRs */
	/* GPRs should be at the start of this structure. Their value should match
	 * the value in mon_ia32_gp_registers_t structure. */
	MON_GUEST_IA32_GP_RAX = 0,
	MON_GUEST_IA32_GP_RBX,
	MON_GUEST_IA32_GP_RCX,
	MON_GUEST_IA32_GP_RDX,
	MON_GUEST_IA32_GP_RDI,
	MON_GUEST_IA32_GP_RSI,
	MON_GUEST_IA32_GP_RBP,
	MON_GUEST_IA32_GP_RSP,
	MON_GUEST_IA32_GP_R8,
	MON_GUEST_IA32_GP_R9,
	MON_GUEST_IA32_GP_R10,
	MON_GUEST_IA32_GP_R11,
	MON_GUEST_IA32_GP_R12,
	MON_GUEST_IA32_GP_R13,
	MON_GUEST_IA32_GP_R14,
	MON_GUEST_IA32_GP_R15,
	/* END: GPRs */
	/* START: VMCS GUEST fields */
	/* The following VMCS fields should match the VMCS_GUEST_xxx fields in
	 * vmcs_field_t structure. */
	MON_GUEST_CR0,
	MON_GUEST_CR3,
	MON_GUEST_CR4,
	MON_GUEST_DR7,
	MON_GUEST_ES_SELECTOR,
	MON_GUEST_ES_BASE,
	MON_GUEST_ES_LIMIT,
	MON_GUEST_ES_AR,
	MON_GUEST_CS_SELECTOR,
	MON_GUEST_CS_BASE,
	MON_GUEST_CS_LIMIT,
	MON_GUEST_CS_AR,
	MON_GUEST_SS_SELECTOR,
	MON_GUEST_SS_BASE,
	MON_GUEST_SS_LIMIT,
	MON_GUEST_SS_AR,
	MON_GUEST_DS_SELECTOR,
	MON_GUEST_DS_BASE,
	MON_GUEST_DS_LIMIT,
	MON_GUEST_DS_AR,
	MON_GUEST_FS_SELECTOR,
	MON_GUEST_FS_BASE,
	MON_GUEST_FS_LIMIT,
	MON_GUEST_FS_AR,
	MON_GUEST_GS_SELECTOR,
	MON_GUEST_GS_BASE,
	MON_GUEST_GS_LIMIT,
	MON_GUEST_GS_AR,
	MON_GUEST_LDTR_SELECTOR,
	MON_GUEST_LDTR_BASE,
	MON_GUEST_LDTR_LIMIT,
	MON_GUEST_LDTR_AR,
	MON_GUEST_TR_SELECTOR,
	MON_GUEST_TR_BASE,
	MON_GUEST_TR_LIMIT,
	MON_GUEST_TR_AR,
	MON_GUEST_GDTR_BASE,
	MON_GUEST_GDTR_LIMIT,
	MON_GUEST_IDTR_BASE,
	MON_GUEST_IDTR_LIMIT,
	MON_GUEST_RSP,
	MON_GUEST_RIP,
	MON_GUEST_RFLAGS,
	MON_GUEST_PEND_DBE,
	MON_GUEST_WORKING_VMCS_PTR,
	MON_GUEST_DEBUG_CONTROL,
	MON_GUEST_INTERRUPTIBILITY,
	MON_GUEST_SLEEP_STATE,
	MON_GUEST_SMBASE,
	MON_GUEST_SYSENTER_CS,
	MON_GUEST_SYSENTER_ESP,
	MON_GUEST_SYSENTER_EIP,
	MON_GUEST_PAT,
	MON_GUEST_EFER,
	MON_GUEST_IA32_PERF_GLOBAL_CTRL,
	MON_GUEST_PDPTR0,
	MON_GUEST_PDPTR1,
	MON_GUEST_PDPTR2,
	MON_GUEST_PDPTR3,
	/* END: VMCS GUEST fields */
	/* START: Other fields */
	/* Any new fields independent of GPRs and VMCS should be added here. */
	MON_GUEST_PREEMPTION_TIMER,
	MON_GUEST_CR8, /* Only valid for 64-bit, undefined behavior in 32-bit */
	/* END: Other fields */
	NUM_OF_MON_GUEST_STATE
} mon_guest_state_t;

typedef union {
	struct {
		uint64_t	mask;
		uint64_t	value;
	} mask_value;
	struct {
		uint64_t	gaw;
		uint64_t	ept_root_table_hpa;
	} ept_value;
	uint64_t value;
} mon_controls_t;

typedef struct {
	/* For setting RIP
	 * TRUE to skip instruction;
	 * FALSE to set RIP to new value */
	boolean_t	skip_rip;
	uint8_t		padding[4];
	uint64_t	value;
} mon_guest_state_value_t;

/*-------------------------------------------------------*
*  PURPOSE  : Get the value of given Guest State ID
*  ARGUMENTS: gcpu          (IN) -- Guest CPU Handle
*             guest_state_id(IN) -- Guest State ID
*             value         (OUT)-- Pointer of the Guest
*                                 State value
*  RETURNS  : TRUE
*             FALSE
*-------------------------------------------------------*/
boolean_t mon_get_vmcs_guest_state(guest_cpu_handle_t gcpu,
				   mon_guest_state_t guest_state_id,
				   mon_guest_state_value_t *value);

/*-------------------------------------------------------*
*  PURPOSE  : Set the value of given Guest State ID to the
*             given value
*  ARGUMENTS: gcpu          (IN) -- Guest CPU Handle
*             guest_state_id(IN) -- Guest State ID
*             value         (IN) -- Given Guest State value
*  RETURNS  : TRUE
*             FALSE
*-------------------------------------------------------*/
boolean_t mon_set_vmcs_guest_state(guest_cpu_handle_t gcpu,
				   mon_guest_state_t guest_state_id,
				   mon_guest_state_value_t value);

/*-------------------------------------------------------*
*  PURPOSE  : Get the value of given Control State ID
*  ARGUMENTS: gcpu            (IN) -- Guest CPU Handle
*             control_state_id(IN) -- Control State ID
*             value           (IN) -- Pointer of the Given
*                                   Control State value
*  RETURNS  : TRUE
*             FALSE
*-------------------------------------------------------*/
boolean_t mon_get_vmcs_control_state(guest_cpu_handle_t gcpu,
				     mon_control_state_t control_state_id,
				     mon_controls_t *value);

/*-------------------------------------------------------*
*  PURPOSE  : Set the value of given Control State ID to
*             the given value
*  ARGUMENTS: gcpu            (IN) -- Guest CPU Handle
*             control_state_id(IN) -- Control State ID
*             value           (IN) -- Pointer of the Given
*                                   Control State value
*  RETURNS  : TRUE
*             FALSE
*-------------------------------------------------------*/
boolean_t mon_set_vmcs_control_state(guest_cpu_handle_t gcpu,
				     mon_control_state_t control_state_id,
				     mon_controls_t *value);

/*-------------------------------------------------------*
*  PURPOSE  : Copy the given memory from given gva to
*             given hva
*  ARGUMENTS: gcpu(IN) -- Guest CPU Handle
*             gva (IN) -- Guest Virtual Address
*             size(IN) -- size of the range from gva
*             hva (IN) -- Pointer of Host Virtual Address
*  RETURNS  : 0 if successful
*-------------------------------------------------------*/
int copy_from_gva(guest_cpu_handle_t gcpu, uint64_t gva, int size,
		  uint64_t hva);

#endif /*_MON_API_H */
