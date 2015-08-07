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
#include "mon_dbg.h"
#include "heap.h"
#include "vmx_vmcs.h"
#include "vmcs_init.h"
#include "vmcs_api.h"
#include "libc.h"
#include "host_memory_manager_api.h"
#include "memory_allocator.h"
#include "vmcs_internal.h"
#include "cli.h"
#include "mon_api.h"
#include "scheduler.h"
#include "isr.h"
#include "ept.h"
#include "memory_dump.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMCS_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMCS_C, __condition)


#define NO_EXIST            VMCS_NOT_EXISTS
#define READONLY            VMCS_READABLE
#define WRITABLE           (VMCS_READABLE | VMCS_WRITABLE)
#define WRITABLE_IN_CACHE  (VMCS_READABLE | VMCS_WRITABLE_IN_CACHE)

#define FIELD_IS_READABLE(__field)  \
	(0 != (g_field_data[__field].access & VMCS_READABLE))
#define FIELD_IS_WRITEABLE(__field) \
	(0 != \
	 (g_field_data[__field].access & \
	  (VMCS_WRITABLE | VMCS_WRITABLE_IN_CACHE)))

#define FULL_ENC_ONLY   0
#define SUPP_HIGH_ENC   1

/*
 * Minimum size of allocated MSR list
 */
#define MIN_SIZE_OF_MSR_LIST  4

typedef uint8_t field_access_type_t;

/* field encoding and naming */
typedef struct {
	uint32_t		encoding;
	field_access_type_t	access;
	uint8_t			supports_high_encoding;
	uint8_t			pad[2];
	const char		*name;
} vmcs_encoding_t;

static vmcs_encoding_t g_field_data[] = {
	{ VM_X_VPID,				 NO_EXIST,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_VPID"		  },
	{ VM_X_EPTP_INDEX,			 NO_EXIST,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_EPTP_INDEX"	  },
	{ VM_X_CONTROL_VECTOR_PIN_EVENTS,	 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_CONTROL_VECTOR_PIN_EVENTS" },
	{ VM_X_CONTROL_VECTOR_PROCESSOR_EVENTS,	 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS" },
	{ VM_X_CONTROL2_VECTOR_PROCESSOR_EVENTS, WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_CONTROL2_VECTOR_PROCESSOR_EVENTS" },
	{ VM_X_EXCEPTION_BITMAP,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXCEPTION_BITMAP" },
	{ VM_X_CR3_TARGET_COUNT,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_CR3_TARGET_COUNT" },
	{ VM_X_CR0_MASK,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_CR0_MASK"	  },
	{ VM_X_CR4_MASK,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_CR4_MASK"	  },
	{ VM_X_CR0_READ_SHADOW,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_CR0_READ_SHADOW" },
	{ VM_X_CR4_READ_SHADOW,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_CR4_READ_SHADOW" },
	{ VM_X_PAGE_FAULT_ERROR_CODE_MASK,	 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_PAGE_FAULT_ERROR_CODE_MASK" },
	{ VM_X_PAGE_FAULT_ERROR_CODE_MATCH,	 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_PAGE_FAULT_ERROR_CODE_MATCH" },
	{ VM_EXIT_CONTROL_VECTOR,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_CONTROL_VECTOR" },
	{ VM_EXIT_MSR_STORE_COUNT,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_MSR_STORE_COUNT" },
	{ VM_EXIT_MSR_LOAD_COUNT,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_MSR_LOAD_COUNT" },
	{ VM_ENTER_CONTROL_VECTOR,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_ENTER_CONTROL_VECTOR" },
	{ VM_ENTER_INTERRUPT_INFO,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_ENTER_INTERRUPT_INFO" },
	{ VM_ENTER_EXCEPTION_ERROR_CODE,	 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_ENTER_EXCEPTION_ERROR_CODE" },
	{ VM_ENTER_INSTRUCTION_LENGTH,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_ENTER_INSTRUCTION_LENGTH" },
	{ VM_ENTER_MSR_LOAD_COUNT,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_ENTER_MSR_LOAD_COUNT" },
	{ VM_X_IO_BITMAP_ADDRESS_A,		 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_IO_BITMAP_ADDRESS_A" },
	{ VM_X_IO_BITMAP_ADDRESS_B,		 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_IO_BITMAP_ADDRESS_B" },
	{ VM_X_MSR_BITMAP_ADDRESS,		 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_MSR_BITMAP_ADDRESS" },
	{ VM_EXIT_MSR_STORE_ADDRESS,		 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_EXIT_MSR_STORE_ADDRESS" },
	{ VM_EXIT_MSR_LOAD_ADDRESS,		 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_EXIT_MSR_LOAD_ADDRESS" },
	{ VM_ENTER_MSR_LOAD_ADDRESS,		 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_ENTER_MSR_LOAD_ADDRESS" },
	{ VM_X_OSV_CONTROLLING_VMCS_ADDRESS,	 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_OSV_CONTROLLING_VMCS_ADDRESS" },
	{ VM_X_TSC_OFFSET,			 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_TSC_OFFSET"	  },
	{ VM_EXIT_PHYSICAL_ADDRESS,		 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_EXIT_INFO_GUEST_PHYSICAL_ADDRESS" },
	{ VM_EXIT_INFO_INSTRUCTION_ERROR_CODE,	 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_INSTRUCTION_ERROR_CODE" },
	{ VM_EXIT_INFO_REASON,			 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_REASON" },
	{ VM_EXIT_INFO_EXCEPTION_INFO,		 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_EXCEPTION_INFO" },
	{ VM_EXIT_INFO_EXCEPTION_ERROR_CODE,	 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_EXCEPTION_ERROR_CODE" },
	{ VM_EXIT_INFO_IDT_VECTORING,		 WRITABLE_IN_CACHE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_IDT_VECTORING" },
	{ VM_EXIT_INFO_IDT_VECTORING_ERROR_CODE, READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_IDT_VECTORING_ERROR_CODE" },
	{ VM_EXIT_INFO_INSTRUCTION_LENGTH,	 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_INSTRUCTION_LENGTH" },
	{ VM_EXIT_INFO_INSTRUCTION_INFO,	 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_INSTRUCTION_INFO" },
	{ VM_EXIT_INFO_QUALIFICATION,		 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_QUALIFICATION" },
	{ VM_EXIT_INFO_IO_RCX,			 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_IO_RCX" },
	{ VM_EXIT_INFO_IO_RSI,			 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_IO_RSI" },
	{ VM_EXIT_INFO_IO_RDI,			 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_IO_RDI" },
	{ VM_EXIT_INFO_IO_RIP,			 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_IO_RIP" },
	{ VM_EXIT_INFO_GUEST_LINEAR_ADDRESS,	 READONLY,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_INFO_GUEST_LINEAR_ADDRESS" },
	{ VM_X_VIRTUAL_APIC_ADDRESS,		 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_VIRTUAL_APIC_ADDRESS" },
	{ VM_X_APIC_ACCESS_ADDRESS,		 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_APIC_ACCESS_ADDRESS" },
	{ VM_EXIT_TPR_THRESHOLD,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_EXIT_TPR_THRESHOLD" },
	{ VM_X_EPTP_ADDRESS,			 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_EPTP_ADDRESS"	  },
	{ VM_X_PREEMTION_TIMER,			 NO_EXIST,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_PREEMPTION_TIMER" },
	{ GUEST_CR0,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_CR0"	  },
	{ GUEST_CR3,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_CR3"	  },
	{ GUEST_CR4,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_CR4"	  },
	{ GUEST_DR7,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_DR7"	  },
	{ GUEST_ES_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_ES_SELECTOR" },
	{ GUEST_ES_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_ES_BASE"	  },
	{ GUEST_ES_LIMIT,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_ES_LIMIT"	  },
	{ GUEST_ES_AR,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_ES_AR"	  },
	{ GUEST_CS_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_CS_SELECTOR" },
	{ GUEST_CS_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_CS_BASE"	  },
	{ GUEST_CS_LIMIT,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_CS_LIMIT"	  },
	{ GUEST_CS_AR,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_CS_AR"	  },
	{ GUEST_SS_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_SS_SELECTOR" },
	{ GUEST_SS_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_SS_BASE"	  },
	{ GUEST_SS_LIMIT,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_SS_LIMIT"	  },
	{ GUEST_SS_AR,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_SS_AR"	  },
	{ GUEST_DS_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_DS_SELECTOR" },
	{ GUEST_DS_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_DS_BASE"	  },
	{ GUEST_DS_LIMIT,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_DS_LIMIT"	  },
	{ GUEST_DS_AR,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_DS_AR"	  },
	{ GUEST_FS_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_FS_SELECTOR" },
	{ GUEST_FS_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_FS_BASE"	  },
	{ GUEST_FS_LIMIT,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_FS_LIMIT"	  },
	{ GUEST_FS_AR,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_FS_AR"	  },
	{ GUEST_GS_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_GS_SELECTOR" },
	{ GUEST_GS_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_GS_BASE"	  },
	{ GUEST_GS_LIMIT,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_GS_LIMIT"	  },
	{ GUEST_GS_AR,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_GS_AR"	  },
	{ GUEST_LDTR_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_GUEST_LDTR_SELECTOR" },
	{ GUEST_LDTR_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_LDTR_BASE"	  },
	{ GUEST_LDTR_LIMIT,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_LDTR_LIMIT"  },
	{ GUEST_LDTR_AR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_LDTR_AR"	  },
	{ GUEST_TR_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_TR_SELECTOR" },
	{ GUEST_TR_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_TR_BASE"	  },
	{ GUEST_TR_LIMIT,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_TR_LIMIT"	  },
	{ GUEST_TR_AR,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_TR_AR"	  },
	{ GUEST_GDTR_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_GDTR_BASE"	  },
	{ GUEST_GDTR_LIMIT,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_GDTR_LIMIT"  },
	{ GUEST_IDTR_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_IDTR_BASE"	  },
	{ GUEST_IDTR_LIMIT,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_IDTR_LIMIT"  },
	{ GUEST_ESP,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_RSP"	  },
	{ GUEST_EIP,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_RIP"	  },
	{ GUEST_EFLAGS,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_RFLAGS"	  },
	{ GUEST_PEND_DBE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_PEND_DBE"	  },
	{ GUEST_WORKING_VMCS_PTR,		 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_GUEST_WORKING_VMCS_PTR" },
	{ GUEST_DEBUG_CONTROL,			 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_GUEST_DEBUG_CONTROL" },
	{ GUEST_INTERRUPTIBILITY,		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_GUEST_INTERRUPTIBILITY" },
	{ GUEST_SLEEP_STATE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_SLEEP_STATE" },
	{ GUEST_SMBASE,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_SMBASE"	  },
	{ GUEST_SYSENTER_CS,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_GUEST_SYSENTER_CS" },
	{ GUEST_SYSENTER_ESP,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_GUEST_SYSENTER_ESP" },
	{ GUEST_SYSENTER_EIP,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VMCS_GUEST_SYSENTER_EIP" },
	{ GUEST_PAT,				 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_GUEST_PAT"	  },
	{ GUEST_EFER,				 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_GUEST_EFER"	  },
	{ GUEST_IA32_PERF_GLOBAL_CTRL,		 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },
	  "GUEST_IA32_PERF_GLOBAL_CTRL" },
	{ GUEST_PDPTR0,				 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_GUEST_PDPTR0"	  },
	{ GUEST_PDPTR1,				 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_GUEST_PDPTR1"	  },
	{ GUEST_PDPTR2,				 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_GUEST_PDPTR2"	  },
	{ GUEST_PDPTR3,				 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_GUEST_PDPTR3"	  },
	{ HOST_CR0,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_CR0"	  },
	{ HOST_CR3,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_CR3"	  },
	{ HOST_CR4,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_CR4"	  },
	{ HOST_ES_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_ES_SELECTOR"  },
	{ HOST_CS_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_CS_SELECTOR"  },
	{ HOST_SS_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_SS_SELECTOR"  },
	{ HOST_DS_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_DS_SELECTOR"  },
	{ HOST_FS_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_FS_SELECTOR"  },
	{ HOST_FS_BASE,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_FS_BASE"	  },
	{ HOST_GS_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_GS_SELECTOR"  },
	{ HOST_GS_BASE,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_GS_BASE"	  },
	{ HOST_TR_SELECTOR,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_TR_SELECTOR"  },
	{ HOST_TR_BASE,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_TR_BASE"	  },
	{ HOST_GDTR_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_GDTR_BASE"	  },
	{ HOST_IDTR_BASE,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_IDTR_BASE"	  },
	{ HOST_ESP,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_RSP"	  },
	{ HOST_EIP,				 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_RIP"	  },
	{ HOST_SYSENTER_CS,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_SYSENTER_CS"  },
	{ HOST_SYSENTER_ESP,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_SYSENTER_ESP" },
	{ HOST_SYSENTER_EIP,			 WRITABLE,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_HOST_SYSENTER_EIP" },
	{ HOST_PAT,				 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_HOST_PAT"	  },
	{ HOST_EFER,				 WRITABLE,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_HOST_EFER"	  },
	{ HOST_IA32_PERF_GLOBAL_CTRL,		 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },
	  "HOST_IA32_PERF_GLOBAL_CTRL" },
	{ VM_X_CR3_TARGET_VALUE(0),		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VM_X_CR3_TARGET_VALUE_0" },
	{ VM_X_CR3_TARGET_VALUE(1),		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VM_X_CR3_TARGET_VALUE_1" },
	{ VM_X_CR3_TARGET_VALUE(2),		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VM_X_CR3_TARGET_VALUE_2" },
	{ VM_X_CR3_TARGET_VALUE(3),		 WRITABLE,
	  FULL_ENC_ONLY, { 0 },
	  "VM_X_CR3_TARGET_VALUE_3" },

	{ VM_X_VMFUNC_CONTROL,			 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },					 "VMCS_VMFUNC_CONTROL"	  },
	{ VM_X_VMFUNC_EPTP_LIST_ADDRESS,	 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_VMFUNC_EPTP_LIST_ADDRESS" },

	{ VM_X_VE_INFO_ADDRESS,			 NO_EXIST,
	  SUPP_HIGH_ENC, { 0 },
	  "VMCS_VE_INFO_ADDRESS" },
	{ VMCS_NO_COMPONENT,			 NO_EXIST,
	  FULL_ENC_ONLY, { 0 },					 "VMCS_FIELD_COUNT"	  }
};

static vmcs_field_t g_guest_state_fields[] = {
	VMCS_GUEST_CR0,
	VMCS_GUEST_CR3,
	VMCS_GUEST_CR4,
	VMCS_GUEST_DR7,
	VMCS_GUEST_ES_SELECTOR,
	VMCS_GUEST_ES_BASE,
	VMCS_GUEST_ES_LIMIT,
	VMCS_GUEST_ES_AR,
	VMCS_GUEST_CS_SELECTOR,
	VMCS_GUEST_CS_BASE,
	VMCS_GUEST_CS_LIMIT,
	VMCS_GUEST_CS_AR,
	VMCS_GUEST_SS_SELECTOR,
	VMCS_GUEST_SS_BASE,
	VMCS_GUEST_SS_LIMIT,
	VMCS_GUEST_SS_AR,
	VMCS_GUEST_DS_SELECTOR,
	VMCS_GUEST_DS_BASE,
	VMCS_GUEST_DS_LIMIT,
	VMCS_GUEST_DS_AR,
	VMCS_GUEST_FS_SELECTOR,
	VMCS_GUEST_FS_BASE,
	VMCS_GUEST_FS_LIMIT,
	VMCS_GUEST_FS_AR,
	VMCS_GUEST_GS_SELECTOR,
	VMCS_GUEST_GS_BASE,
	VMCS_GUEST_GS_LIMIT,
	VMCS_GUEST_GS_AR,
	VMCS_GUEST_LDTR_SELECTOR,
	VMCS_GUEST_LDTR_BASE,
	VMCS_GUEST_LDTR_LIMIT,
	VMCS_GUEST_LDTR_AR,
	VMCS_GUEST_TR_SELECTOR,
	VMCS_GUEST_TR_BASE,
	VMCS_GUEST_TR_LIMIT,
	VMCS_GUEST_TR_AR,
	VMCS_GUEST_GDTR_BASE,
	VMCS_GUEST_GDTR_LIMIT,
	VMCS_GUEST_IDTR_BASE,
	VMCS_GUEST_IDTR_LIMIT,
	VMCS_GUEST_RSP,
	VMCS_GUEST_RIP,
	VMCS_GUEST_RFLAGS,
	VMCS_GUEST_PEND_DBE,
	VMCS_GUEST_WORKING_VMCS_PTR,
	VMCS_GUEST_DEBUG_CONTROL,
	VMCS_GUEST_INTERRUPTIBILITY,
	VMCS_GUEST_SLEEP_STATE,
	VMCS_GUEST_SMBASE,
	VMCS_GUEST_SYSENTER_CS,
	VMCS_GUEST_SYSENTER_ESP,
	VMCS_GUEST_SYSENTER_EIP,
	VMCS_GUEST_PAT,
	VMCS_GUEST_EFER,
	VMCS_GUEST_PDPTR0,
	VMCS_GUEST_PDPTR1,
	VMCS_GUEST_PDPTR2,
	VMCS_GUEST_PDPTR3,
	VMCS_PREEMPTION_TIMER
};

static vmcs_field_t g_host_state_fields[] = {
	VMCS_HOST_CR0,
	VMCS_HOST_CR3,
	VMCS_HOST_CR4,
	VMCS_HOST_ES_SELECTOR,
	VMCS_HOST_CS_SELECTOR,
	VMCS_HOST_SS_SELECTOR,
	VMCS_HOST_DS_SELECTOR,
	VMCS_HOST_FS_SELECTOR,
	VMCS_HOST_FS_BASE,
	VMCS_HOST_GS_SELECTOR,
	VMCS_HOST_GS_BASE,
	VMCS_HOST_TR_SELECTOR,
	VMCS_HOST_TR_BASE,
	VMCS_HOST_GDTR_BASE,
	VMCS_HOST_IDTR_BASE,
	VMCS_HOST_RSP,
	VMCS_HOST_RIP,
	VMCS_HOST_SYSENTER_CS,
	VMCS_HOST_SYSENTER_ESP,
	VMCS_HOST_SYSENTER_EIP,
	VMCS_HOST_PAT,
	VMCS_HOST_EFER
};

static vmcs_field_t g_control_fields[] = {
	VMCS_VPID,
	VMCS_CONTROL_VECTOR_PIN_EVENTS,
	VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS,
	VMCS_CONTROL2_VECTOR_PROCESSOR_EVENTS,
	VMCS_EXCEPTION_BITMAP,
	VMCS_CR3_TARGET_COUNT,
	VMCS_CR0_MASK,
	VMCS_CR4_MASK,
	VMCS_CR0_READ_SHADOW,
	VMCS_CR4_READ_SHADOW,
	VMCS_PAGE_FAULT_ERROR_CODE_MASK,
	VMCS_PAGE_FAULT_ERROR_CODE_MATCH,
	VMCS_EXIT_CONTROL_VECTOR,
	VMCS_EXIT_MSR_STORE_COUNT,
	VMCS_EXIT_MSR_LOAD_COUNT,
	VMCS_ENTER_CONTROL_VECTOR,
	VMCS_ENTER_INTERRUPT_INFO,
	VMCS_ENTER_EXCEPTION_ERROR_CODE,
	VMCS_ENTER_INSTRUCTION_LENGTH,
	VMCS_ENTER_MSR_LOAD_COUNT,
	VMCS_IO_BITMAP_ADDRESS_A,
	VMCS_IO_BITMAP_ADDRESS_B,
	VMCS_MSR_BITMAP_ADDRESS,
	VMCS_EXIT_MSR_STORE_ADDRESS,
	VMCS_EXIT_MSR_LOAD_ADDRESS,
	VMCS_ENTER_MSR_LOAD_ADDRESS,
	VMCS_OSV_CONTROLLING_VMCS_ADDRESS,
	VMCS_TSC_OFFSET,
	VMCS_EXIT_INFO_GUEST_PHYSICAL_ADDRESS,
	VMCS_EXIT_INFO_GUEST_LINEAR_ADDRESS,
	VMCS_EXIT_INFO_INSTRUCTION_ERROR_CODE,
	VMCS_EXIT_INFO_REASON,
	VMCS_EXIT_INFO_EXCEPTION_INFO,
	VMCS_EXIT_INFO_EXCEPTION_ERROR_CODE,
	VMCS_EXIT_INFO_IDT_VECTORING,
	VMCS_EXIT_INFO_IDT_VECTORING_ERROR_CODE,
	VMCS_EXIT_INFO_INSTRUCTION_LENGTH,
	VMCS_EXIT_INFO_INSTRUCTION_INFO,
	VMCS_EXIT_INFO_QUALIFICATION,
	VMCS_EXIT_INFO_IO_RCX,
	VMCS_EXIT_INFO_IO_RSI,
	VMCS_EXIT_INFO_IO_RDI,
	VMCS_EXIT_INFO_IO_RIP,
	VMCS_VIRTUAL_APIC_ADDRESS,
	VMCS_APIC_ACCESS_ADDRESS,
	VMCS_EXIT_TPR_THRESHOLD,
	VMCS_EPTP_ADDRESS,
	VMCS_CR3_TARGET_VALUE_0,
	VMCS_CR3_TARGET_VALUE_1,
	VMCS_CR3_TARGET_VALUE_2,
	VMCS_CR3_TARGET_VALUE_3,
	VMCS_VMFUNC_CONTROL,
	VMCS_VMFUNC_EPTP_LIST_ADDRESS
};

/* translation encoding -> field enum */

#define NUMBER_OF_ENCODING_TYPES    16
#define MAX_ENCODINGS_OF_SAME_TYPE  32

/* this array is parallel to array of encoding tables
 *    encoding type
 * 1  0x0000 _ 0000_00xx_xxxx_xxx0
 * 2  0x0800 _ 0000_10xx_xxxx_xxx0
 * 3  0x0C00 _ 0000_11xx_xxxx_xxx0
 * 4  0x2000 _ 0010_00xx_xxxx_xxx0
 * 5  0x2400 _ 0010_01xx_xxxx_xxx0
 * 6  0x2800 _ 0010_10xx_xxxx_xxx0
 * 7  0x2C00 _ 0010_11xx_xxxx_xxx0
 * 8  0x4000 _ 0100_00xx_xxxx_xxx0
 * 9  0x4400 _ 0100_01xx_xxxx_xxx0
 * 10 0x4800 _ 0100_10xx_xxxx_xxx0
 * 11 0x4C00 _ 0100_11xx_xxxx_xxx0
 * 12 0x6000 _ 0110_00xx_xxxx_xxx0
 * 13 0x6400 _ 0110_01xx_xxxx_xxx0
 * 14 0x6800 _ 0110_10xx_xxxx_xxx0
 * 15 0x6C00 _ 0110_11xx_xxxx_xxx0
 * In the above table we see that encoding looks like the following
 * 0mm0_nnxx_xxxx_xxxA
 * Where actual encoding type is mmnn and A is a FULL/HIGH selector
 * assuming that bits signed as 0 must be 0 */
#define ENC_MUST_BE_ZERO_BITS 0x9000
#define ENC_M_BITS            0x6000
#define ENC_M_BITS_SHIFT      11
#define ENC_N_BITS            0x0C00
#define ENC_N_BITS_SHIFT      10
#define ENC_X_BITS            0x03FE
#define ENC_BITS_SHIFT        1
#define ENC_HIGH_TYPE_BIT     0x1
#define ENC_TYPE_FROM_ENCODING(enc)                          \
	((((enc) & ENC_M_BITS) >> ENC_M_BITS_SHIFT) |               \
	 (((enc) & ENC_N_BITS) >> ENC_N_BITS_SHIFT))
#define ENTRY_IDX_FROM_ENCODING(enc) (((enc) & ENC_X_BITS) >> ENC_BITS_SHIFT)
#define IS_ENCODING_HIGH_TYPE(enc)       \
	(((enc) & ENC_HIGH_TYPE_BIT) == ENC_HIGH_TYPE_BIT)

typedef struct {
	uint32_t	valid;
	vmcs_field_t	field_id;
} enc_2_field_entry_t;

static enc_2_field_entry_t
	g_enc_2_field[NUMBER_OF_ENCODING_TYPES][MAX_ENCODINGS_OF_SAME_TYPE];

MON_DEBUG_CODE(
	static void vmcs_print_group(const vmcs_object_t *obj,
				     const vmcs_field_t *fields_to_print,
				     uint32_t count);
	)
static void enable_vmcs_2_0_fields(const vmcs_hw_constraints_t *constraints);
static void init_enc_2_field_tables(void);
static boolean_t string_is_substring(const char *bigstring,
				     const char *smallstring);
static boolean_t strings_are_substrings(const char *bigstring,
					uint32_t num_of_smallstrings,
					char *smallstring[]);

/*------------------------------ Code ----------------------------------------*/

boolean_t vmcs_field_is_supported(vmcs_field_t field_id)
{
	return field_id < VMCS_FIELD_COUNT &&
	       field_id < (NELEMENTS(g_field_data) - 1) &&
	       g_field_data[field_id].access != NO_EXIST;
}

void vmcs_write_nocheck(vmcs_object_t *vmcs,
			vmcs_field_t field_id, uint64_t value)
{
	MON_ASSERT(vmcs);
	MON_ASSERT(field_id < VMCS_FIELD_COUNT);
	if (field_id < VMCS_FIELD_COUNT) {
		vmcs->vmcs_write(vmcs, field_id, value);
	}
}

void mon_vmcs_write(vmcs_object_t *vmcs, vmcs_field_t field_id, uint64_t value)
{
	MON_ASSERT(field_id < VMCS_FIELD_COUNT);
	MON_ASSERT(FIELD_IS_WRITEABLE(field_id));
	if (field_id < VMCS_FIELD_COUNT &&
	    (vmcs->skip_access_checking || FIELD_IS_WRITEABLE(field_id))) {
		vmcs->vmcs_write(vmcs, field_id, value);
	}
}

uint64_t mon_vmcs_read(const vmcs_object_t *vmcs, vmcs_field_t field_id)
{
	uint64_t value;

	MON_ASSERT(vmcs);
	MON_ASSERT(field_id < VMCS_FIELD_COUNT);
	MON_ASSERT(FIELD_IS_READABLE(field_id));
	if (field_id < VMCS_FIELD_COUNT &&
	    (vmcs->skip_access_checking || FIELD_IS_READABLE(field_id))) {
		value = vmcs->vmcs_read(vmcs, field_id);
	} else {
		value = UINT64_ALL_ONES;
	}

	return value;
}

void vmcs_update(vmcs_object_t *vmcs, vmcs_field_t field_id,
		 uint64_t value, uint64_t bits_to_update)
{
	uint64_t result_value;

	MON_ASSERT(field_id < VMCS_FIELD_COUNT);

	result_value = mon_vmcs_read(vmcs, field_id);

	/* clear all bits except bits_to_update */
	value &= bits_to_update;
	/* clear bits_to_update */
	result_value &= ~bits_to_update;
	result_value |= value;
	mon_vmcs_write(vmcs, field_id, result_value);
}

uint32_t vmcs_get_field_encoding(vmcs_field_t field_id, rw_access_t *p_access)
{
	if (field_id < VMCS_FIELD_COUNT) {
		if (NULL != p_access) {
			*p_access = (rw_access_t)g_field_data[field_id].access;
		}

		return g_field_data[field_id].encoding;
	}

	return VMCS_NO_COMPONENT;
}

/*
 * Init the package
 */
void vmcs_manager_init(void)
{
	const vmcs_hw_constraints_t *constraints;

	constraints = mon_vmcs_hw_get_vmx_constraints();
	enable_vmcs_2_0_fields(constraints);
	init_enc_2_field_tables();
}

/*
 * Enable VMCS 2.0 fields if exist
 */
void enable_vmcs_2_0_fields(const vmcs_hw_constraints_t *constraints)
{
	if (constraints->ept_supported) {
		g_field_data[VMCS_EPTP_ADDRESS].access = WRITABLE;
		g_field_data[VMCS_EXIT_INFO_GUEST_PHYSICAL_ADDRESS].access =
			READONLY;
		g_field_data[VMCS_GUEST_PDPTR0].access = WRITABLE;
		g_field_data[VMCS_GUEST_PDPTR1].access = WRITABLE;
		g_field_data[VMCS_GUEST_PDPTR2].access = WRITABLE;
		g_field_data[VMCS_GUEST_PDPTR3].access = WRITABLE;
	}

	if (constraints->vpid_supported) {
		g_field_data[VMCS_VPID].access = WRITABLE;
	}

	if ((1 == constraints->may1_vm_entry_ctrl.bits.load_efer) ||
	    (1 == constraints->may1_vm_exit_ctrl.bits.save_efer)) {
		g_field_data[VMCS_GUEST_EFER].access = WRITABLE;
	}

	if ((1 == constraints->may1_vm_entry_ctrl.bits.load_pat) ||
	    (1 == constraints->may1_vm_exit_ctrl.bits.save_pat)) {
		g_field_data[VMCS_GUEST_PAT].access = WRITABLE;
	}

	if (1 == constraints->may1_pin_based_exec_ctrl.bits.vmx_timer) {
		g_field_data[VMCS_PREEMPTION_TIMER].access = WRITABLE;
	}

	if (1 == constraints->may1_vm_exit_ctrl.bits.load_efer) {
		g_field_data[VMCS_HOST_EFER].access = WRITABLE;
	}

	if (1 == constraints->may1_vm_exit_ctrl.bits.load_pat) {
		g_field_data[VMCS_HOST_PAT].access = WRITABLE;
	}

	if (1 ==
	    constraints->may1_vm_exit_ctrl.bits.load_ia32_perf_global_ctrl) {
		g_field_data[VMCS_GUEST_IA32_PERF_GLOBAL_CTRL].access =
			WRITABLE;
	}

	if (1 ==
	    constraints->may1_vm_entry_ctrl.bits.load_ia32_perf_global_ctrl) {
		g_field_data[VMCS_HOST_IA32_PERF_GLOBAL_CTRL].access = WRITABLE;
	}

	if (constraints->vmfunc_supported) {
		g_field_data[VMCS_VMFUNC_CONTROL].access = WRITABLE;
		g_field_data[VMCS_VMFUNC_EPTP_LIST_ADDRESS].access = WRITABLE;
	}

	if (constraints->ve_supported) {
		g_field_data[VMCS_EPTP_INDEX].access = WRITABLE;
		g_field_data[VMCS_VE_INFO_ADDRESS].access = WRITABLE;
	}
}

static
boolean_t get_enc_2_field_entry(uint32_t encoding, enc_2_field_entry_t **entry)
{
	uint32_t enc_type = ENC_TYPE_FROM_ENCODING(encoding);
	uint32_t entry_idx = ENTRY_IDX_FROM_ENCODING(encoding);

	MON_ASSERT(NULL != entry);

	if ((encoding & ENC_MUST_BE_ZERO_BITS) != 0) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ERROR: VMCS Encoding %P contains bits that assumed to be 0\n",
			encoding);
		return FALSE;
	}

	if (enc_type >= NUMBER_OF_ENCODING_TYPES) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ERROR: VMCS Encoding %P means that need more encoding types %P>=%P\n",
			encoding,
			enc_type,
			NUMBER_OF_ENCODING_TYPES);
		return FALSE;
	}

	if (entry_idx >= MAX_ENCODINGS_OF_SAME_TYPE) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ERROR: VMCS Encoding %P means that need more entries per type %P>=%P\n",
			encoding,
			entry_idx,
			MAX_ENCODINGS_OF_SAME_TYPE);
		return FALSE;
	}

	*entry = &(g_enc_2_field[enc_type][entry_idx]);

	return TRUE;
}

static
void init_enc_2_field_tables(void)
{
	vmcs_field_t cur_field;
	vmcs_encoding_t *enc;
	enc_2_field_entry_t *enc_2_field_entry;
	boolean_t ok;

	mon_memset(g_enc_2_field, 0, sizeof(g_enc_2_field));

	/* loop though all supported fields and fill the enc->field table */
	for (cur_field = (vmcs_field_t)0;
	     cur_field < VMCS_FIELD_COUNT;
	     ++cur_field) {
		enc = g_field_data + (uint32_t)cur_field;

		if (NO_EXIST == enc->access) {
			continue;
		}

		ok = get_enc_2_field_entry(enc->encoding, &enc_2_field_entry);
		MON_ASSERT(ok);
		MON_ASSERT(FALSE == enc_2_field_entry->valid);

		enc_2_field_entry->valid = TRUE;
		enc_2_field_entry->field_id = cur_field;
	}
}

vmcs_field_t vmcs_get_field_id_by_encoding(uint32_t encoding,
					   OPTIONAL boolean_t *is_HIGH_part)
{
	enc_2_field_entry_t *enc_2_field_entry;
	boolean_t found;
	vmcs_field_t field;

	found = get_enc_2_field_entry(encoding, &enc_2_field_entry);

	if ((!found) || (!enc_2_field_entry->valid)) {
		/* MON_LOG(mask_anonymous, level_trace,"ERROR: VMCS Encoding %P is
		 * unknown\n", encoding); */
		return VMCS_FIELD_COUNT;
	}

	field = enc_2_field_entry->field_id;

	if (IS_ENCODING_HIGH_TYPE(encoding)) {
		if (SUPP_HIGH_ENC !=
		    g_field_data[field].supports_high_encoding) {
			MON_LOG(mask_anonymous,
				level_trace,
				"ERROR: VMCS Encoding %P does not map to a known HIGH type encoding\n",
				encoding);
			return VMCS_FIELD_COUNT;
		}
	}

	if (is_HIGH_part) {
		*is_HIGH_part = IS_ENCODING_HIGH_TYPE(encoding);
	}

	return field;
}

#ifdef CLI_INCLUDE
void vmcs_print_group(const vmcs_object_t *obj,
		      const vmcs_field_t *fields_to_print, uint32_t count)
{
	uint32_t i;
	uint64_t value;
	const vmcs_encoding_t *field_desc;

	for (i = 0; i < count; ++i) {
		field_desc = &g_field_data[fields_to_print[i]];
		if (field_desc->access == NO_EXIST) {
			continue;
		}

		value = mon_vmcs_read(obj, fields_to_print[i]);
		CLI_PRINT("%40s (0x%04X) = %P\n", field_desc->name,
			field_desc->encoding, value);
	}
}

boolean_t string_is_substring(const char *bigstring, const char *smallstring)
{
	boolean_t match = TRUE;
	int ib, is;

	for (ib = 0, is = 0; bigstring[ib] != 0 && smallstring[is] != 0; ++ib) {
		if (bigstring[ib] == smallstring[is]) {
			is++;
			match = TRUE;
		} else {
			is = 0;
			match = FALSE;
		}
	}
	if (smallstring[is] != 0) {
		match = FALSE;
	}
	return match;
}

/* Returns TRUE if all smallstrings are part of a bigstring */
boolean_t strings_are_substrings(const char *bigstring,
				 uint32_t num_of_smallstrings,
				 char *smallstring[])
{
	uint32_t i;

	for (i = 0; i < num_of_smallstrings; ++i) {
		if (FALSE == string_is_substring(bigstring, smallstring[i])) {
			return FALSE;
		}
	}
	return TRUE;
}

void vmcs_print_all_filtered(const vmcs_object_t *obj,
			     uint32_t num_of_filters, char *filters[])
{
	uint32_t i;
	uint64_t value;
	const vmcs_encoding_t *field_desc;

	for (i = 0; i < NELEMENTS(g_field_data); ++i) {
		field_desc = &g_field_data[i];
		if (field_desc->access == NO_EXIST) {
			continue;
		}

		if (strings_are_substrings(field_desc->name, num_of_filters,
			    filters)) {
			value = mon_vmcs_read(obj, (vmcs_field_t)i);
			CLI_PRINT("%40s (0x%04X) = %P\n", field_desc->name,
				field_desc->encoding, value);
		}
	}
}

void vmcs_print_guest_state(const vmcs_object_t *obj)
{
	CLI_PRINT("------------- VMCS Guest State --------------\n");
	vmcs_print_group(obj, g_guest_state_fields,
		NELEMENTS(g_guest_state_fields));
	CLI_PRINT("------------- END of VMCS Guest State -------\n\n");
}

void vmcs_print_host_state(const vmcs_object_t *obj)
{
	CLI_PRINT("------------- VMCS Host State --------------\n");
	vmcs_print_group(obj, g_host_state_fields, NELEMENTS(
			g_host_state_fields));
	CLI_PRINT("------------- END of VMCS Host State -------\n\n");
}

void vmcs_print_controls(const vmcs_object_t *obj)
{
	CLI_PRINT("------------- VMCS Controls --------------\n");
	vmcs_print_group(obj, g_control_fields, NELEMENTS(g_control_fields));
	CLI_PRINT("------------- END of VMCS Controls -------\n\n");
}

void vmcs_print_all(const vmcs_object_t *obj)
{
	vmcs_print_controls(obj);
	vmcs_print_guest_state(obj);
	vmcs_print_host_state(obj);
}
#endif
const char *vmcs_get_field_name(vmcs_field_t field_id)
{
	const char *name;
	const vmcs_encoding_t *enc =
		(field_id < VMCS_FIELD_COUNT) ? &g_field_data[field_id] : NULL;

	if (enc) {
		name = enc->name;
	} else {
		name = "UNKNOWN VMCS FIELD";
	}

	return name;
}

void vmcs_clear_all_msr_lists(vmcs_object_t *vmcs)
{
	mon_vmcs_write(vmcs, VMCS_EXIT_MSR_STORE_COUNT, 0);
	mon_vmcs_write(vmcs, VMCS_EXIT_MSR_LOAD_COUNT, 0);
	mon_vmcs_write(vmcs, VMCS_ENTER_MSR_LOAD_COUNT, 0);
}

void vmcs_init_all_msr_lists(vmcs_object_t *vmcs)
{
	mon_vmcs_write(vmcs, VMCS_EXIT_MSR_STORE_ADDRESS, VMCS_INVALID_ADDRESS);
	mon_vmcs_write(vmcs, VMCS_EXIT_MSR_LOAD_ADDRESS, VMCS_INVALID_ADDRESS);
	mon_vmcs_write(vmcs, VMCS_ENTER_MSR_LOAD_ADDRESS, VMCS_INVALID_ADDRESS);
	vmcs_clear_all_msr_lists(vmcs);
	vmcs->max_num_of_vmexit_store_msrs = 0;
	vmcs->max_num_of_vmexit_load_msrs = 0;
	vmcs->max_num_of_vmenter_load_msrs = 0;
}

static
void vmcs_free_msr_list(uint64_t msr_list_addr, boolean_t address_is_in_hpa)
{
	if (msr_list_addr != VMCS_INVALID_ADDRESS) {
		hva_t msr_list_addr_hva;

		if (address_is_in_hpa) {
			if (!mon_hmm_hpa_to_hva(msr_list_addr,
				    &msr_list_addr_hva)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Could not retrieve hva_t of MSR list\n",
					__FUNCTION__);
				MON_DEADLOOP();
			}
		} else {
			msr_list_addr_hva = msr_list_addr;
		}

		mon_mfree((void *)msr_list_addr_hva);
	}
}

void vmcs_destroy_all_msr_lists_internal(vmcs_object_t *vmcs,
					 boolean_t addresses_are_in_hpa)
{
	uint64_t exit_store_addr;
	uint64_t exit_load_addr;
	uint64_t enter_load_addr;

	exit_store_addr = mon_vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_ADDRESS);
	exit_load_addr = mon_vmcs_read(vmcs, VMCS_EXIT_MSR_LOAD_ADDRESS);
	enter_load_addr = mon_vmcs_read(vmcs, VMCS_ENTER_MSR_LOAD_ADDRESS);

	vmcs_free_msr_list(exit_store_addr, addresses_are_in_hpa);
	vmcs_free_msr_list(exit_load_addr, addresses_are_in_hpa);
	vmcs_free_msr_list(enter_load_addr, addresses_are_in_hpa);

	vmcs_init_all_msr_lists(vmcs);
}

static
void vmcs_alloc_msr_list(IN uint32_t requested_num_of_msrs,
			 OUT void **msl_list_memory,
			 OUT uint32_t *allocated_num_of_msrs)
{
	uint32_t num_of_msrs;

	/* Fund closes power of 2 */
	num_of_msrs =
		(requested_num_of_msrs >
		 8) ? requested_num_of_msrs : MIN_SIZE_OF_MSR_LIST;
	for (; !IS_POW_OF_2(num_of_msrs); num_of_msrs++)
		;

	*msl_list_memory =
		mon_malloc_aligned(num_of_msrs * sizeof(ia32_vmx_msr_entry_t),
			sizeof(ia32_vmx_msr_entry_t));
	if (*msl_list_memory == NULL) {
		MON_LOG(mask_anonymous,
			level_trace,
			"%s: Failed to allocate memory for MSR list\n",
			__FUNCTION__);
		MON_DEADLOOP();
		*allocated_num_of_msrs = 0;
	} else {
		*allocated_num_of_msrs = num_of_msrs;
	}
}

static
void vmcs_copy_msr_list(ia32_vmx_msr_entry_t *copy_to,
			ia32_vmx_msr_entry_t *copy_from, uint32_t num_of_msrs)
{
	for (; num_of_msrs > 0; num_of_msrs--)
		*copy_to++ = *copy_from++;
}

void vmcs_add_msr_to_list(vmcs_object_t *vmcs, uint32_t msr_index,
			  uint64_t value, vmcs_field_t list_address,
			  vmcs_field_t list_count, uint32_t *max_msrs_counter,
			  boolean_t is_addres_hpa)
{
	uint64_t msr_list_addr = mon_vmcs_read(vmcs, list_address);
	uint32_t msr_list_count = (uint32_t)mon_vmcs_read(vmcs, list_count);
	ia32_vmx_msr_entry_t *msr_list_addr_ptr = NULL;
	ia32_vmx_msr_entry_t *new_msr_ptr;
	uint32_t i;

	/* Retrieve pointer to a MSR list. */
	if (msr_list_addr != VMCS_INVALID_ADDRESS) {
		/* Get pointer to MSR list */
		if (is_addres_hpa) {
			/* Address that is written in VMCS is HPA, convert it to pointer */
			if (!mon_hmm_hpa_to_hva
				    ((hpa_t)msr_list_addr,
				    (hva_t *)&msr_list_addr_ptr)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Failed to retrieve hva_t of MSR list from hpa_t=%P\n",
					__FUNCTION__,
					msr_list_addr);
				MON_DEADLOOP();
			}
		} else {
			/* Address that is written in VMCS is HVA */
			msr_list_addr_ptr =
				(ia32_vmx_msr_entry_t *)msr_list_addr;
		}

		MON_ASSERT(msr_list_addr_ptr != NULL);
		MON_ASSERT(ALIGN_BACKWARD
				((uint64_t)msr_list_addr_ptr,
				sizeof(ia32_vmx_msr_entry_t)) ==
			(uint64_t)msr_list_addr_ptr);
	}

	MON_ASSERT(*max_msrs_counter <= 256);

	/* Check that MSR is not already in a list. */
	for (i = 0, new_msr_ptr = msr_list_addr_ptr; i < msr_list_count;
	     i++, new_msr_ptr++)
		if ((new_msr_ptr != NULL) &&
		    (new_msr_ptr->msr_index == msr_index)) {
			break;
		}

	if (i >= msr_list_count) {
		/* Check if a MSR list should be re/allocated. */
		if (msr_list_count >= *max_msrs_counter) {
			/* The list is full or not allocated, expand it */
			ia32_vmx_msr_entry_t *new_msr_list_addr_ptr = NULL;
			uint32_t new_max_counter;

			MON_ASSERT(*max_msrs_counter < 256);

			/* Allocate new list */
			vmcs_alloc_msr_list(msr_list_count + 1,
				(void **)&new_msr_list_addr_ptr,
				&new_max_counter);
			MON_ASSERT(new_msr_list_addr_ptr != NULL);

			if (msr_list_count != 0) {
				vmcs_copy_msr_list(new_msr_list_addr_ptr,
					msr_list_addr_ptr,
					msr_list_count);
				mon_mfree(msr_list_addr_ptr);
			}

			msr_list_addr_ptr = new_msr_list_addr_ptr;

			if (is_addres_hpa) {
				uint64_t msr_list_addr_hpa;

				if (!mon_hmm_hva_to_hpa
					    ((uint64_t)msr_list_addr_ptr,
					    &msr_list_addr_hpa)) {
					MON_LOG(mask_anonymous,
						level_trace,
						"%s: Failed to retrieve hpa_t of MSR list\n",
						__FUNCTION__);
					MON_DEADLOOP();
				}
				mon_vmcs_write(vmcs,
					list_address,
					msr_list_addr_hpa);
			} else {
				mon_vmcs_write(vmcs,
					list_address,
					(uint64_t)msr_list_addr_ptr);
			}

			*max_msrs_counter = new_max_counter;
		}

		new_msr_ptr = msr_list_addr_ptr + msr_list_count;

		mon_vmcs_write(vmcs, list_count, msr_list_count + 1);
	}

	if (new_msr_ptr != NULL) {
		new_msr_ptr->msr_index = msr_index;
		new_msr_ptr->reserved = 0;
		new_msr_ptr->msr_data = value;
	}
}

void vmcs_delete_msr_from_list(vmcs_object_t *vmcs,
			       uint32_t msr_index, vmcs_field_t list_address,
			       vmcs_field_t list_count, boolean_t is_addres_hpa)
{
	uint64_t msr_list_addr = mon_vmcs_read(vmcs, list_address);
	uint32_t msr_list_count = (uint32_t)mon_vmcs_read(vmcs, list_count);
	ia32_vmx_msr_entry_t *msr_list_addr_ptr = NULL;
	ia32_vmx_msr_entry_t *msr_ptr;
	uint32_t i;

	if (msr_list_count != 0 && msr_list_addr != VMCS_INVALID_ADDRESS) {
		/* Get pointer to MSR list */
		if (is_addres_hpa) {
			/* Address that is written in VMCS is HPA, convert it to pointer */
			if (!mon_hmm_hpa_to_hva
				    ((hpa_t)msr_list_addr,
				    (hva_t *)&msr_list_addr_ptr)) {
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Failed to retrieve hva_t of MSR list from hpa_t=%P\n",
					__FUNCTION__,
					msr_list_addr);
				MON_DEADLOOP();
			}
		} else {
			/* Address that is written in VMCS is HVA */
			msr_list_addr_ptr =
				(ia32_vmx_msr_entry_t *)msr_list_addr;
		}

		MON_ASSERT(msr_list_addr_ptr != NULL);
		MON_ASSERT(ALIGN_BACKWARD
				((uint64_t)msr_list_addr_ptr,
				sizeof(ia32_vmx_msr_entry_t)) ==
			(uint64_t)msr_list_addr_ptr);

		/* Look for that MSR in a list. */
		for (i = 0, msr_ptr = msr_list_addr_ptr; i < msr_list_count;
		     i++, msr_ptr++) {
			if (msr_ptr->msr_index == msr_index) {
				/* Shift the rest of a list by one up. */
				uint32_t msrs_to_copy = msr_list_count - i - 1;

				if (msrs_to_copy > 0) {
					vmcs_copy_msr_list(msr_ptr,
						msr_ptr + 1,
						msrs_to_copy);
				}

				/* Save new list size. */
				mon_vmcs_write(vmcs,
					list_count,
					msr_list_count - 1);

				break;
			}
		}
	}
}

void vmcs_add_msr_to_vmexit_store_and_vmenter_load_lists_internal(
	vmcs_object_t *vmcs,
	uint32_t msr_index, uint64_t value,
	boolean_t is_msr_list_addr_hpa)
{
	uint64_t vmexit_store_addr = mon_vmcs_read(vmcs,
		VMCS_EXIT_MSR_STORE_ADDRESS);
	uint64_t vmenter_load_addr = mon_vmcs_read(vmcs,
		VMCS_ENTER_MSR_LOAD_ADDRESS);

	if ((vmexit_store_addr != vmenter_load_addr) &&
	    (vmenter_load_addr != VMCS_INVALID_ADDRESS)) {
		vmcs_add_msr_to_vmexit_store_list_internal(vmcs,
			msr_index,
			value,
			is_msr_list_addr_hpa);
		vmcs_add_msr_to_vmenter_load_list_internal(vmcs,
			msr_index,
			value,
			is_msr_list_addr_hpa);
	} else {
		vmcs_add_msr_to_vmexit_store_list_internal(vmcs,
			msr_index,
			value,
			is_msr_list_addr_hpa);
		MON_ASSERT(mon_vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_ADDRESS) !=
			VMCS_INVALID_ADDRESS);
		MON_ASSERT(mon_vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_COUNT) > 0);

		mon_vmcs_write(vmcs, VMCS_ENTER_MSR_LOAD_ADDRESS,
			mon_vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_ADDRESS));
		mon_vmcs_write(vmcs, VMCS_ENTER_MSR_LOAD_COUNT,
			mon_vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_COUNT));
	}
}

void vmcs_delete_msr_from_vmexit_store_and_vmenter_load_lists_internal(
	vmcs_object_t *vmcs,
	uint32_t msr_index,
	boolean_t is_msr_list_addr_hpa)
{
	uint64_t vmexit_store_addr = mon_vmcs_read(vmcs,
		VMCS_EXIT_MSR_STORE_ADDRESS);
	uint64_t vmenter_load_addr = mon_vmcs_read(vmcs,
		VMCS_ENTER_MSR_LOAD_ADDRESS);

	if (vmexit_store_addr != vmenter_load_addr) {
		vmcs_delete_msr_from_vmexit_store_list_internal(vmcs, msr_index,
			is_msr_list_addr_hpa);
		vmcs_delete_msr_from_vmenter_load_list_internal(vmcs, msr_index,
			is_msr_list_addr_hpa);
	} else if (vmenter_load_addr != VMCS_INVALID_ADDRESS) {
		vmcs_delete_msr_from_vmexit_store_list_internal(vmcs, msr_index,
			is_msr_list_addr_hpa);

		mon_vmcs_write(vmcs, VMCS_ENTER_MSR_LOAD_COUNT,
			mon_vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_COUNT));
	}
}

void vmcs_assign_vmexit_msr_load_list(vmcs_object_t *vmcs,
				      uint64_t address_value,
				      uint64_t count_value)
{
	mon_vmcs_write(vmcs, VMCS_EXIT_MSR_LOAD_ADDRESS, address_value);
	mon_vmcs_write(vmcs, VMCS_EXIT_MSR_LOAD_COUNT, count_value);
	/* in order to disable extension of current list */
	vmcs->max_num_of_vmexit_load_msrs = 0;
}

static
boolean_t vmcs_is_msr_in_list(vmcs_object_t *vmcs,
			      vmcs_field_t list_address_field,
			      vmcs_field_t list_count_field, uint32_t msr_index)
{
	uint64_t msr_list_addr = mon_vmcs_read(vmcs, list_address_field);
	uint32_t msr_list_count =
		(uint32_t)mon_vmcs_read(vmcs, list_count_field);
	ia32_vmx_msr_entry_t *msr_list_addr_ptr = NULL;
	uint32_t i;

	if (msr_list_count == 0) {
		return FALSE;
	}

	MON_ASSERT(msr_list_addr != VMCS_INVALID_ADDRESS);

	if (vmcs_get_level(vmcs) == VMCS_MERGED) {
		if (!mon_hmm_hpa_to_hva(msr_list_addr,
			    (hva_t *)(&msr_list_addr_ptr))) {
			MON_LOG(mask_anonymous,
				level_trace,
				"%s: Failed to retrieve hva_t of MSR list\n",
				__FUNCTION__);
			MON_DEADLOOP();
		}
	} else {
		msr_list_addr_ptr = (ia32_vmx_msr_entry_t *)msr_list_addr;
	}

	for (i = 0; i < msr_list_count; i++) {
		if (msr_list_addr_ptr[i].msr_index == msr_index) {
			return TRUE;
		}
	}

	return FALSE;
}

boolean_t vmcs_is_msr_in_vmexit_store_list(vmcs_object_t *vmcs,
					   uint32_t msr_index)
{
	return vmcs_is_msr_in_list(vmcs, VMCS_EXIT_MSR_STORE_ADDRESS,
		VMCS_EXIT_MSR_STORE_COUNT, msr_index);
}

boolean_t vmcs_is_msr_in_vmexit_load_list(vmcs_object_t *vmcs,
					  uint32_t msr_index)
{
	return vmcs_is_msr_in_list(vmcs, VMCS_EXIT_MSR_LOAD_ADDRESS,
		VMCS_EXIT_MSR_LOAD_COUNT, msr_index);
}

boolean_t vmcs_is_msr_in_vmenter_load_list(vmcs_object_t *vmcs,
					   uint32_t msr_index)
{
	return vmcs_is_msr_in_list(vmcs, VMCS_ENTER_MSR_LOAD_ADDRESS,
		VMCS_ENTER_MSR_LOAD_COUNT, msr_index);
}

void vmcs_print_msr_list(vmcs_object_t *vmcs, vmcs_field_t address,
			 vmcs_field_t count, boolean_t is_addr_in_hpa)
{
	uint32_t i;
	uint32_t max = (uint32_t)mon_vmcs_read(vmcs, count);
	uint64_t addr = mon_vmcs_read(vmcs, address);
	ia32_vmx_msr_entry_t *msr_list_ptr = NULL;

	CLI_PRINT("MSR list:\n");
	CLI_PRINT("=============\n");

	if (addr != VMCS_INVALID_ADDRESS) {
		if (is_addr_in_hpa) {
			if (!mon_hmm_hpa_to_hva(addr, (hva_t *)&msr_list_ptr)) {
				CLI_PRINT(
					"%s: Failed to translate hpa_t to hva_t\n",
					__FUNCTION__);
				MON_DEADLOOP();
			}
		} else {
			msr_list_ptr = (ia32_vmx_msr_entry_t *)addr;
		}

		for (i = 0; i < max; i++)
			CLI_PRINT("\t0x%x : %P\n", msr_list_ptr[i].msr_index,
				msr_list_ptr[i].msr_data);
	} else {
		CLI_PRINT("%s: Invalid VMCS address. \n", __FUNCTION__);
	}
	CLI_PRINT("=============\n\n");
}

void vmcs_print_vmenter_msr_load_list(vmcs_object_t *vmcs)
{
	vmcs_print_msr_list(vmcs, VMCS_ENTER_MSR_LOAD_ADDRESS,
		VMCS_ENTER_MSR_LOAD_COUNT,
		(vmcs_get_level(vmcs) == VMCS_MERGED));
}

void vmcs_print_vmexit_msr_store_list(vmcs_object_t *vmcs)
{
	vmcs_print_msr_list(vmcs, VMCS_EXIT_MSR_STORE_ADDRESS,
		VMCS_EXIT_MSR_STORE_COUNT,
		(vmcs_get_level(vmcs) == VMCS_MERGED));
}

extern gpm_handle_t gcpu_get_current_gpm(guest_handle_t guest);
extern boolean_t gpm_gpa_to_hva(gpm_handle_t gpm_handle, gpa_t gpa, hva_t *hva);

boolean_t mon_copy_to_guest_phy_addr(guest_cpu_handle_t gcpu, void *gpa,
				     uint32_t size, void *hva)
{
	uint64_t gpa_dst = (uint64_t)gpa;
	uint64_t hva_dst = 0;
	uint8_t *hva_src = (uint8_t *)hva;
	guest_handle_t guest;

	MON_ASSERT(gcpu);
	guest = mon_gcpu_guest_handle(gcpu);

	if (!gpm_gpa_to_hva(gcpu_get_current_gpm(guest), gpa_dst, &hva_dst)) {
		MON_LOG(mask_mon,
			level_error,
			"%s: Failed to convert gpa=%P to hva\n",
			__FUNCTION__,
			gpa_dst);
		return FALSE;
	}

	mon_memcpy((void *)hva_dst, hva_src, size);

	return TRUE;
}

/* Save initial vmcs state for deadloop/asssert handler */
void vmcs_store_initial(guest_cpu_handle_t gcpu, cpu_id_t cpu_id)
{
	vmcs_field_t field_id;
	vmcs_object_t *vmcs;
	uint32_t i, j, count;
	uint64_t *initial_vmcs;

	count =
		NELEMENTS(g_control_fields) + NELEMENTS(g_guest_state_fields) +
		NELEMENTS(g_host_state_fields);
	vmcs = mon_gcpu_get_vmcs(gcpu);

	if (g_initial_vmcs[cpu_id] == 0) {
		g_initial_vmcs[cpu_id] = (uint64_t)mon_malloc(
			sizeof(uint64_t) * count);
	}
	initial_vmcs = (uint64_t *)g_initial_vmcs[cpu_id];
	if (initial_vmcs == NULL) {
		MON_LOG(mask_anonymous,
			level_trace,
			"%s: Failed to allocate memory\n",
			__FUNCTION__);
		return;
	}

	j = 0;
	/* save control fields */
	for (i = 0; i < NELEMENTS(g_control_fields); i++) {
		field_id = g_control_fields[i];
		if (FIELD_IS_READABLE(field_id) &&
		    FIELD_IS_WRITEABLE(field_id)) {
			initial_vmcs[j++] = mon_vmcs_read(vmcs, field_id);
		}
	}
	/* save guest fields */
	for (i = 0; i < NELEMENTS(g_guest_state_fields); i++) {
		field_id = g_guest_state_fields[i];
		if (FIELD_IS_READABLE(field_id) &&
		    FIELD_IS_WRITEABLE(field_id)) {
			initial_vmcs[j++] = mon_vmcs_read(vmcs, field_id);
		}
	}
	/* save host fields */
	for (i = 0; i < NELEMENTS(g_host_state_fields); i++) {
		field_id = g_host_state_fields[i];
		if (FIELD_IS_READABLE(field_id) &&
		    FIELD_IS_WRITEABLE(field_id)) {
			initial_vmcs[j++] = mon_vmcs_read(vmcs, field_id);
		}
	}
}

/* Restore initial vmcs state for deadloop/asssert handler */
void vmcs_restore_initial(guest_cpu_handle_t gcpu)
{
	vmcs_field_t field_id;
	vmcs_object_t *vmcs;
	uint32_t i, j;
	uint64_t *initial_vmcs;
	cpu_id_t cpu_id;
	guest_handle_t guest;
	uint64_t eptp;
	uint64_t default_ept_root_table_hpa = 0;
	uint32_t default_ept_gaw = 0;

	cpu_id = hw_cpu_id();
	if (g_initial_vmcs[cpu_id] == 0) {
		return;
	}

	vmcs = mon_gcpu_get_vmcs(gcpu);
	initial_vmcs = (uint64_t *)g_initial_vmcs[cpu_id];

	/* write vmcs directly to HW */
	vmcs_sw_shadow_disable[cpu_id] = TRUE;

	j = 0;
	/* restore control fields */
	for (i = 0; i < NELEMENTS(g_control_fields); i++) {
		field_id = g_control_fields[i];
		if (FIELD_IS_READABLE(field_id) &&
		    FIELD_IS_WRITEABLE(field_id)) {
			mon_vmcs_write(vmcs, field_id, initial_vmcs[j++]);
		}
	}
	/* restore guest fields */
	for (i = 0; i < NELEMENTS(g_guest_state_fields); i++) {
		field_id = g_guest_state_fields[i];
		if (FIELD_IS_READABLE(field_id) &&
		    FIELD_IS_WRITEABLE(field_id)) {
			mon_vmcs_write(vmcs, field_id, initial_vmcs[j++]);
		}
	}
	/* restore host fields */
	for (i = 0; i < NELEMENTS(g_host_state_fields); i++) {
		field_id = g_host_state_fields[i];
		if (FIELD_IS_READABLE(field_id) &&
		    FIELD_IS_WRITEABLE(field_id)) {
			mon_vmcs_write(vmcs, field_id, initial_vmcs[j++]);
		}
	}

	/* Set EPTP to default EPT */
	guest = mon_gcpu_guest_handle(gcpu);
	ept_get_default_ept(guest, &default_ept_root_table_hpa,
		&default_ept_gaw);
	eptp = mon_ept_compute_eptp(guest, default_ept_root_table_hpa,
		default_ept_gaw);
	mon_vmcs_write(vmcs, VMCS_EPTP_ADDRESS, eptp);
}

/* required buffer byte size for control-532, guest-592, host-222
 * do not use malloc for tmp buffer, corrupted memory will trigger
 * nested assert causing deadloop handler to hang */
#define MAX_VMCS_BUF_SIZE       650

/* format vmcs info and write to guest buffer */
static
void vmcs_dump_group(guest_cpu_handle_t gcpu,
		     const vmcs_object_t *vmcs,
		     const vmcs_field_t *fields_to_print,
		     uint32_t count, uint64_t debug_gpa)
{
	char buf[MAX_VMCS_BUF_SIZE], *bufptr;
	uint32_t i;
	uint16_t entry_count;
	const vmcs_encoding_t *field_desc;
	vmcs_entry_t entry;

	entry_count = 0;

	/* skip over the count field */
	bufptr = (char *)buf + sizeof(uint16_t);

	for (i = 0; i < count; i++) {
		/* copy only the existing fields to guest buffer */
		field_desc = &g_field_data[fields_to_print[i]];
		if (field_desc->access == NO_EXIST) {
			continue;
		}

		entry.index = fields_to_print[i];
		entry.value = mon_vmcs_read(vmcs, fields_to_print[i]);
		entry_count++;

		/* copy vmcs entry to tmp buffer */
		mon_memcpy(bufptr, (void *)&entry, sizeof(vmcs_entry_t));
		bufptr = bufptr + sizeof(vmcs_entry_t);
	}

	/* save count to beginning of buffer */
	mon_memcpy(buf, (void *)&entry_count, sizeof(uint16_t));

	/* copy vmcs group to guest buffer */
	if (!mon_copy_to_guest_phy_addr(gcpu,
		    (void *)(debug_gpa),
		    sizeof(uint16_t) +
		    (sizeof(vmcs_entry_t) * entry_count),
		    (void *)buf)) {
		MON_LOG(mask_mon,
			level_error,
			"CPU%d: %s: Error: Could not copy vmcs message back to guest\n",
			hw_cpu_id(),
			__FUNCTION__);
	}
}

/* write all vmcs fields to guest buffer */
void vmcs_dump_all(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs;
	uint64_t debug_gpa;
	uint32_t control_size, guest_size, host_size;

	/* guest buffer is 4K size and vmcs starts at the 2K offset */
	control_size =
		sizeof(uint16_t) +
		(sizeof(vmcs_entry_t) * NELEMENTS(g_control_fields));
	guest_size =
		sizeof(uint16_t) +
		(sizeof(vmcs_entry_t) * NELEMENTS(g_guest_state_fields));
	host_size =
		sizeof(uint16_t) +
		(sizeof(vmcs_entry_t) * NELEMENTS(g_host_state_fields));

	if ((control_size + guest_size + host_size) > VMCS_SIZE) {
		MON_LOG(mask_mon, level_error,
			"%s: Error: Debug info exceeds guest buffer size\n",
			__FUNCTION__);
		return;
	}

	vmcs = mon_gcpu_get_vmcs(gcpu);

	/* write control fields to guest buffer */
	debug_gpa = g_debug_gpa + OFFSET_VMCS;
	vmcs_dump_group(gcpu, vmcs, g_control_fields, NELEMENTS(
			g_control_fields),
		debug_gpa);

	/* write guest fields to guest buffer */
	debug_gpa = debug_gpa + control_size;
	vmcs_dump_group(gcpu, vmcs, g_guest_state_fields,
		NELEMENTS(g_guest_state_fields), debug_gpa);

	/* write host fields to guest buffer */
	debug_gpa = debug_gpa + guest_size;
	vmcs_dump_group(gcpu, vmcs, g_host_state_fields,
		NELEMENTS(g_host_state_fields), debug_gpa);
}

extern boolean_t ept_is_ept_supported(void);
boolean_t mon_get_vmcs_guest_state(guest_cpu_handle_t gcpu,
				   mon_guest_state_t guest_state_id,
				   mon_guest_state_value_t *value)
{
	vmcs_object_t *vmcs;
	vmcs_field_t vmcs_field_id;
	vmentry_controls_t vmentry_control;

	MON_ASSERT(gcpu);

	vmcs = mon_gcpu_get_vmcs(gcpu);
	MON_ASSERT(vmcs);

	if (((uint32_t)guest_state_id) >
	    ((uint32_t)(NUM_OF_MON_GUEST_STATE - 1))) {
		return FALSE;
	}

	if (guest_state_id < MON_GUEST_CR0) {
		value->value =
			gcpu_get_gp_reg(gcpu,
				(mon_ia32_gp_registers_t)guest_state_id);
	} else if (guest_state_id == MON_GUEST_RIP) {
		value->value = gcpu_get_gp_reg(gcpu, IA32_REG_RIP);
	} else if (guest_state_id == MON_GUEST_PAT) {
		value->value = gcpu_get_msr_reg(gcpu, IA32_MON_MSR_PAT);
	} else if (guest_state_id == MON_GUEST_EFER) {
		value->value = gcpu_get_msr_reg(gcpu, IA32_MON_MSR_EFER);
	} else if (guest_state_id == MON_GUEST_CR8) {
		value->value = gcpu_get_control_reg(gcpu, IA32_CTRL_CR8);
	} else if (guest_state_id == MON_GUEST_IA32_PERF_GLOBAL_CTRL) {
		vmentry_control.uint32 = (uint32_t)mon_vmcs_read(vmcs,
			VMCS_ENTER_CONTROL_VECTOR);
		if (vmentry_control.bits.load_ia32_perf_global_ctrl) {
			value->value = mon_vmcs_read(vmcs,
				VMCS_GUEST_IA32_PERF_GLOBAL_CTRL);
		} else {
			value->value = UINT64_ALL_ONES;
		}
	} else if (guest_state_id == MON_GUEST_INTERRUPTIBILITY) {
		value->value = gcpu_get_interruptibility_state(gcpu);
	} else {
		if ((guest_state_id == MON_GUEST_PREEMPTION_TIMER)
		    && ept_is_ept_supported()) {
			vmcs_field_id = VMCS_PREEMPTION_TIMER;
		} else if ((guest_state_id >= MON_GUEST_CR0)
			   && (guest_state_id <= MON_GUEST_SYSENTER_EIP)) {
			vmcs_field_id =
				(vmcs_field_t)(guest_state_id - MON_GUEST_CR0 +
					       VMCS_GUEST_CR0);
		} else if (ept_is_ept_supported() &&
			   (guest_state_id >= MON_GUEST_PDPTR0)
			   && (guest_state_id <= MON_GUEST_PDPTR3)) {
			vmcs_field_id =
				(vmcs_field_t)(guest_state_id -
					       MON_GUEST_PDPTR0 +
					       VMCS_GUEST_PDPTR0);
		} else {
			return FALSE;
		}
		value->value = mon_vmcs_read(vmcs, vmcs_field_id);
	}

	return TRUE;
}

boolean_t mon_set_vmcs_guest_state(guest_cpu_handle_t gcpu,
				   mon_guest_state_t guest_state_id,
				   mon_guest_state_value_t value)
{
	vmcs_object_t *vmcs;
	vmcs_field_t vmcs_field_id;

	MON_ASSERT(gcpu);

	vmcs = mon_gcpu_get_vmcs(gcpu);
	MON_ASSERT(vmcs);

	if (((uint32_t)guest_state_id) >
	    ((uint32_t)(NUM_OF_MON_GUEST_STATE - 1))) {
		return FALSE;
	}

	if (guest_state_id < MON_GUEST_CR0) {
		gcpu_set_gp_reg(gcpu, (mon_ia32_gp_registers_t)guest_state_id,
			value.value);
	} else if (guest_state_id == MON_GUEST_PAT) {
		gcpu_set_msr_reg(gcpu, IA32_MON_MSR_PAT, value.value);
	} else if (guest_state_id == MON_GUEST_EFER) {
		gcpu_set_msr_reg(gcpu, IA32_MON_MSR_EFER, value.value);
	} else if (guest_state_id == MON_GUEST_CR0 || guest_state_id ==
		   MON_GUEST_CR4
		   || guest_state_id == MON_GUEST_IA32_PERF_GLOBAL_CTRL) {
		/* TBD;
		 * New functionality,needs to be implemented if required in future */
		return FALSE;
	} else if (guest_state_id == MON_GUEST_RIP) {
		if (TRUE == value.skip_rip) {
			gcpu_skip_guest_instruction(gcpu);
		} else {
			mon_vmcs_write(vmcs, VMCS_GUEST_RIP, value.value);
		}
	} else if (guest_state_id == MON_GUEST_INTERRUPTIBILITY) {
		gcpu_set_interruptibility_state(gcpu, (uint32_t)value.value);
	} else {
		if ((guest_state_id == MON_GUEST_PREEMPTION_TIMER)
		    && ept_is_ept_supported()) {
			vmcs_field_id = VMCS_PREEMPTION_TIMER;
		} else if ((guest_state_id >= MON_GUEST_CR0)
			   && (guest_state_id <= MON_GUEST_SYSENTER_EIP)) {
			vmcs_field_id =
				(vmcs_field_t)(guest_state_id - MON_GUEST_CR0 +
					       VMCS_GUEST_CR0);
		} else if (ept_is_ept_supported() &&
			   (guest_state_id >= MON_GUEST_PDPTR0)
			   && (guest_state_id <= MON_GUEST_PDPTR3)) {
			vmcs_field_id =
				(vmcs_field_t)(guest_state_id -
					       MON_GUEST_PDPTR0 +
					       VMCS_GUEST_PDPTR0);
		} else {
			return FALSE;
		}
		mon_vmcs_write(vmcs, vmcs_field_id, value.value);
	}

	return TRUE;
}
