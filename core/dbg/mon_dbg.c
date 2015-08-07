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

#include "guest_cpu.h"
#include "scheduler.h"
#include "common_libc.h"
#include "mon_dbg.h"
#include "guest_cpu_vmenter_event.h"
#include "vmcs_api.h"
#include "isr.h"
#include "mon_arch_defs.h"
#include "memory_dump.h"
#include "em64t_defs.h"
#include "mon_stack_api.h"
#include "fvs.h"
#include "mon_callback.h"

uint64_t g_debug_gpa = 0;
uint64_t g_initial_vmcs[MON_MAX_CPU_SUPPORTED] = { 0 };

isr_parameters_on_stack_t *g_exception_stack = NULL;
mon_gp_registers_t g_exception_gpr = { 0 };

extern boolean_t mon_copy_to_guest_phy_addr(guest_cpu_handle_t gcpu,
					    void *gpa,
					    uint32_t size,
					    void *hva);

int CLI_active(void)
{
#ifdef CLI_INCLUDE
	return (MON_MASK_CHECK(mask_cli)) ? 1 : 0;
#else
	return 0;
#endif
}

/* Dump debug info to guest buffer
 * To ensure only 1 signature string instance in memory dump
 * 1. build signature one char at a time
 * 2. no signature to serial output
 * 3. clear signature in the string buffer after used */
#define BUFFER_SIZE 256
void mon_deadloop_internal(uint32_t file_code, uint32_t line_num,
			   guest_cpu_handle_t gcpu)
{
	static uint32_t dump_started;
	char buffer[BUFFER_SIZE], err_msg[BUFFER_SIZE];
	uint64_t rsp, stack_base;
	uint32_t size;
	cpu_id_t cpu_id;
	exception_info_t header;

	/* skip dumping debug info if deadloop/assert happened before launch */
	if (g_debug_gpa == 0) {
		return;
	}

	cpu_id = hw_cpu_id();
	if (cpu_id >= MAX_CPUS) {
		return;
	}

	mon_sprintf_s(err_msg, 128,
		"CPU%d: %s: Error: Could not copy deadloop message"
		" back to guest\n", cpu_id, __FUNCTION__);

	/* send cpu id, file code, line number to serial port */
	mon_printf("%02d%04d%04d\n", cpu_id, file_code, line_num);

	/* must match format defined in file_line_info_t */
	size = mon_sprintf_s(buffer,
		BUFFER_SIZE,
		"%04d%04d",
		file_code,
		line_num);

	/* copy file code/line number to guest buffer at offset defined in
	 * deadloop_dump_t
	 * strlen(signature) + sizeof(cpu_id) + file_line[cpu] */
	if (!mon_copy_to_guest_phy_addr(gcpu,
		    (void *)(g_debug_gpa + 8 + 8 +
			     (cpu_id * size)), size,
		    (void *)buffer)) {
		MON_LOG(mask_mon, level_error, err_msg);
	}

	/* only copy signature, VERSION, cpu_id, exception info, vmcs to guest
	 * buffer once */
	if (hw_interlocked_compare_exchange(&dump_started, 0, 1) == 0) {
		size = mon_sprintf_s(buffer,
			BUFFER_SIZE,
			"%c%c%c%c%c%c%c%c%s%04d",
			DEADLOOP_SIGNATURE[0],
			DEADLOOP_SIGNATURE[1],
			DEADLOOP_SIGNATURE[2],
			DEADLOOP_SIGNATURE[3],
			DEADLOOP_SIGNATURE[4],
			DEADLOOP_SIGNATURE[5],
			DEADLOOP_SIGNATURE[6],
			DEADLOOP_SIGNATURE[7],
			VERSION,
			cpu_id);

		/* copy signature and cpu_id to guest buffer */
		if (!mon_copy_to_guest_phy_addr(gcpu,
			    (void *)(g_debug_gpa),
			    size, (void *)buffer)) {
			MON_LOG(mask_mon, level_error, err_msg);
		}

		/* clear buffer erasing the signature or setting no exception flag */
		mon_zeromem(buffer, sizeof(uint64_t));

		/* copy exception info to guest buffer */
		if (g_exception_stack != NULL) {
			mon_memcpy((void *)&header.exception_stack,
				g_exception_stack,
				sizeof(isr_parameters_on_stack_t));
			header.base_address =
				mon_startup_data.mon_memory_layout[mon_image].
				base_address;

			if (g_exception_stack->a.vector_id ==
			    IA32_EXCEPTION_VECTOR_PAGE_FAULT) {
				header.cr2 = hw_read_cr2();
			}

			/* copy exception info to guest buffer */
			if (!mon_copy_to_guest_phy_addr(gcpu,
				    (void *)(g_debug_gpa +
					     OFFSET_EXCEPTION),
				    sizeof(exception_info_t),
				    (void *)&header)) {
				MON_LOG(mask_mon, level_error, err_msg);
			}

			/* copy GPRs to guest buffer */
			if (!mon_copy_to_guest_phy_addr(gcpu,
				    (void *)(g_debug_gpa + OFFSET_GPR),
				    sizeof(mon_gp_registers_t),
				    (void *)&g_exception_gpr)) {
				MON_LOG(mask_mon, level_error, err_msg);
			}

			/* copy stack to guest buffer */
			rsp =
				isr_error_code_required((vector_id_t)g_exception_stack->
					a.vector_id) ? g_exception_stack->
				u.errcode_exception.sp :
				g_exception_stack->u.exception.sp;

			mon_stack_get_stack_pointer_for_cpu(cpu_id,
				&stack_base);

			size = sizeof(uint64_t) * STACK_TRACE_SIZE;
			if ((rsp + size) > stack_base) {
				size = (uint32_t)(stack_base - rsp);
			}

			if (!mon_copy_to_guest_phy_addr(gcpu,
				    (void *)(g_debug_gpa +
					     OFFSET_STACK), size,
				    (void *)rsp)) {
				MON_LOG(mask_mon, level_error, err_msg);
			}
		} else {
			/* Clear base image address indicating exception did not happen */
			if (!mon_copy_to_guest_phy_addr(gcpu,
				    (void *)(g_debug_gpa +
					     OFFSET_EXCEPTION),
				    sizeof(uint64_t), (void *)buffer)) {
				MON_LOG(mask_mon, level_error, err_msg);
			}
		}

		/* copy vmcs to guest buffer */
		vmcs_dump_all(gcpu);
	}
}

void mon_deadloop_dump(uint32_t file_code, uint32_t line_num)
{
#define DEFAULT_VIEW_HANDLE 0

	guest_cpu_handle_t gcpu;
	em64t_rflags_t rflags;
	ia32_vmx_vmcs_guest_interruptibility_t interruptibility;

	gcpu = mon_scheduler_current_gcpu();
	if (!gcpu) {
		MON_UP_BREAKPOINT();
	}

	report_mon_event(MON_EVENT_MON_ASSERT, (mon_identification_data_t)gcpu,
		(const guest_vcpu_t *)mon_guest_vcpu(gcpu), NULL);

	/* send debug info to serial port and guest buffer */
	mon_deadloop_internal(file_code, line_num, gcpu);

	/* clear interrupt flag */
	rflags.uint64 = gcpu_get_gp_reg(gcpu, IA32_REG_RFLAGS);
	rflags.bits.ifl = 0;
	gcpu_set_gp_reg(gcpu, IA32_REG_RFLAGS, rflags.uint64);

	interruptibility.uint32 = gcpu_get_interruptibility_state(gcpu);
	interruptibility.bits.block_next_instruction = 0;
	gcpu_set_interruptibility_state(gcpu, interruptibility.uint32);

	/* generate BSOD */
	mon_gcpu_inject_gp0(gcpu);
	gcpu_resume(gcpu);
}

/*
 * Generic debug helper function
 *
 * returns TRUE */


boolean_t deadloop_helper(const char *assert_condition, const char *func_name,
			  const char *file_name, uint32_t line_num,
			  uint32_t access_level)
{
	if (!assert_condition) {
		mon_printf("Deadloop in %s() - %s:%d\n",
			func_name, file_name, line_num);
	} else {
		mon_printf("MON assert (%s) failed\n\t in %s() at %s:%d\n",
			assert_condition, func_name, file_name, line_num);
	}

	return TRUE;
}

