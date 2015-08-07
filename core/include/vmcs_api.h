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

#ifndef _VMCS_API_H_
#define _VMCS_API_H_

#include "mon_dbg.h"
#include "mon_objects.h"
#include "memory_allocator.h"

/* means that the address is invalid */
#define VMCS_INVALID_ADDRESS    (address_t)(-1)

/* VMCS fields */
typedef enum {
	VMCS_VPID = 0,
	VMCS_EPTP_INDEX,
	VMCS_CONTROL_VECTOR_PIN_EVENTS,
	/* Special case - nmi_window cannot be updated using this value. Use
	 * special APIs to update nmi_window setting */
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
	VMCS_EXIT_INFO_GUEST_LINEAR_ADDRESS,
	VMCS_VIRTUAL_APIC_ADDRESS,
	VMCS_APIC_ACCESS_ADDRESS,
	VMCS_EXIT_TPR_THRESHOLD,
	VMCS_EPTP_ADDRESS,
	VMCS_PREEMPTION_TIMER,
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
	VMCS_GUEST_IA32_PERF_GLOBAL_CTRL,
	VMCS_GUEST_PDPTR0,
	VMCS_GUEST_PDPTR1,
	VMCS_GUEST_PDPTR2,
	VMCS_GUEST_PDPTR3,
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
	VMCS_HOST_EFER,
	VMCS_HOST_IA32_PERF_GLOBAL_CTRL,
	VMCS_CR3_TARGET_VALUE_0,
	VMCS_CR3_TARGET_VALUE_1,
	VMCS_CR3_TARGET_VALUE_2,
	VMCS_CR3_TARGET_VALUE_3,
	VMCS_VMFUNC_CONTROL,
	VMCS_VMFUNC_EPTP_LIST_ADDRESS,

	VMCS_VE_INFO_ADDRESS,

	/* last */
	VMCS_FIELD_COUNT
} vmcs_field_t;

#define VMCS_CR3_TARGET_VALUE(__x) (VMCS_CR3_TARGET_VALUE_0 + (__x))

#define VMCS_NOT_EXISTS         0
#define VMCS_READABLE           1
#define VMCS_WRITABLE           2
#define VMCS_WRITABLE_IN_CACHE  4

#define VMCS_SIGNATURE 0x12345678

typedef enum {
	VMCS_LEVEL_0,   /* VMCS of current level-1 MON */
	VMCS_LEVEL_1,   /* VMCS of level-0 MON. NULL means no layering */
	VMCS_MERGED,    /* merged VMCS; when no layering, identical to vmcs0 */
	VMCS_LEVELS
} vmcs_level_t;

typedef struct vmcs_object_t {
	uint32_t	signature;
	uint32_t	level;
	boolean_t	skip_access_checking;
	uint32_t	max_num_of_vmexit_store_msrs;
	uint32_t	max_num_of_vmexit_load_msrs;
	uint32_t	max_num_of_vmenter_load_msrs;
	uint64_t (*vmcs_read)(const struct vmcs_object_t *vmcs,
			      vmcs_field_t field_id);
	void (*vmcs_write)(struct vmcs_object_t *vmcs, vmcs_field_t field_id,
			   uint64_t value);
	void (*vmcs_flush_to_cpu)(const struct vmcs_object_t *vmcs);
	void (*vmcs_flush_to_memory)(struct vmcs_object_t *vmcs);
	boolean_t (*vmcs_is_dirty)(const struct vmcs_object_t *vmcs);
	guest_cpu_handle_t (*vmcs_get_owner)(const struct vmcs_object_t *vmcs);
	void (*vmcs_destroy)(struct vmcs_object_t *vmcs);
	void (*vmcs_add_msr_to_vmexit_store_list)(struct vmcs_object_t *vmcs,
						  uint32_t msr_index,
						  uint64_t value);
	void (*vmcs_add_msr_to_vmexit_load_list)(struct vmcs_object_t *vmcs,
						 uint32_t msr_index,
						 uint64_t value);
	void (*vmcs_add_msr_to_vmenter_load_list)(struct vmcs_object_t *vmcs,
						  uint32_t msr_index,
						  uint64_t value);
	void (*vmcs_add_msr_to_vmexit_store_and_vmenter_load_list)(struct
								   vmcs_object_t
								   *vmcs,
								   uint32_t
	msr_index, uint64_t value);
	void (*vmcs_delete_msr_from_vmexit_store_list)(struct vmcs_object_t *
						       vmcs,
						       uint32_t msr_index);
	void (*vmcs_delete_msr_from_vmexit_load_list)(struct vmcs_object_t *vmcs,
						      uint32_t msr_index);
	void (*vmcs_delete_msr_from_vmenter_load_list)(struct vmcs_object_t *
						       vmcs,
						       uint32_t msr_index);
	void (*vmcs_delete_msr_from_vmexit_store_and_vmenter_load_list)(struct
									vmcs_object_t
									*vmcs,
									uint32_t
	msr_index);
} vmcs_object_t;

void vmcs_copy(vmcs_object_t *vmcs_dst, const vmcs_object_t *vmcs_src);
void mon_vmcs_write(vmcs_object_t *vmcs, vmcs_field_t field_id, uint64_t value);
void vmcs_write_nocheck(vmcs_object_t *vmcs,
			vmcs_field_t field_id,
			uint64_t value);
uint64_t mon_vmcs_read(const vmcs_object_t *vmcs, vmcs_field_t field_id);
boolean_t vmcs_field_is_supported(vmcs_field_t field_id);

INLINE void vmcs_flush_to_cpu(const vmcs_object_t *vmcs)
{
	vmcs->vmcs_flush_to_cpu(vmcs);
}

INLINE void vmcs_flush_to_memory(vmcs_object_t *vmcs)
{
	vmcs->vmcs_flush_to_memory(vmcs);
}

INLINE void vmcs_clear_dirty(const vmcs_object_t *vmcs)
{
	vmcs->vmcs_flush_to_cpu(vmcs);
}

INLINE boolean_t vmcs_is_dirty(const vmcs_object_t *vmcs)
{
	return vmcs->vmcs_is_dirty(vmcs);
}

INLINE guest_cpu_handle_t vmcs_get_owner(const vmcs_object_t *vmcs)
{
	return vmcs->vmcs_get_owner(vmcs);
}

INLINE void vmcs_destroy(vmcs_object_t *vmcs)
{
	vmcs->vmcs_destroy(vmcs);
	mon_mfree(vmcs);
}

INLINE vmcs_level_t vmcs_get_level(vmcs_object_t *vmcs)
{
	return (vmcs_level_t)vmcs->level;
}

INLINE boolean_t vmcs_is_vmcs(vmcs_object_t *vmcs)
{
	return VMCS_SIGNATURE == vmcs->signature;
}

INLINE uint32_t vmcs_get_storage_size(void)
{
	return sizeof(uint64_t) * VMCS_FIELD_COUNT;
}

void vmcs_init_all_msr_lists(vmcs_object_t *vmcs);

INLINE void vmcs_add_msr_to_vmexit_store_list(vmcs_object_t *vmcs,
					      uint32_t msr_index,
					      uint64_t value)
{
	vmcs->vmcs_add_msr_to_vmexit_store_list(vmcs, msr_index, value);
}

INLINE void vmcs_add_msr_to_vmexit_load_list(vmcs_object_t *vmcs,
					     uint32_t msr_index, uint64_t value)
{
	vmcs->vmcs_add_msr_to_vmexit_load_list(vmcs, msr_index, value);
}

INLINE void vmcs_add_msr_to_vmenter_load_list(vmcs_object_t *vmcs,
					      uint32_t msr_index,
					      uint64_t value)
{
	vmcs->vmcs_add_msr_to_vmenter_load_list(vmcs, msr_index, value);
}

INLINE void vmcs_add_msr_to_vmexit_store_and_vmenter_load_lists(
	vmcs_object_t *vmcs,
	uint32_t msr_index,
	uint64_t value)
{
	vmcs->vmcs_add_msr_to_vmexit_store_and_vmenter_load_list(vmcs,
		msr_index,
		value);
}

#ifdef ENABLE_LAYERING
INLINE void vmcs_delete_msr_from_vmexit_store_list(vmcs_object_t *vmcs,
						   uint32_t msr_index)
{
	vmcs->vmcs_delete_msr_from_vmexit_store_list(vmcs, msr_index);
}

INLINE void vmcs_delete_msr_from_vmexit_load_list(vmcs_object_t *vmcs,
						  uint32_t msr_index)
{
	vmcs->vmcs_delete_msr_from_vmexit_load_list(vmcs, msr_index);
}

INLINE void vmcs_delete_msr_from_vmenter_load_list(vmcs_object_t *vmcs,
						   uint32_t msr_index)
{
	vmcs->vmcs_delete_msr_from_vmenter_load_list(vmcs, msr_index);
}

INLINE void vmcs_delete_msr_from_vmexit_store_and_vmenter_load_lists(
	vmcs_object_t *vmcs,
	uint32_t msr_index)
{
	vmcs->vmcs_delete_msr_from_vmexit_store_and_vmenter_load_list(vmcs,
		msr_index);
}
#endif

void vmcs_assign_vmexit_msr_load_list(vmcs_object_t *vmcs,
				      uint64_t address_value,
				      uint64_t count_value);

void vmcs_assign_vmexit_msr_load_list(vmcs_object_t *vmcs,
				      uint64_t address_value,
				      uint64_t count_value);
INLINE void vmcs_clear_vmexit_store_list(vmcs_object_t *vmcs)
{
	mon_vmcs_write(vmcs, VMCS_EXIT_MSR_STORE_COUNT, 0);
}

INLINE void vmcs_clear_vmexit_load_list(vmcs_object_t *vmcs)
{
	mon_vmcs_write(vmcs, VMCS_EXIT_MSR_LOAD_COUNT, 0);
}

INLINE void vmcs_clear_vmenter_load_list(vmcs_object_t *vmcs)
{
	mon_vmcs_write(vmcs, VMCS_ENTER_MSR_LOAD_COUNT, 0);
}

void vmcs_store(vmcs_object_t *vmcs, uint64_t *buffer);
void vmcs_load(vmcs_object_t *vmcs, uint64_t *buffer);
uint32_t vmcs_get_field_encoding(vmcs_field_t field_id, rw_access_t *p_access);
void vmcs_update(vmcs_object_t *vmcs,
		 vmcs_field_t field_id,
		 uint64_t value,
		 uint64_t bits_to_update);
void vmcs_manager_init(void);

/* is_HIGH_part is TRUE if encodign is for high part only of the VMCS field */
vmcs_field_t vmcs_get_field_id_by_encoding(uint32_t encoding,
					   OPTIONAL boolean_t *is_HIGH_part);

boolean_t vmcs_is_msr_in_vmexit_store_list(vmcs_object_t *vmcs,
					   uint32_t msr_index);

boolean_t vmcs_is_msr_in_vmexit_load_list(vmcs_object_t *vmcs,
					  uint32_t msr_index);

boolean_t vmcs_is_msr_in_vmenter_load_list(vmcs_object_t *vmcs,
					   uint32_t msr_index);

/*-------------------------------------------------------------------------
 *
 * Print VMCS fields
 *
 *------------------------------------------------------------------------- */
#ifdef CLI_INCLUDE
void vmcs_print_guest_state(const vmcs_object_t *obj);
void vmcs_print_host_state(const vmcs_object_t *obj);
void vmcs_print_controls(const vmcs_object_t *obj);
void vmcs_print_all(const vmcs_object_t *obj);
void vmcs_print_all_filtered(const vmcs_object_t *obj,
			     uint32_t num_of_filters,
			     char *filters[]);
#endif
const char *vmcs_get_field_name(vmcs_field_t field_id);
void vmcs_print_vmenter_msr_load_list(vmcs_object_t *vmcs);
void vmcs_print_vmexit_msr_store_list(vmcs_object_t *vmcs);

/* dump vmcs to guest buffer */
void vmcs_store_initial(guest_cpu_handle_t gcpu, cpu_id_t cpu_id);
void vmcs_restore_initial(guest_cpu_handle_t gcpu);
void vmcs_dump_all(guest_cpu_handle_t gcpu);

#endif                          /* _VMCS_API_H_ */
