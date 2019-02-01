/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_objects.h"
#include "vmcs.h"
#include "vmexit.h"
#include "gcpu.h"
#include "guest.h"
#include "event.h"
#include "host_cpu.h"
#include "vmm_base.h"

#include "modules/vmx_timer.h"

typedef struct _vmx_timer_t{
	guest_cpu_handle_t gcpu;
	uint32_t mode;
	uint32_t padding;
	uint64_t periodic; // valid only when mode is period
	struct _vmx_timer_t* next;
} vmx_timer_t;

// each host cpu maintain its own g_vmx_timer, so that we don't need a lock here
static vmx_timer_t *g_vmx_timer[MAX_CPU_NUM];

static void vmx_timer_vmexit (guest_cpu_handle_t gcpu);

static vmx_timer_t * create_vmx_timer(guest_cpu_handle_t gcpu)
{
	vmx_timer_t* vmx_timer;
	uint16_t hcpu_id = host_cpu_id();

	print_trace("creating vmx timer for guest%d, cpu%d\n",
		gcpu->guest->id, gcpu->id);

	vmx_timer = (vmx_timer_t*)mem_alloc(sizeof(vmx_timer_t));

	vmx_timer->gcpu = gcpu;
	vmx_timer->next = g_vmx_timer[hcpu_id];
	g_vmx_timer[hcpu_id] = vmx_timer;
	return vmx_timer;
}

/* find registered vmx_timer the input gcpu
** when not found:
**	if create_if_not_found is FALSE, return NULL
**	if create_if_not_found is TRUE, create vmx_timer and install vmexit handler
**		if it is a new guest
*/
static vmx_timer_t * find_vmx_timer(guest_cpu_handle_t gcpu, boolean_t create_if_not_found)
{
	vmx_timer_t* vmx_timer;
	uint16_t guest_id;

	guest_id = gcpu->guest->id;
	for (vmx_timer = g_vmx_timer[host_cpu_id()]; vmx_timer!=NULL; vmx_timer=vmx_timer->next)
	{
		if (vmx_timer->gcpu == gcpu)
		{
			break;
		}
		if ((guest_id != INVALID_GUEST_ID) &&
			(vmx_timer->gcpu->guest->id == guest_id))
			guest_id = INVALID_GUEST_ID;
	}
	if (vmx_timer)
		return vmx_timer;
	// not found
	if (create_if_not_found)
	{
		vmx_timer = create_vmx_timer(gcpu);
	}

	return vmx_timer;
}

static inline void vmx_timer_set_remaining(guest_cpu_handle_t gcpu, uint64_t tick)
{
	// gcpu is asserted by caller
	vmcs_write(gcpu->vmcs, VMCS_PREEMPTION_TIMER, tick);
}

static inline uint64_t vmx_timer_get_remaining(guest_cpu_handle_t gcpu)
{
	// gcpu is asserted by caller
	return vmcs_read(gcpu->vmcs, VMCS_PREEMPTION_TIMER);
}

static void vmx_timer_start(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;
	// gcpu is asserted by caller
	vmcs = gcpu->vmcs;

	vmcs_write(vmcs, VMCS_PIN_CTRL,
		vmcs_read(vmcs, VMCS_PIN_CTRL) |
		PIN_PREEMPTION_TIMER);
	vmcs_write(vmcs, VMCS_EXIT_CTRL,
		vmcs_read(vmcs, VMCS_EXIT_CTRL) |
		EXIT_SAVE_PREE_TIME);
}

static void vmx_timer_stop(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;
	// gcpu is asserted by caller
	vmcs = gcpu->vmcs;

	vmcs_write(vmcs, VMCS_PIN_CTRL,
		vmcs_read(vmcs, VMCS_PIN_CTRL) &
		~(PIN_PREEMPTION_TIMER));
	vmcs_write(vmcs, VMCS_EXIT_CTRL,
		vmcs_read(vmcs, VMCS_EXIT_CTRL) &
		~(EXIT_SAVE_PREE_TIME));
}

static inline boolean_t vmx_timer_is_started(guest_cpu_handle_t gcpu)
{
	uint32_t pin = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_PIN_CTRL);
	return (pin & (PIN_PREEMPTION_TIMER));
}

static void vmx_timer_vmexit (guest_cpu_handle_t gcpu)
{
	vmx_timer_t* vmx_timer = find_vmx_timer(gcpu, FALSE);

	D(VMM_ASSERT(vmx_timer));

	switch (vmx_timer->mode)
	{
		case TIMER_MODE_PERIOD:
			vmx_timer_set_remaining(gcpu, vmx_timer->periodic);
			break;
		case TIMER_MODE_ONESHOT:
			vmx_timer_stop(gcpu);
			vmx_timer->mode = TIMER_MODE_ONESHOT_DEACTIVE;
			break;
		default:
			print_panic("invalid mode %d\n", vmx_timer->mode);
			VMM_DEADLOOP();
	}

	event_raise(gcpu, EVENT_VMX_TIMER, NULL);

}

void vmx_timer_init(void)
{
	VMM_ASSERT_EX((get_pinctl_cap(NULL) & PIN_PREEMPTION_TIMER),
			"preemption timer is not supported\n");
	VMM_ASSERT_EX((get_exitctl_cap(NULL)& EXIT_SAVE_PREE_TIME),
			"preemption timer is not supported\n");
	D(VMM_ASSERT(get_tsc_per_ms()));

	vmexit_install_handler(vmx_timer_vmexit, REASON_52_PREEMP_TIMER);
}

void vmx_timer_set_mode(guest_cpu_handle_t gcpu, uint32_t mode, uint64_t periodic)
{
	vmx_timer_t* vmx_timer;

	print_trace("%s() mode=%d, periodic=0x%llx\n", __FUNCTION__, mode, periodic);

	D(VMM_ASSERT(gcpu));
	VMM_ASSERT_EX((mode <= TIMER_MODE_STOPPED),
			"vmx timer mode(%u) is invalid\n", mode);
	vmx_timer = find_vmx_timer(gcpu, TRUE);
	vmx_timer->mode = mode;
	if (mode == TIMER_MODE_STOPPED)
	{
		vmx_timer_stop(gcpu);
	}
	else // period or oneshot
	{
		if (mode == TIMER_MODE_PERIOD)
			vmx_timer->periodic = periodic;
		vmx_timer_set_remaining(gcpu, periodic);
		vmx_timer_start(gcpu);
	}
}

uint32_t vmx_timer_get_mode(guest_cpu_handle_t gcpu)
{
	vmx_timer_t* vmx_timer;

	D(VMM_ASSERT(gcpu));
	vmx_timer = find_vmx_timer(gcpu, FALSE);
	if(vmx_timer)
		return vmx_timer->mode;
	else
		return TIMER_MODE_NOT_EXIST;
}

void vmx_timer_copy(guest_cpu_handle_t gcpu_from, guest_cpu_handle_t gcpu_to)
{
	vmx_timer_t *vmx_timer_from, *vmx_timer_to;

	VMM_ASSERT_EX(gcpu_from, "gcpu_from is NULL\n");
	vmx_timer_from = find_vmx_timer(gcpu_from, FALSE);
	if (vmx_timer_from == NULL) // do nothing
	{
		print_warn("%s(), vmx_timer_from is NULL\n", __FUNCTION__);
		return;
	}

	VMM_ASSERT_EX(gcpu_to, "gcpu_to is NULL\n");
	vmx_timer_to = find_vmx_timer(gcpu_to, TRUE);// create if not exists
	vmx_timer_to->mode = vmx_timer_from->mode;
	vmx_timer_to->periodic = vmx_timer_from->periodic;
	if (vmx_timer_is_started(gcpu_from))
		vmx_timer_start(gcpu_to);
	else
		vmx_timer_stop(gcpu_to);
	vmx_timer_set_remaining(gcpu_to,
		vmx_timer_get_remaining(gcpu_from));
}
