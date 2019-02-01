/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "gcpu.h"
#include "vmexit.h"
#include "guest.h"
#include "dbg.h"
#include "gcpu_inject_event.h"
#include "heap.h"

#include "modules/vmcall.h"

typedef struct _vmcall_entry_t{
	uint32_t vmcall_id;
	uint32_t padding;
	vmcall_handler_t handler;
	struct _vmcall_entry_t *next;
} vmcall_entry_t;

typedef struct _vmcall_guest_entry_t{
	uint16_t guest_id;
	uint16_t padding[3];
	vmcall_entry_t *vmcall_entry;
	struct _vmcall_guest_entry_t *next;
} vmcall_guest_entry_t;

/* for all guests */
static vmcall_guest_entry_t *g_vmcall_guest;

static vmcall_guest_entry_t* init_vmcall_guest(uint16_t guest_id)
{
	vmcall_guest_entry_t *vmcall_guest;

	print_trace("%s(guest_id=%u)\n", __FUNCTION__, guest_id);

	vmcall_guest = (vmcall_guest_entry_t *)mem_alloc(
		sizeof(vmcall_guest_entry_t));

	vmcall_guest->guest_id = guest_id;
	vmcall_guest->vmcall_entry = NULL;
	vmcall_guest->next = g_vmcall_guest;
	g_vmcall_guest = vmcall_guest;

	return vmcall_guest;
}

static vmcall_guest_entry_t* find_vmcall_guest(uint16_t guest_id)
{
	vmcall_guest_entry_t *vmcall_guest;

	for (vmcall_guest = g_vmcall_guest; vmcall_guest; vmcall_guest=vmcall_guest->next)
	{
		if (vmcall_guest->guest_id == guest_id)
			return vmcall_guest;
	}
	return NULL;
}

static void init_vmcall_entry(vmcall_guest_entry_t* vmcall_guest,
	uint32_t vmcall_id, vmcall_handler_t vmcall_handler)
{
	vmcall_entry_t *vmcall_entry;

	print_trace("%s(guest_id=%u, vmcall_id=0x%x)\n", __FUNCTION__,
		vmcall_guest->guest_id, vmcall_id);

	vmcall_entry = (vmcall_entry_t *)mem_alloc(sizeof(vmcall_entry_t));

	vmcall_entry->vmcall_id = vmcall_id;
	vmcall_entry->handler = vmcall_handler;
	vmcall_entry->next = vmcall_guest->vmcall_entry;
	vmcall_guest->vmcall_entry = vmcall_entry;
}

static vmcall_entry_t* find_vmcall_entry(vmcall_guest_entry_t *vmcall_guest,
	uint32_t vmcall_id)
{
	vmcall_entry_t *vmcall_entry;

	if (!vmcall_guest) // vmcall_guest might be NULL when called from vmcall_common_handler.
		return NULL;

	for (vmcall_entry = vmcall_guest->vmcall_entry; vmcall_entry; vmcall_entry=vmcall_entry->next)
	{
		if (vmcall_entry->vmcall_id == vmcall_id)
			return vmcall_entry;
	}
	return NULL;
}

static void vmcall_common_handler(guest_cpu_handle_t gcpu)
{
	uint16_t guest_id = gcpu->guest->id;
	uint32_t vmcall_id;
	vmcall_entry_t *vmcall_entry;

	vmcall_id = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RAX);
	vmcall_entry = find_vmcall_entry(find_vmcall_guest(guest_id), vmcall_id);
	if (!vmcall_entry)
	{
		print_warn("guest %u, vmcall 0x%x not exist\n",
			guest_id, vmcall_id);
		gcpu_inject_ud(gcpu);
		return;
	}

	// no need to assert vmcall_entry->handler. it was asserted in vmcall_register()
	vmcall_entry->handler(gcpu);

	gcpu_skip_instruction(gcpu);
}

void vmcall_register(uint16_t guest_id, uint32_t vmcall_id,
			 vmcall_handler_t handler)
{
	vmcall_guest_entry_t *vmcall_guest;

	VMM_ASSERT_EX(handler, "vmcall handler is NULL\n");

	vmcall_guest = find_vmcall_guest(guest_id);
	if (vmcall_guest)
	{
		D(VMM_ASSERT(find_vmcall_entry(
			vmcall_guest, vmcall_id) == NULL));
	}
	else
	{
		vmcall_guest = init_vmcall_guest(guest_id);
	}
	init_vmcall_entry(vmcall_guest, vmcall_id, handler);
}

void vmcall_init()
{
	vmexit_install_handler(vmcall_common_handler, REASON_18_VMCALL_INSTR);
}

