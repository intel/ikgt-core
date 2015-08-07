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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMCALL_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMCALL_C, __condition)
#include "mon_defs.h"
#include "heap.h"
#include "hw_utils.h"
#include "guest.h"
#include "guest_cpu.h"
#include "gpm_api.h"
#include "vmexit.h"
#include "vmcall.h"
#include "mon_dbg.h"
#include "list.h"
#include "lock.h"
#include "memory_allocator.h"
#include "../guest/guest_cpu/unrestricted_guest.h"

/* MAX_ACTIVE_VMCALLS_PER_GUEST must be power of 2 */
#define MAX_ACTIVE_VMCALLS_PER_GUEST   64
#define UNALLOCATED_VMCALL             VMCALL_LAST_USED_INTERNAL

#define VMCALL_IS_VALID(__vmcall_id) ((__vmcall_id) != UNALLOCATED_VMCALL)

typedef struct {
	vmcall_handler_t	vmcall_handler;
	boolean_t		vmcall_special; /* e.g. for emuator termination */
	vmcall_id_t		vmcall_id;
} vmcall_entry_t;

typedef struct {
	guest_id_t	guest_id;
	uint8_t		padding[2];
	uint32_t	filled_entries_count;
	vmcall_entry_t	vmcall_table[MAX_ACTIVE_VMCALLS_PER_GUEST];
	list_element_t	list[1];
} guest_vmcall_entries_t;

typedef struct {
	list_element_t guest_vmcall_entries[1];
} vmcall_global_state_t;

/* for all guests */
static vmcall_global_state_t vmcall_global_state;

static mon_status_t vmcall_unimplemented(guest_cpu_handle_t gcpu,
					 address_t *arg1,
					 address_t *arg2,
					 address_t *arg3);
static mon_status_t vmcall_print_string(guest_cpu_handle_t gcpu,
					address_t *p_string,
					address_t *is_real_guest,
					address_t *arg3);

static vmexit_handling_status_t vmcall_common_handler(guest_cpu_handle_t gcpu);

static guest_vmcall_entries_t *vmcall_find_guest_vmcalls(guest_id_t guest_id);

static vmcall_entry_t *vmcall_get_vmcall_entry(guest_id_t guest_id,
					       vmcall_id_t vmcall_id);
boolean_t handle_int15_vmcall(guest_cpu_handle_t gcpu);

void vmcall_intialize(void)
{
	mon_memset(&vmcall_global_state, 0, sizeof(vmcall_global_state));
	list_init(vmcall_global_state.guest_vmcall_entries);
}

void vmcall_guest_intialize(guest_id_t guest_id)
{
	uint32_t id;
	guest_vmcall_entries_t *guest_vmcalls;
	vmcall_entry_t *vmcall_entry;

	MON_LOG(mask_mon, level_trace, "vmcall_guest_intialize start\r\n");

	guest_vmcalls =
		(guest_vmcall_entries_t *)mon_malloc(sizeof(
				guest_vmcall_entries_t));
	MON_ASSERT(guest_vmcalls);

	guest_vmcalls->guest_id = guest_id;
	guest_vmcalls->filled_entries_count = 0;

	list_add(vmcall_global_state.guest_vmcall_entries, guest_vmcalls->list);

	vmexit_install_handler(guest_id,
		vmcall_common_handler,
		IA32_VMX_EXIT_BASIC_REASON_VMCALL_INSTRUCTION);

	for (id = 0; id < MAX_ACTIVE_VMCALLS_PER_GUEST; ++id) {
		vmcall_entry = &guest_vmcalls->vmcall_table[id];
		vmcall_entry->vmcall_handler = vmcall_unimplemented;
		vmcall_entry->vmcall_id = UNALLOCATED_VMCALL;
	}
	MON_LOG(mask_mon, level_trace, "vmcall_guest_intialize end\r\n");
}

void mon_vmcall_register(guest_id_t guest_id,
			 vmcall_id_t vmcall_id,
			 vmcall_handler_t handler, boolean_t special_call)
{
	vmcall_entry_t *vmcall_entry;

	MON_ASSERT(NULL != handler);

	/* if already exists, check that all params are the same */
	vmcall_entry = vmcall_get_vmcall_entry(guest_id, vmcall_id);
	if (NULL != vmcall_entry) {
		if ((vmcall_entry->vmcall_id == vmcall_id) &&
		    (vmcall_entry->vmcall_handler == handler) &&
		    (vmcall_entry->vmcall_special == special_call)) {
			return;
		}

		MON_LOG(mask_mon, level_trace,
			"VMCALL %d is already registered for the Guest %d"
			" with different params\n",
			vmcall_id, guest_id);
		MON_ASSERT(FALSE);
	}

	vmcall_entry = vmcall_get_vmcall_entry(guest_id, UNALLOCATED_VMCALL);
	MON_ASSERT(vmcall_entry);
	MON_LOG(mask_mon, level_trace,
		"vmcall_register: guest %d vmcall_id %d vmcall_entry %p\r\n",
		guest_id, vmcall_id, vmcall_entry);

	vmcall_entry->vmcall_handler = handler;
	vmcall_entry->vmcall_special = special_call;
	vmcall_entry->vmcall_id = vmcall_id;
}

extern uint32_t g_is_post_launch;
vmexit_handling_status_t vmcall_common_handler(guest_cpu_handle_t gcpu)
{
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
	guest_id_t guest_id = guest_get_id(guest);
	vmcall_id_t vmcall_id;
	address_t arg1, arg2, arg3;
	mon_status_t ret_value;
	vmcall_handler_t vmcall_function;
	boolean_t is_vmcall_special = FALSE;
	vmcall_entry_t *vmcall_entry = NULL;
	vmexit_handling_status_t handle_status;

	/* Check INT15 only for pre-OS launch */
	if (!g_is_post_launch && mon_is_unrestricted_guest_supported()) {
		if (handle_int15_vmcall(gcpu)) {
			return VMEXIT_HANDLED;
		}
	}


	vmcall_id = (vmcall_id_t)gcpu_get_native_gp_reg(gcpu, IA32_REG_RCX);

	if (MON_NATIVE_VMCALL_SIGNATURE ==
	    gcpu_get_native_gp_reg(gcpu, IA32_REG_RAX)) {
		vmcall_entry = vmcall_get_vmcall_entry(guest_id, vmcall_id);
	}

	if (NULL != vmcall_entry) {
		MON_ASSERT(vmcall_entry->vmcall_id == vmcall_id);

		vmcall_function = vmcall_entry->vmcall_handler;
		is_vmcall_special = vmcall_entry->vmcall_special;
	} else {
		if (GUEST_LEVEL_2 == gcpu_get_guest_level(gcpu)) {
			/* VMCALL will be delivered to level#1 MON for processing */
			vmcall_function = NULL;
		} else {
			MON_LOG(mask_mon,
				level_trace,
				"ERROR: vmcall %d is not implemented\n",
				vmcall_id);
			vmcall_function = vmcall_unimplemented;
			is_vmcall_special = FALSE;
		}
	}

	if (NULL != vmcall_function) {
		if (TRUE == is_vmcall_special) {
			vmcall_function(gcpu, NULL, NULL, NULL);
		} else {
			arg1 = gcpu_get_native_gp_reg(gcpu, IA32_REG_RDX);
			arg2 = gcpu_get_native_gp_reg(gcpu, IA32_REG_RDI);
			arg3 = gcpu_get_native_gp_reg(gcpu, IA32_REG_RSI);

			/* Invoke vmcall_function that is registered for this vmcall_id */
			ret_value = vmcall_function(gcpu, &arg1, &arg2, &arg3);

			if (ret_value == MON_OK) {
				/* return arguments back to Guest, in case they were changed */
				gcpu_set_native_gp_reg(gcpu, IA32_REG_RDX,
					arg1);
				gcpu_set_native_gp_reg(gcpu, IA32_REG_RDI,
					arg2);
				gcpu_set_native_gp_reg(gcpu, IA32_REG_RSI,
					arg3);

				/* Skip instruction only if return_value is MON_OK */
				gcpu_skip_guest_instruction(gcpu);
			}
		}
		handle_status = VMEXIT_HANDLED;
	} else {
		MON_LOG(mask_mon, level_error,
			"CPU%d: %s: Error: VMEXIT_NOT_HANDLED\n",
			hw_cpu_id(), __FUNCTION__);
		handle_status = VMEXIT_NOT_HANDLED;
	}

	return handle_status;
}

mon_status_t vmcall_unimplemented(guest_cpu_handle_t gcpu USED_IN_DEBUG_ONLY,
				  address_t *arg1 UNUSED,
				  address_t *arg2 UNUSED,
				  address_t *arg3 UNUSED)
{
	MON_LOG(mask_mon, level_error,
		"CPU%d: %s: Error: Unimplemented VMCALL invoked on Guest ",
		hw_cpu_id(), __FUNCTION__);
	PRINT_GCPU_IDENTITY(gcpu);
	MON_LOG(mask_mon, level_error, "\n");

	return MON_ERROR;
}


static
guest_vmcall_entries_t *vmcall_find_guest_vmcalls(guest_id_t guest_id)
{
	list_element_t *iter = NULL;
	guest_vmcall_entries_t *guest_vmcalls = NULL;

	LIST_FOR_EACH(vmcall_global_state.guest_vmcall_entries, iter) {
		guest_vmcalls = LIST_ENTRY(iter, guest_vmcall_entries_t, list);
		if (guest_vmcalls->guest_id == guest_id) {
			return guest_vmcalls;
		}
	}
	return NULL;
}

static
vmcall_entry_t *find_guest_vmcall_entry(guest_vmcall_entries_t *guest_vmcalls,
					vmcall_id_t call_id)
{
	uint32_t idx;

	for (idx = 0; idx < MAX_ACTIVE_VMCALLS_PER_GUEST; ++idx) {
		if (guest_vmcalls->vmcall_table[idx].vmcall_id == call_id) {
			return &(guest_vmcalls->vmcall_table[idx]);
		}
	}

	return NULL;
}

static
vmcall_entry_t *vmcall_get_vmcall_entry(guest_id_t guest_id,
					vmcall_id_t vmcall_id)
{
	guest_vmcall_entries_t *guest_vmcalls;
	vmcall_entry_t *vmcall_entry;

	guest_vmcalls = vmcall_find_guest_vmcalls(guest_id);
	if (NULL == guest_vmcalls) {
		MON_ASSERT(0);
		return NULL;
	}

	vmcall_entry = find_guest_vmcall_entry(guest_vmcalls, vmcall_id);

	return vmcall_entry;
}
