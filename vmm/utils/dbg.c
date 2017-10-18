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

#include "gcpu.h"
#include "gcpu_switch.h"
#include "scheduler.h"
#include "dbg.h"
#include "gcpu_inject_event.h"
#include "vmcs.h"
#include "stack.h"
#include "guest.h"
#include "host_cpu.h"
#include "lock.h"
#include "event.h"

#ifdef LIB_PRINT
vmm_lock_t vmm_print_lock;
#endif

#define RESET_IO_PORT         0xCF9

#ifndef DEBUG
static void reset_platform(void)
{

	/* see io-controller-hub-10-family-datasheet
	 * chapter 13 LPC Interface Bridge Registers
	 * 13.7 Processor Interface Registers
	 * 13.7.5 Reset Control Register
	 * bit1 System Reset, bit2 Reset CPU, bit3 Full Reset, others reserved.
	 */
	/* bit2(Reset CPU) needs a 0->1 switch to reset */
	asm_out8(RESET_IO_PORT, 0);
	/* perform a hard reset. system reset(bit[1]=1), reset CPU(bit[2]=1) */
	asm_out8(RESET_IO_PORT, 0x6);

}
#endif

/* flag used to prevent double deadloop. */
static uint32_t in_deadloop[MAX_CPU_NUM];

void clear_deadloop_flag(void)
{
	uint16_t cpu_id = host_cpu_id();
	in_deadloop[cpu_id] = 0;
}

static void (*final_deadloop_handler)(void);

void register_final_deadloop_handler(void (*func)(void))
{
	D(VMM_ASSERT_EX(func, "final deadloop handle is NULL\n"));
	if (final_deadloop_handler) {
		print_warn(" final deadloop handle has exsit\n");
	}
	final_deadloop_handler = func;
}

void vmm_deadloop(const char *file_name, uint32_t line_num)
{
	uint16_t host_cpu;
	guest_cpu_handle_t gcpu;
	event_deadloop_t event_deadloop;
	uint32_t guest_state;
	uint32_t vmexit_reason;

	print_init(FALSE);

	printf("Deadloop/Assert in %s:%d\n", file_name, line_num);

	//get host cpu id form calculate_cpu_id().host_cpu_id()has assert in debug build.
	host_cpu = calculate_cpu_id(asm_str());
	if (!(host_cpu < host_cpu_num)) {
		printf("Deadloop: host_cpu_id(%d) should less than host_cpu_num(%d)\n", host_cpu, host_cpu_num);
		goto final_handler;
	}
	printf("Deadloop: host_cpu_id = %d\n", host_cpu);

	if (asm_lock_cmpxchg32(&in_deadloop[host_cpu], 1, 0) != 0) {
		printf("Deadloop: it is the double deadloop\n");
		goto final;
	}

	gcpu = get_current_gcpu();

	if (!gcpu) {
		printf("Deadloop: gcpu is NULL\n");
		goto final_handler;
	}
	printf("Deadloop: gcpu_id = %d\n", gcpu->id);

	if (!gcpu->guest) {
		printf("Deadloop: gcpu_guest is NULL\n");
		goto final_handler;
	}

	if (!gcpu->vmcs) {
		printf("Deadloop: vmcs is NULL\n");
		goto final_handler;
	}
	printf("Deadloop: guest_id = %d\n", gcpu->guest->id);

#ifdef DEBUG
	vmcs_print_all(gcpu->vmcs);
#endif

	if (gcpu->guest->id != 0) {
		printf("Deadloop: it is not guest 0\n");
		goto final_handler;
	}

	if (gcpu->is_vmentry_fail)
	{
		printf("Deadloop: vmentry fail.\n");
		goto final_handler;
	}

	vmexit_reason = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_REASON);
	if (vmexit_reason == REASON_02_TRIPLE_FAULT ||
			vmexit_reason == REASON_33_ENTRY_FAIL_GUEST ||
			vmexit_reason == REASON_34_ENTRY_FAIL_MSR ||
			vmexit_reason == REASON_41_ENTRY_FAIL_MC) {
		printf("irrecoverable vmexit reason:%d\n", vmexit_reason);
		goto final_handler;
	}

	guest_state = (uint32_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_ACTIVITY_STATE);
	if (guest_state != ACTIVITY_STATE_ACTIVE) {
		printf("guest state(%d) is not active\n", guest_state);
		goto final_handler;
	}

	event_deadloop.file_name = (const char *)file_name;
	event_deadloop.line_num = line_num;
	event_raise(gcpu, EVENT_DEADLOOP, (void *)&event_deadloop);

	/* inject #GP to guest 0 */
	gcpu_inject_gp0(gcpu);
	gcpu_resume(gcpu);

final_handler:
	if (final_deadloop_handler != NULL)
		final_deadloop_handler();

final:
#ifndef DEBUG
	reset_platform();
#endif
	__STOP_HERE__;
	//printf("BUG: should never see this log after deadloop\n");
}
