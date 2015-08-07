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

#include "file_codes.h"
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMCS_ACTUAL_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMCS_ACTUAL_C, __condition)
#include "mon_defs.h"
#include "mon_dbg.h"
#include "memory_allocator.h"
#include "cache64.h"
#include "mon_objects.h"
#include "guest.h"
#include "gpm_api.h"
#include "vmcs_init.h"
#include "hw_vmx_utils.h"
#include "hw_utils.h"
#include "hw_interlocked.h"
#include "gdt.h"
#include "libc.h"
#include "vmcs_actual.h"
#include "vmcs_internal.h"
#include "vmx_nmi.h"

#define UPDATE_SUCCEEDED    0
#define UPDATE_FINISHED     1
#define UPDATE_FAILED       2

typedef enum {
	LAUNCHED_FLAG = 0,      /* was already launched */
	ACTIVATED_FLAG,         /* is set curent on the owning CPU */
	NEVER_ACTIVATED_FLAG    /* is in the init stage */
} flags_t;

#define SET_LAUNCHED_FLAG(obj)    BIT_SET((obj)->flags, LAUNCHED_FLAG)
#define CLR_LAUNCHED_FLAG(obj)    BIT_CLR((obj)->flags, LAUNCHED_FLAG)
#define GET_LAUNCHED_FLAG(obj)    BIT_GET((obj)->flags, LAUNCHED_FLAG)

#define SET_ACTIVATED_FLAG(obj)   BIT_SET((obj)->flags, ACTIVATED_FLAG)
#define CLR_ACTIVATED_FLAG(obj)   BIT_CLR((obj)->flags, ACTIVATED_FLAG)
#define GET_ACTIVATED_FLAG(obj)   BIT_GET((obj)->flags, ACTIVATED_FLAG)

#define SET_NEVER_ACTIVATED_FLAG(obj) \
	BIT_SET((obj)->flags, NEVER_ACTIVATED_FLAG)
#define CLR_NEVER_ACTIVATED_FLAG(obj) \
	BIT_CLR((obj)->flags, NEVER_ACTIVATED_FLAG)
#define GET_NEVER_ACTIVATED_FLAG(obj) \
	BIT_GET((obj)->flags, NEVER_ACTIVATED_FLAG)

#define FIELD_IS_HW_WRITABLE(__access) (VMCS_WRITABLE & (__access))

#define NMI_WINDOW_BIT  22

typedef struct {
	vmcs_object_t		vmcs_base[1];
	cache64_object_t	cache;
	address_t		hpa;
	address_t		hva;
	guest_cpu_handle_t	gcpu_owner;
	uint32_t		update_status;
	flags_t			flags;
	cpu_id_t		owning_host_cpu; /* the VMCS object was launched in this cpu */
	uint8_t			pad[6];
} vmcs_actual_object_t;

#define CPU_NEVER_USED ((cpu_id_t)-1)
#define HW_VMCS_IS_EMPTY ((uint64_t)-1)

static
const char *g_instr_error_message[] = {
	"VMCS_INSTR_NO_INSTRUCTION_ERROR",                                      /* VMxxxxx */
	"VMCS_INSTR_VMCALL_IN_ROOT_ERROR",                                      /* VMCALL */
	"VMCS_INSTR_VMCLEAR_INVALID_PHYSICAL_ADDRESS_ERROR",                    /* VMCLEAR */
	"VMCS_INSTR_VMCLEAR_WITH_CURRENT_CONTROLLING_PTR_ERROR",                /* VMCLEAR */
	"VMCS_INSTR_VMLAUNCH_WITH_NON_CLEAR_VMCS_ERROR",                        /* VMLAUNCH */
	"VMCS_INSTR_VMRESUME_WITH_NON_LAUNCHED_VMCS_ERROR",                     /* VMRESUME */
	"VMCS_INSTR_VMRESUME_WITH_NON_CHILD_VMCS_ERROR",                        /* VMRESUME */
	"VMCS_INSTR_VMENTER_BAD_CONTROL_FIELD_ERROR",                           /* VMENTER */
	"VMCS_INSTR_VMENTER_BAD_MONITOR_STATE_ERROR",                           /* VMENTER */
	"VMCS_INSTR_VMPTRLD_INVALID_PHYSICAL_ADDRESS_ERROR",                    /* VMPTRLD */
	"VMCS_INSTR_VMPTRLD_WITH_CURRENT_CONTROLLING_PTR_ERROR",                /* VMPTRLD */
	"VMCS_INSTR_VMPTRLD_WITH_BAD_REVISION_ID_ERROR",                        /* VMPTRLD */
	"VMCS_INSTR_VMREAD_OR_VMWRITE_OF_UNSUPPORTED_COMPONENT_ERROR",          /* VMREAD */
	"VMCS_INSTR_VMWRITE_OF_READ_ONLY_COMPONENT_ERROR",                      /* VMWRITE */
	"VMCS_INSTR_VMWRITE_INVALID_FIELD_VALUE_ERROR",                         /* VMWRITE */
	"VMCS_INSTR_VMXON_IN_VMX_ROOT_OPERATION_ERROR",                         /* VMXON */
	"VMCS_INSTR_VMENTRY_WITH_BAD_OSV_CONTROLLING_VMCS_ERROR",               /* VMENTER */
	/* VMENTER */
	"VMCS_INSTR_VMENTRY_WITH_NON_LAUNCHED_OSV_CONTROLLING_VMCS_ERROR",
	"VMCS_INSTR_VMENTRY_WITH_NON_ROOT_OSV_CONTROLLING_VMCS_ERROR",          /* VMENTER */
	"VMCS_INSTR_VMCALL_WITH_NON_CLEAR_VMCS_ERROR",                          /* VMCALL */
	"VMCS_INSTR_VMCALL_WITH_BAD_VMEXIT_FIELDS_ERROR",                       /* VMCALL */
	"VMCS_INSTR_VMCALL_WITH_INVALID_MSEG_MSR_ERROR",                        /* VMCALL */
	"VMCS_INSTR_VMCALL_WITH_INVALID_MSEG_REVISION_ERROR",                   /* VMCALL */
	"VMCS_INSTR_VMXOFF_WITH_CONFIGURED_SMM_MONITOR_ERROR",                  /* VMXOFF */
	"VMCS_INSTR_VMCALL_WITH_BAD_SMM_MONITOR_FEATURES_ERROR",                /* VMCALL */
	/* Return from SMM */
	"VMCS_INSTR_RETURN_FROM_SMM_WITH_BAD_VM_EXECUTION_CONTROLS_ERROR",
	"VMCS_INSTR_VMENTRY_WITH_EVENTS_BLOCKED_BY_MOV_SS_ERROR",       /* VMENTER */
	"VMCS_INSTR_BAD_ERROR_CODE",                                    /* Bad error code */
	"VMCS_INSTR_INVALIDATION_WITH_INVALID_OPERAND"                  /* INVEPT, INVVPID */
};

/*-------------------------- Forward Declarations ----------------------------*/
/*--- callbacks ---*/
static
uint64_t vmcs_act_read(const vmcs_object_t *vmcs, vmcs_field_t field_id);
static
void vmcs_act_write(vmcs_object_t *vmcs, vmcs_field_t field_id, uint64_t value);
static
void vmcs_act_flush_to_cpu(const vmcs_object_t *vmcs);
static
void vmcs_act_flush_to_memory(vmcs_object_t *vmcs);
static
boolean_t vmcs_act_is_dirty(const vmcs_object_t *vmcs);
static
guest_cpu_handle_t vmcs_act_get_owner(const vmcs_object_t *vmcs);
static
void vmcs_act_destroy(vmcs_object_t *vmcs);
static
void vmcs_act_add_msr_to_vmexit_store_list(vmcs_object_t *vmcs,
					   uint32_t msr_index,
					   uint64_t value);
static
void vmcs_act_add_msr_to_vmexit_load_list(vmcs_object_t *vmcs,
					  uint32_t msr_index,
					  uint64_t value);
static
void vmcs_act_add_msr_to_vmenter_load_list(vmcs_object_t *vmcs,
					   uint32_t msr_index,
					   uint64_t value);
static
void vmcs_act_add_msr_to_vmexit_store_and_vmenter_load_lists(vmcs_object_t
							     *vmcs,
							     uint32_t msr_index,
							     uint64_t value);
static
void vmcs_act_delete_msr_from_vmexit_store_list(vmcs_object_t *vmcs,
						uint32_t msr_index);
static
void vmcs_act_delete_msr_from_vmexit_load_list(vmcs_object_t *vmcs,
					       uint32_t msr_index);
static
void vmcs_act_delete_msr_from_vmenter_load_list(vmcs_object_t
						*vmcs, uint32_t msr_index);
static
void vmcs_act_delete_msr_from_vmexit_store_and_vmenter_load_lists(vmcs_object_t
								  *vmcs,
								  uint32_t
								  msr_index);

/*--- low level ---*/
static
void vmcs_act_flush_field_to_cpu(uint32_t entry_no,
				 vmcs_actual_object_t *p_vmcs);
static
void vmcs_act_flush_nmi_depended_field_to_cpu(vmcs_actual_object_t *p_vmcs,
					      uint64_t value);
static
uint64_t vmcs_act_read_from_hardware(vmcs_actual_object_t *p_vmcs,
				     vmcs_field_t field_id);
static
void vmcs_act_write_to_hardware(vmcs_actual_object_t *p_vmcs,
				vmcs_field_t field_id,
				uint64_t value);

/*--- helper ---*/
static
uint64_t temp_replace_vmcs_ptr(uint64_t new_ptr);
static
void restore_previous_vmcs_ptr(uint64_t ptr_to_restore);
static
void error_processing(uint64_t vmcs,
		      hw_vmx_ret_value_t ret_val,
		      const char *operation,
		      vmcs_field_t field);

/* stores NMI Windows which should be injected per CPU */
static
boolean_t nmi_window[MON_MAX_CPU_SUPPORTED];

/*----------------------------------------------------------------------------*
*                              NMI Handling
*  When NMI occured:
*    FS := non zero value        ; mark that NMI occured during VMEXIT
*    nmi_window[cpu-no] := TRUE  ; mark that NMI Window should be injected on
*                                ; next VMENTER
*    spoil transaction status (see below).
*
*  When NMI-Window is set - like ordinar VMCS field
*  When NMI-Window is clear - clear it, but then check FS !=0 and if so, set
*                             NMI-Window back
*  When flushing VMCS cache into CPU:
*    do it in transactional way, i.e.
*        set start transaction flage
*        do the job
*        check if succeeded
*        if not repeat
*----------------------------------------------------------------------------*/
INLINE boolean_t nmi_is_nmi_occured(void)
{
	return 0 != hw_read_fs();
}

INLINE void nmi_window_set(void)
{
	nmi_window[hw_cpu_id()] = TRUE;
}

INLINE void nmi_window_clear(void)
{
	nmi_window[hw_cpu_id()] = FALSE;
	if (nmi_is_nmi_occured()) {
		nmi_window[hw_cpu_id()] = TRUE;
	}
}

INLINE void nmi_remember_occured_nmi(void)
{
	hw_write_fs(DATA32_GDT_ENTRY_OFFSET);
	nmi_window_set();
}

INLINE boolean_t nmi_window_is_requested(void)
{
	return nmi_is_nmi_occured() || nmi_window[hw_cpu_id()];
}

void vmcs_nmi_handler(vmcs_object_t *vmcs)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)vmcs;
	uint64_t value;

	MON_ASSERT(p_vmcs);

	/* mark that NMI Window must be set, in case that SW still did not flush
	 * VMCSS to hardware */
	nmi_remember_occured_nmi();

	/* spoil VMCS flush process in case it is in progress */
	p_vmcs->update_status = UPDATE_FAILED;

	/* write directly into hardware in case that SW already did flush to CPU */
	value =
		vmcs_act_read_from_hardware(p_vmcs,
			VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
	BIT_SET64(value, NMI_WINDOW_BIT);
	vmcs_act_write_to_hardware(p_vmcs, VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS,
		value);
}

void nmi_window_update_before_vmresume(vmcs_object_t *vmcs)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)vmcs;
	uint64_t value;

	if (nmi_is_nmi_occured() || nmi_is_pending_this()) {
		MON_ASSERT(p_vmcs);

		value =
			vmcs_act_read_from_hardware(p_vmcs,
				VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);
		BIT_SET64(value, NMI_WINDOW_BIT);
		vmcs_act_write_to_hardware(p_vmcs,
			VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS,
			value);
		nmi_window_set();
	}
}

void vmcs_write_nmi_window_bit(vmcs_object_t *vmcs, boolean_t value)
{
	vmcs_update(vmcs, VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS,
		FALSE == value ? 0 : (uint64_t)-1, BIT_VALUE(NMI_WINDOW_BIT));

	if (value) {
		nmi_window_set();
	} else {
		nmi_window_clear();
	}
}

boolean_t vmcs_read_nmi_window_bit(vmcs_object_t *vmcs)
{
	uint64_t value = mon_vmcs_read(vmcs,
		VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS);

	return 0 != BIT_GET64(value, NMI_WINDOW_BIT);
}

vmcs_object_t *vmcs_act_create(guest_cpu_handle_t gcpu)
{
	vmcs_actual_object_t *p_vmcs;

	p_vmcs = mon_malloc(sizeof(*p_vmcs));
	if (NULL == p_vmcs) {
		MON_LOG(mask_anonymous,
			level_trace,
			"[vmcs] %s: Allocation failed\n",
			__FUNCTION__);
		return NULL;
	}

	p_vmcs->cache = cache64_create(VMCS_FIELD_COUNT);
	if (NULL == p_vmcs->cache) {
		mon_mfree(p_vmcs);
		MON_LOG(mask_anonymous,
			level_trace,
			"[vmcs] %s: Allocation failed\n",
			__FUNCTION__);
		return NULL;
	}
	/* validate it's ok - TBD */
	p_vmcs->hva = vmcs_hw_allocate_region(&p_vmcs->hpa);
	SET_NEVER_ACTIVATED_FLAG(p_vmcs);
	p_vmcs->owning_host_cpu = CPU_NEVER_USED;
	p_vmcs->gcpu_owner = gcpu;

	p_vmcs->vmcs_base->vmcs_read = vmcs_act_read;
	p_vmcs->vmcs_base->vmcs_write = vmcs_act_write;
	p_vmcs->vmcs_base->vmcs_flush_to_cpu = vmcs_act_flush_to_cpu;
	p_vmcs->vmcs_base->vmcs_flush_to_memory = vmcs_act_flush_to_memory;
	p_vmcs->vmcs_base->vmcs_is_dirty = vmcs_act_is_dirty;
	p_vmcs->vmcs_base->vmcs_get_owner = vmcs_act_get_owner;
	p_vmcs->vmcs_base->vmcs_destroy = vmcs_act_destroy;
	p_vmcs->vmcs_base->vmcs_add_msr_to_vmexit_store_list =
		vmcs_act_add_msr_to_vmexit_store_list;
	p_vmcs->vmcs_base->vmcs_add_msr_to_vmexit_load_list =
		vmcs_act_add_msr_to_vmexit_load_list;
	p_vmcs->vmcs_base->vmcs_add_msr_to_vmenter_load_list =
		vmcs_act_add_msr_to_vmenter_load_list;
	p_vmcs->vmcs_base->vmcs_add_msr_to_vmexit_store_and_vmenter_load_list =
		vmcs_act_add_msr_to_vmexit_store_and_vmenter_load_lists;
	p_vmcs->vmcs_base->vmcs_delete_msr_from_vmexit_store_list =
		vmcs_act_delete_msr_from_vmexit_store_list;
	p_vmcs->vmcs_base->vmcs_delete_msr_from_vmexit_load_list =
		vmcs_act_delete_msr_from_vmexit_load_list;
	p_vmcs->vmcs_base->vmcs_delete_msr_from_vmenter_load_list =
		vmcs_act_delete_msr_from_vmenter_load_list;
	p_vmcs->vmcs_base->
	vmcs_delete_msr_from_vmexit_store_and_vmenter_load_list =
		vmcs_act_delete_msr_from_vmexit_store_and_vmenter_load_lists;

	p_vmcs->vmcs_base->level = VMCS_MERGED;
	p_vmcs->vmcs_base->skip_access_checking = FALSE;
	p_vmcs->vmcs_base->signature = VMCS_SIGNATURE;

	vmcs_init_all_msr_lists(p_vmcs->vmcs_base);

	return p_vmcs->vmcs_base;
}

boolean_t vmcs_act_is_dirty(const vmcs_object_t *vmcs)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)vmcs;

	MON_ASSERT(p_vmcs);
	return cache64_is_dirty(p_vmcs->cache);
}

guest_cpu_handle_t vmcs_act_get_owner(const vmcs_object_t *vmcs)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)vmcs;

	MON_ASSERT(p_vmcs);
	return p_vmcs->gcpu_owner;
}

extern boolean_t vmcs_sw_shadow_disable[];
void vmcs_act_write(vmcs_object_t *vmcs, vmcs_field_t field_id,
		    uint64_t value)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)vmcs;

	MON_ASSERT(p_vmcs);
	if (!vmcs_sw_shadow_disable[hw_cpu_id()]) {
		cache64_write(p_vmcs->cache, value, (uint32_t)field_id);
	} else {
		vmcs_act_write_to_hardware(p_vmcs, field_id, value);
	}
}

uint64_t vmcs_act_read(const vmcs_object_t *vmcs, vmcs_field_t field_id)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)vmcs;
	uint64_t value;

	MON_ASSERT(p_vmcs);
	MON_ASSERT(field_id < VMCS_FIELD_COUNT);

	if (TRUE != cache64_read(p_vmcs->cache, &value, (uint32_t)field_id)) {
		/* special case - if hw VMCS was never filled, there is nothing to read
		 * from HW */
		if (GET_NEVER_ACTIVATED_FLAG(p_vmcs)) {
			/* assume the init was with all 0 */
			cache64_write(p_vmcs->cache, 0, (uint32_t)field_id);
			return 0;
		}

		value = vmcs_act_read_from_hardware(p_vmcs, field_id);
		/* update cache */
		cache64_write(p_vmcs->cache, value, (uint32_t)field_id);
	}
	return value;
}

uint64_t vmcs_act_read_from_hardware(vmcs_actual_object_t *p_vmcs,
				     vmcs_field_t field_id)
{
	uint64_t value;
	hw_vmx_ret_value_t ret_val;
	/* 0 - not replaced */
	uint64_t previous_vmcs = 0;
	uint32_t encoding;

	MON_DEBUG_CODE(if ((p_vmcs->owning_host_cpu != CPU_NEVER_USED) &&
			   (p_vmcs->owning_host_cpu != hw_cpu_id())) {
			MON_LOG(mask_anonymous, level_trace,
				"Trying to access VMCS, used on another CPU\n");
			MON_DEADLOOP();
		}
		)

	encoding = vmcs_get_field_encoding(field_id, NULL);
	MON_ASSERT(encoding != VMCS_NO_COMPONENT);

	/* if VMCS is not "current" now, make it current temporary */
	if (0 == GET_ACTIVATED_FLAG(p_vmcs)) {
		previous_vmcs = temp_replace_vmcs_ptr(p_vmcs->hpa);
	}

	ret_val = hw_vmx_read_current_vmcs(encoding, &value);

	if (ret_val != HW_VMX_SUCCESS) {
		error_processing(p_vmcs->hpa,
			ret_val, "hw_vmx_read_current_vmcs", field_id);
	}

	/* flush current VMCS if it was never used on this CPU */
	if (p_vmcs->owning_host_cpu == CPU_NEVER_USED) {
		ret_val = hw_vmx_flush_current_vmcs(&p_vmcs->hpa);

		if (ret_val != HW_VMX_SUCCESS) {
			error_processing(p_vmcs->hpa,
				ret_val,
				"hw_vmx_flush_current_vmcs", VMCS_FIELD_COUNT);
		}
	}

	/* restore the previous "current" VMCS */
	if (0 != previous_vmcs) {
		restore_previous_vmcs_ptr(previous_vmcs);
	}

	return value;
}

void vmcs_act_write_to_hardware(vmcs_actual_object_t *p_vmcs,
				vmcs_field_t field_id, uint64_t value)
{
	hw_vmx_ret_value_t ret_val;
	uint32_t encoding;
	rw_access_t access_type;

	MON_DEBUG_CODE(if ((p_vmcs->owning_host_cpu != CPU_NEVER_USED) &&
			   (p_vmcs->owning_host_cpu != hw_cpu_id())) {
			MON_LOG(mask_anonymous, level_trace,
				"Trying to access VMCS, used on another CPU\n");
			MON_DEADLOOP();
		}
		)

	encoding = vmcs_get_field_encoding(field_id, &access_type);
	MON_ASSERT(encoding != VMCS_NO_COMPONENT);

	if (0 == FIELD_IS_HW_WRITABLE(access_type)) {
		return;
	}

	ret_val = hw_vmx_write_current_vmcs(encoding, value);

	if (ret_val != HW_VMX_SUCCESS) {
		error_processing(p_vmcs->hpa,
			ret_val, "hw_vmx_write_current_vmcs", field_id);
	}
}

void vmcs_act_flush_to_cpu(const vmcs_object_t *vmcs)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)vmcs;

	/* MON_ASSERT(vmcs); checked by caller */
	MON_ASSERT(GET_ACTIVATED_FLAG(p_vmcs));
	MON_ASSERT(p_vmcs->owning_host_cpu == hw_cpu_id());

	/* in case the guest was re-scheduled, NMI Window is set in other VMCS **
	 * To speed the handling up, set NMI-Window in current VMCS if needed. */
	if (nmi_window_is_requested()) {
		vmcs_update((vmcs_object_t *)vmcs,
			VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS,
			UINT64_ALL_ONES, BIT_VALUE64(NMI_WINDOW_BIT));
	}

	if (cache64_is_dirty(p_vmcs->cache)) {
		cache64_flush_dirty(p_vmcs->cache,
			CACHE_ALL_ENTRIES, (func_cache64_field_process_t)
			vmcs_act_flush_field_to_cpu, p_vmcs);
	}
}

void vmcs_act_flush_field_to_cpu(uint32_t field_id,
				 vmcs_actual_object_t *p_vmcs)
{
	uint64_t value;

	if (FALSE == cache64_read(p_vmcs->cache, &value, field_id)) {
		MON_LOG(mask_anonymous, level_trace,
			"Read field %d from cache failed.\n", field_id);
		return;
	}

	if (VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS != field_id) {
		vmcs_act_write_to_hardware(p_vmcs, (vmcs_field_t)field_id,
			value);
	} else {
		vmcs_act_flush_nmi_depended_field_to_cpu(p_vmcs, value);
	}
}

void vmcs_act_flush_nmi_depended_field_to_cpu(vmcs_actual_object_t *p_vmcs,
					      uint64_t value)
{
	boolean_t success = FALSE;

	while (FALSE == success) {
		p_vmcs->update_status = UPDATE_SUCCEEDED;

		if (nmi_window_is_requested()) {
			BIT_SET64(value, NMI_WINDOW_BIT);
		}

		vmcs_act_write_to_hardware(p_vmcs,
			VMCS_CONTROL_VECTOR_PROCESSOR_EVENTS,
			value);

		if (UPDATE_SUCCEEDED ==
		    hw_interlocked_compare_exchange(&p_vmcs->update_status,
			    /* expected value */
			    UPDATE_SUCCEEDED,
			    /* new value */
			    UPDATE_FINISHED)) {
			success = TRUE;
		} else {
			MON_DEBUG_CODE(MON_LOG
					(mask_anonymous, level_trace,
					"nmi Occured during update\n"););
		}
	}
}

void vmcs_act_flush_to_memory(vmcs_object_t *vmcs)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)vmcs;
	hw_vmx_ret_value_t ret_val;
	uint64_t previous_vmcs;

	MON_ASSERT(p_vmcs);

	MON_ASSERT(GET_ACTIVATED_FLAG(p_vmcs) == 0);

	if (p_vmcs->owning_host_cpu == CPU_NEVER_USED) {
		return;
	}

	MON_ASSERT(hw_cpu_id() == p_vmcs->owning_host_cpu);

	hw_vmx_get_current_vmcs(&previous_vmcs);

	/* make my active temporary */
	vmcs_activate(vmcs);

	/* flush all modifications from cache to CPU */
	vmcs_act_flush_to_cpu(vmcs);

	/* now flush from hardware */
	ret_val = hw_vmx_flush_current_vmcs(&p_vmcs->hpa);

	if (ret_val != HW_VMX_SUCCESS) {
		error_processing(p_vmcs->hpa,
			ret_val,
			"hw_vmx_flush_current_vmcs", VMCS_FIELD_COUNT);
	}

	vmcs_deactivate(vmcs);

	/* reset launching field */
	CLR_LAUNCHED_FLAG(p_vmcs);

	p_vmcs->owning_host_cpu = CPU_NEVER_USED;

	/* restore previous */
	restore_previous_vmcs_ptr(previous_vmcs);
}

void vmcs_act_destroy(vmcs_object_t *vmcs)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)vmcs;

	MON_ASSERT(p_vmcs);

	vmcs_act_flush_to_memory(vmcs);
	vmcs_destroy_all_msr_lists_internal(vmcs, TRUE);
	cache64_destroy(p_vmcs->cache);
	mon_mfree((void *)p_vmcs->hva);
}

/*
 * Handle temporary VMCS PTR replacements
 */
/* return previous ptr */
uint64_t temp_replace_vmcs_ptr(uint64_t new_ptr)
{
	hw_vmx_ret_value_t ret_val;
	uint64_t previous_vmcs;

	hw_vmx_get_current_vmcs(&previous_vmcs);

	ret_val = hw_vmx_set_current_vmcs(&new_ptr);

	if (ret_val != HW_VMX_SUCCESS) {
		error_processing(new_ptr,
			ret_val, "hw_vmx_set_current_vmcs", VMCS_FIELD_COUNT);
	}

	return previous_vmcs;
}

void restore_previous_vmcs_ptr(uint64_t ptr_to_restore)
{
	hw_vmx_ret_value_t ret_val;
	uint64_t temp_vmcs_ptr;

	/* restore previous VMCS pointer */
	if (ptr_to_restore != HW_VMCS_IS_EMPTY) {
		ret_val = hw_vmx_set_current_vmcs(&ptr_to_restore);

		if (ret_val != HW_VMX_SUCCESS) {
			error_processing(ptr_to_restore,
				ret_val,
				"hw_vmx_set_current_vmcs", VMCS_FIELD_COUNT);
		}
	} else {
		/* reset hw VMCS pointer */
		hw_vmx_get_current_vmcs(&temp_vmcs_ptr);

		if (temp_vmcs_ptr != HW_VMCS_IS_EMPTY) {
			ret_val = hw_vmx_flush_current_vmcs(&temp_vmcs_ptr);

			if (ret_val != HW_VMX_SUCCESS) {
				error_processing(temp_vmcs_ptr,
					ret_val,
					"hw_vmx_flush_current_vmcs",
					VMCS_FIELD_COUNT);
			}
		}
	}
}

/*-------------------------------------------------------------------------
 *
 * Reset all read caching. MUST NOT be called with modifications not flushed to
 * hw
 *
 *------------------------------------------------------------------------- */
void vmcs_clear_cache(vmcs_object_t *obj)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)obj;

	MON_ASSERT(p_vmcs);
	cache64_invalidate(p_vmcs->cache, CACHE_ALL_ENTRIES);
}

/*
 * Activate
 */
void vmcs_activate(vmcs_object_t *obj)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)obj;
	cpu_id_t this_cpu = hw_cpu_id();
	hw_vmx_ret_value_t ret_val;

	MON_ASSERT(obj);
	MON_ASSERT(p_vmcs->hpa);
	MON_ASSERT(GET_ACTIVATED_FLAG(p_vmcs) == 0);

	MON_DEBUG_CODE(if ((p_vmcs->owning_host_cpu != CPU_NEVER_USED) &&
			   (p_vmcs->owning_host_cpu != this_cpu)) {
			MON_LOG(mask_anonymous,
				level_trace,
				"Trying to activate VMCS, used on another CPU\n");
			MON_DEADLOOP();
		}
		)

	/* special case - if VMCS is still in the initialization state
	 * (first load) init the hw before activating it */
	if (GET_NEVER_ACTIVATED_FLAG(p_vmcs)) {
		ret_val = hw_vmx_flush_current_vmcs(&p_vmcs->hpa);

		if (ret_val != HW_VMX_SUCCESS) {
			error_processing(p_vmcs->hpa,
				ret_val,
				"hw_vmx_flush_current_vmcs", VMCS_FIELD_COUNT);
		}
	}

	ret_val = hw_vmx_set_current_vmcs(&p_vmcs->hpa);

	if (ret_val != HW_VMX_SUCCESS) {
		error_processing(p_vmcs->hpa,
			ret_val, "hw_vmx_set_current_vmcs", VMCS_FIELD_COUNT);
	}

	p_vmcs->owning_host_cpu = this_cpu;
	SET_ACTIVATED_FLAG(p_vmcs);
	MON_ASSERT(GET_ACTIVATED_FLAG(p_vmcs) == 1);

	CLR_NEVER_ACTIVATED_FLAG(p_vmcs);
}

/*
 * Deactivate
 */
void vmcs_deactivate(vmcs_object_t *obj)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)obj;

	MON_ASSERT(obj);
	MON_ASSERT(GET_ACTIVATED_FLAG(p_vmcs) == 1);
	MON_ASSERT(hw_cpu_id() == p_vmcs->owning_host_cpu);
	CLR_ACTIVATED_FLAG(p_vmcs);
}

boolean_t vmcs_launch_required(const vmcs_object_t *obj)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)obj;

	MON_ASSERT(p_vmcs);

	return GET_LAUNCHED_FLAG(p_vmcs) == 0;
}

void vmcs_set_launched(vmcs_object_t *obj)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)obj;

	MON_ASSERT(p_vmcs);
	MON_ASSERT(GET_LAUNCHED_FLAG(p_vmcs) == 0);

	SET_LAUNCHED_FLAG(p_vmcs);
}

void vmcs_set_launch_required(vmcs_object_t *obj)
{
	vmcs_actual_object_t *p_vmcs = (vmcs_actual_object_t *)obj;

	MON_ASSERT(p_vmcs);

	CLR_LAUNCHED_FLAG(p_vmcs);
}

/*
 * Error message
 */
vmcs_instruction_error_t vmcs_last_instruction_error_code(
	const vmcs_object_t *obj,
	const char
	**error_message)
{
	uint32_t err = (uint32_t)mon_vmcs_read(obj,
		VMCS_EXIT_INFO_INSTRUCTION_ERROR_CODE);

	if (error_message) {
		*error_message = (err <= VMCS_INSTR_BAD_ERROR_CODE) ?
				 g_instr_error_message[err] :
				 "UNKNOWN VMCS_EXIT_INFO_INSTRUCTION_ERROR_CODE";
	}

	return (vmcs_instruction_error_t)err;
}

void error_processing(uint64_t vmcs,
		      hw_vmx_ret_value_t ret_val,
		      const char *operation, vmcs_field_t field)
{
	const char *error_message = 0;
	uint64_t err = 0;
	hw_vmx_ret_value_t my_err;

	switch (ret_val) {
	case HW_VMX_SUCCESS:
		return;

	case HW_VMX_FAILED_WITH_STATUS:
		/* use hard-coded encoding */
		my_err = hw_vmx_read_current_vmcs(
			VM_EXIT_INFO_INSTRUCTION_ERROR_CODE,
			&err);

		if (my_err == HW_VMX_SUCCESS) {
			error_message = g_instr_error_message[(uint32_t)err];
			break;
		}

	/* fall through */

	case HW_VMX_FAILED:
	default:
		error_message = "operation FAILED";
	}

	if (field == VMCS_FIELD_COUNT) {
		MON_LOG(mask_anonymous, level_trace,
			"%s( %P ) failed with the error: %s\n", operation, vmcs,
			error_message ? error_message : "unknown error");
	} else {
		MON_LOG(mask_anonymous,
			level_trace,
			"%s( %P, %s ) failed with the error: %s\n",
			operation,
			vmcs,
			vmcs_get_field_name(field),
			error_message ? error_message : "unknown error");
	}

	MON_DEADLOOP();
	return;
}


static
void vmcs_act_add_msr_to_vmexit_store_list(vmcs_object_t *vmcs,
					   uint32_t msr_index, uint64_t value)
{
	vmcs_add_msr_to_vmexit_store_list_internal(vmcs, msr_index, value,
		TRUE);
}

static
void vmcs_act_add_msr_to_vmexit_load_list(vmcs_object_t *vmcs,
					  uint32_t msr_index, uint64_t value)
{
	vmcs_add_msr_to_vmexit_load_list_internal(vmcs, msr_index, value, TRUE);
}

static
void vmcs_act_add_msr_to_vmenter_load_list(vmcs_object_t *vmcs,
					   uint32_t msr_index, uint64_t value)
{
	vmcs_add_msr_to_vmenter_load_list_internal(vmcs, msr_index, value,
		TRUE);
}

static
void vmcs_act_add_msr_to_vmexit_store_and_vmenter_load_lists(vmcs_object_t
							     *vmcs,
							     uint32_t msr_index,
							     uint64_t value)
{
	vmcs_add_msr_to_vmexit_store_and_vmenter_load_lists_internal(vmcs,
		msr_index,
		value, TRUE);
}

static
void vmcs_act_delete_msr_from_vmexit_store_list(vmcs_object_t *vmcs,
						uint32_t msr_index)
{
	vmcs_delete_msr_from_vmexit_store_list_internal(vmcs, msr_index, TRUE);
}

static
void vmcs_act_delete_msr_from_vmexit_load_list(vmcs_object_t *vmcs,
					       uint32_t msr_index)
{
	vmcs_delete_msr_from_vmexit_load_list_internal(vmcs, msr_index, TRUE);
}

static
void vmcs_act_delete_msr_from_vmenter_load_list(vmcs_object_t *vmcs,
						uint32_t msr_index)
{
	vmcs_delete_msr_from_vmenter_load_list_internal(vmcs, msr_index, TRUE);
}

static
void vmcs_act_delete_msr_from_vmexit_store_and_vmenter_load_lists(vmcs_object_t
								  *vmcs,
								  uint32_t
								  msr_index)
{
	vmcs_delete_msr_from_vmexit_store_and_vmenter_load_lists_internal(vmcs,
		msr_index,
		TRUE);
}
