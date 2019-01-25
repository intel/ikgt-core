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

#include "vmm_base.h"
#include "vmm_objects.h"
#include "vmexit.h"
#include "scheduler.h"
#include "guest.h"
#include "heap.h"
#include "gcpu.h"
#include "vmcs.h"
#include "vmx_cap.h"
#include "event.h"
#include "host_cpu.h"

#include "lib/lapic_ipi.h"
#include "lib/util.h"

#ifdef MODULE_LAPIC_ID
#include "modules/lapic_id.h"
#endif
#include "modules/ipc.h"

/* IPC_MESSAGE_NUM can be set to 2^N under 32,
** and don't need to change any other codes
** the reason why it cannot be set to ANY value is because
** the last_valid might overflow */
#define IPC_MESSAGE_NUM 32

typedef struct {
	ipc_func_t func;
	void* arg;
} ipc_message_t;

typedef struct {
	ipc_message_t message[IPC_MESSAGE_NUM];
	volatile uint32_t last_valid;
	volatile uint32_t request[MAX_CPU_NUM]; // for each host cpu. 1 bit means 1 message
						// set/checked by sender, cleared by receiver
	uint32_t pad[(MAX_CPU_NUM+1)%2];
} ipc_data_t;

static ipc_data_t g_ipc_data;

static void vmexit_nmi(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs = gcpu->vmcs;
	vmx_exit_interrupt_info_t vmexit_exception_info;

	vmexit_exception_info.uint32 = (uint32_t)vmcs_read(vmcs,
		VMCS_EXIT_INT_INFO);

	switch (vmexit_exception_info.bits.interrupt_type) {
		case VECTOR_TYPE_HW_EXCEPTION:
		case VECTOR_TYPE_SW_EXCEPTION:
			// keep this code which can be used for debugging guest double fault later
			print_trace("falut (%d) trapped in guest\n",
				vmexit_exception_info.bits.vector);
			print_trace("VMCS_EXIT_INT_INFO = 0x%x\n",
				vmexit_exception_info.uint32);
			print_trace("VMCS_EXIT_QUAL = 0x%llx\n",
				vmcs_read(vmcs, VMCS_EXIT_QUAL));
			print_trace("VMCS_EXIT_INT_ERR_CODE = 0x%llx\n",
				vmcs_read(vmcs, VMCS_EXIT_INT_ERR_CODE));
			VMM_DEADLOOP();
			break;
		case VECTOR_TYPE_NMI:
			host_cpu_inc_pending_nmi();
			asm_perform_iret();
			print_trace("hcpu%d, %s(): nmi=%d\n", host_cpu_id(),
				__FUNCTION__, host_cpu_get_pending_nmi());
			break;

		default:
			print_panic(
				"%s(): Unsupported interrupt/exception type: %d", __FUNCTION__,
				vmexit_exception_info.bits.interrupt_type);
			VMM_DEADLOOP();
			break;
	}

}

// EVENT_SIPI_VMEXIT,
static void ipc_check_messages(UNUSED guest_cpu_handle_t gcpu, void *pv)
{
	uint16_t hcpu_id = host_cpu_id();
	event_sipi_vmexit_t *sipi_vmexit_param;

	D(VMM_ASSERT(pv));

	sipi_vmexit_param = (event_sipi_vmexit_t *)pv;

	if (sipi_vmexit_param->vector == 0xff)
	{
		if (g_ipc_data.request[hcpu_id] == 0)
		{
			print_warn("%s(): no request in SIPI with vector 0xff\n", __FUNCTION__);
		}
		else
		{
			host_cpu_inc_pending_nmi(); // add pending nmi, which will be processed before resume
			print_trace("hcpu%d, %s(): nmi=%d\n", host_cpu_id(),
				__FUNCTION__, host_cpu_get_pending_nmi());
		}
		sipi_vmexit_param->handled = TRUE;
	} else
		sipi_vmexit_param->handled = FALSE;
}

// it should work for most cases that only 1 message (pointed by last_valid) is pending
static uint32_t ipc_process_messages_quick(guest_cpu_handle_t gcpu, uint16_t hcpu_id)
{
	uint32_t request = g_ipc_data.request[hcpu_id]; // get a copy
	uint32_t index = g_ipc_data.last_valid; // get a copy
	uint32_t mask;
	ipc_func_t func;
	void* arg;

	index &= (IPC_MESSAGE_NUM - 1);
	mask = 1U << index;

	if (request == mask)
	{
		/* no need to assert func, it is not set to NULL after use. it will never be NULL
		** after first use. */
		func = g_ipc_data.message[index].func; // get a copy
		arg = g_ipc_data.message[index].arg; // get a copy
		asm_lock_and32(&(g_ipc_data.request[hcpu_id]), ~mask);
		func(gcpu, arg);
		return 1;
	}
	return 0;
}

static uint32_t ipc_process_messages_all(guest_cpu_handle_t gcpu, uint16_t hcpu_id)
{
	uint32_t request = g_ipc_data.request[hcpu_id]; // get a copy
	uint32_t index = g_ipc_data.last_valid; // get a copy
	uint32_t i, count;
	ipc_func_t func;
	void* arg;

	index &= (IPC_MESSAGE_NUM - 1);
	count = 0;
	// go through (index+1) .. IPC_MESSAGE_NUM, 0 .. index
	for (i=0; i < IPC_MESSAGE_NUM; i++)
	{
		index++; // start from index+1
		index &= (IPC_MESSAGE_NUM - 1);
		if (request & ((uint32_t)1<<index))
		{
			/* no need to assert func, it is not set to NULL after use. it will never be NULL
			** after first use. */
			func = g_ipc_data.message[index].func; // get a copy
			arg = g_ipc_data.message[index].arg; // get a copy
			asm_lock_and32(&(g_ipc_data.request[hcpu_id]), ~((uint32_t)1<<index));
			func(gcpu, arg);
			count++;
		}
	}
	return count;
}

// EVENT_PROCESS_NMI_BEFORE_RESUME,
static void ipc_process_messages(guest_cpu_handle_t gcpu, void *pv)
{
	uint32_t *total_count = (uint32_t *)pv;
	uint32_t count;
	uint16_t hcpu_id = host_cpu_id();

	D(VMM_ASSERT(pv));

	while (1)
	{
		if (g_ipc_data.request[hcpu_id] == 0)
			return;
		count = ipc_process_messages_quick(gcpu, hcpu_id);
		if (!count)
			count = ipc_process_messages_all(gcpu, hcpu_id);
		D(VMM_ASSERT(count));
		*total_count += count;
	}
	// never reach here
}

static void ipc_gcpu_init(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	uint32_t pin_base;
	uint32_t pin_may1;
	vmcs_obj_t vmcs;

	vmcs = gcpu->vmcs;
	pin_base = (uint32_t)vmcs_read(vmcs, VMCS_PIN_CTRL);
	pin_may1 = get_pinctl_cap(NULL);
	pin_base |= PIN_NMI_EXIT;
	if (pin_may1 & PIN_VIRTUAL_NMI)
		pin_base |= PIN_VIRTUAL_NMI;
	else
		print_warn("%s(): virtual_nmi is not supported\n", __FUNCTION__);
	vmcs_write(vmcs, VMCS_PIN_CTRL, pin_base);
}

void ipc_init(void)
{
	uint32_t pin_may1;
	pin_may1 = get_pinctl_cap(NULL);
	VMM_ASSERT_EX((pin_may1 & PIN_NMI_EXIT),
		"nmi exit is not supported\n");

	vmexit_install_handler(vmexit_nmi, REASON_00_NMI_EXCEPTION);
	event_register(EVENT_GCPU_MODULE_INIT, ipc_gcpu_init);
	event_register(EVENT_SIPI_VMEXIT, ipc_check_messages);
	event_register(EVENT_PROCESS_NMI_BEFORE_RESUME, ipc_process_messages);
}

static uint32_t ipc_setup_func(ipc_func_t func, void* arg)
{
	uint32_t index, i;
	uint32_t mask;

	index = lock_inc32(&(g_ipc_data.last_valid));
	index &= (IPC_MESSAGE_NUM - 1); // just need the index within IPC_MESSAGE_NUM
	mask = 1U << index;

	for (i=0; i<host_cpu_num; i++)
	{
		// the slot is available
		VMM_ASSERT_EX(((g_ipc_data.request[i] & mask) == 0),
			"too many ipc calls\n");
	}

	g_ipc_data.message[index].func = func;
	g_ipc_data.message[index].arg = arg;

	return mask;
}

void ipc_exec_on_all_other_cpus(ipc_func_t func, void* arg)
{
	uint32_t mask, i;
	uint16_t this_hcpu_id = host_cpu_id();

	D(VMM_ASSERT_EX(func, "%s: func is NULL\n", __FUNCTION__));

	mask = ipc_setup_func(func, arg);

	for (i=0; i<host_cpu_num; i++)
	{
		/* request will be SET by sender only. since the index is allocated with lock,
		** there's no chance for the bit (asserted 0 before) to be 1
		** if the index overflows and equal to this index again, it will fail in the assert 0 above.
		** so, there's no need to assert 0 (by checking return value of asm_lock_or32) again
		*/
		if (i != this_hcpu_id)
			asm_lock_or32(&(g_ipc_data.request[i]), mask);
	}
	/* when target cpu is in guest mode and guest mode is wait-for-sipi, NMI will be dropped and
	** no VMExit will occur. it only accepts SIPI. when target cpu is in other status, SIPI will be dropped
	** so, here, both SIPI and NMI will be used to make sure each target cpu will and will only receive 1 IPI
	** NMI should be sent first.
	** if send SIPI first, NMI might be deliverd through host IDT, which kicks target cpu twice
	** by sending NMI first, there's no such concern */
	VMM_ASSERT_EX(broadcast_nmi(), "%s(): broadcast nmi failed\n", __FUNCTION__);
	VMM_ASSERT_EX(broadcast_startup(0xff), "%s(): broadcast startup failed\n", __FUNCTION__); // use 0xff as an indicator
}

#ifdef MODULE_LAPIC_ID
void ipc_exec_on_host_cpu(uint16_t hcpu_id, ipc_func_t func, void* arg)
{
	uint32_t mask;
	uint16_t this_hcpu_id = host_cpu_id();

	D(VMM_ASSERT_EX(func, "%s: func is NULL\n", __FUNCTION__));
	VMM_ASSERT_EX((this_hcpu_id != hcpu_id),
		"ipc is called on the same hcpu id\n");

	mask = ipc_setup_func(func, arg);
	asm_lock_or32(&(g_ipc_data.request[hcpu_id]), mask);
	/*IA32 spec, volume3, chapter 10 APIC->Local APIC->Local APIC ID
	 *Local APIC ID usually not be changed */
	VMM_ASSERT_EX(send_nmi(get_lapic_id(hcpu_id)), "%s(): send_nmi to hcpu%d failed\n", __FUNCTION__, hcpu_id);
	VMM_ASSERT_EX(send_startup(get_lapic_id(hcpu_id), 0xff), "%s():send startup failed to hcpu%d\n", __FUNCTION__, hcpu_id); // use 0xff as an indicator
}
#endif
