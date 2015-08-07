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

#include "vmx_trace.h"
#include "trace.h"
#include "common_libc.h"
#include "vmcs_api.h"
#include "scheduler.h"
#include "hw_utils.h"
#include "mon_dbg.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMX_TRACE_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMX_TRACE_C, __condition)

static mon_trace_state_t mon_trace_state = MON_TRACE_DISABLED;

boolean_t mon_trace_init(uint32_t max_num_guests, uint32_t max_num_guest_cpus)
{
	static boolean_t called = FALSE;

	if (!called) {
		called = trace_init(max_num_guests, max_num_guest_cpus);
	}

	return called;
}

void mon_trace_state_set(mon_trace_state_t state)
{
	switch (state) {
	case MON_TRACE_DISABLED:
	case MON_TRACE_ENABLED_RECYCLED:
	case MON_TRACE_ENABLED_NON_RECYCLED:
		trace_set_recyclable(MON_TRACE_ENABLED_RECYCLED == state);
		mon_trace_state = state;
		break;
	default:
		break;
	}
}

static size_t
mon_trace_print_string(const char *format, va_list marker, char *string)
{
	char formatted_string[MAX_STRING_LENGTH];
	size_t max_index, length;

	if (MON_TRACE_DISABLED == mon_trace_state) {
		return 0;
	}

	max_index = mon_strlen(format);
	if (max_index >= MAX_STRING_LENGTH) {
		MON_DEADLOOP();
	}

	mon_strcpy_s(formatted_string, MAX_STRING_LENGTH, format);
	formatted_string[max_index] = '\0';

	length =
		mon_vsprintf_s(string,
			MAX_STRING_LENGTH,
			formatted_string,
			marker);
	if (length > MAX_STRING_LENGTH) {
		MON_DEADLOOP();
	}

	return length;
}

boolean_t mon_trace_buffer(guest_cpu_handle_t guest_cpu,
			   uint8_t buffer_index, const char *format, ...)
{
	va_list marker;
	trace_record_data_t data;
	vmcs_object_t *vmcs_obj = 0;
	const virtual_cpu_id_t *virtual_cpu_id = 0;
	guest_id_t guest_id = 0;
	cpu_id_t gcpu_id = 0;

	if (MON_TRACE_DISABLED == mon_trace_state) {
		return FALSE;
	}

	if (guest_cpu == NULL) {
		MON_LOG(mask_anonymous,
			level_trace,
			"%a %d: Invalid parameter(s): guest_cpu 0x%x\n",
			__FUNCTION__,
			__LINE__,
			guest_cpu);
		MON_DEADLOOP();
	}

	mon_memset(&data, 0, sizeof(data));

	va_start(marker, format);
	mon_trace_print_string(format, marker, data.string);
	va_end(marker);

	vmcs_obj = mon_gcpu_get_vmcs(guest_cpu);
	virtual_cpu_id = mon_guest_vcpu(guest_cpu);

	if (vmcs_obj != NULL) {
		data.exit_reason =
			mon_vmcs_read(vmcs_obj, VMCS_EXIT_INFO_REASON);
		data.guest_eip = mon_vmcs_read(vmcs_obj, VMCS_GUEST_RIP);
	}
	data.tsc = (buffer_index == (MAX_TRACE_BUFFERS - 1)) ? hw_rdtsc() : 0;

	if (virtual_cpu_id != NULL) {
		guest_id = virtual_cpu_id->guest_id;
		gcpu_id = virtual_cpu_id->guest_cpu_id;
	}

	return trace_add_record(guest_id, gcpu_id, buffer_index, &data);
}

boolean_t mon_trace(guest_cpu_handle_t guest_cpu, const char *format, ...)
{
	va_list marker;
	trace_record_data_t data;
	vmcs_object_t *vmcs_obj = 0;
	const virtual_cpu_id_t *virtual_cpu_id = 0;
	guest_id_t guest_id = 0;
	cpu_id_t gcpu_id = 0;

	if (MON_TRACE_DISABLED == mon_trace_state) {
		return FALSE;
	}

	if (guest_cpu == NULL) {
		MON_LOG(mask_anonymous,
			level_trace,
			"%a %d: Invalid parameter(s): guest_cpu 0x%x\n",
			__FUNCTION__,
			__LINE__,
			guest_cpu);
		MON_DEADLOOP();
	}

	mon_memset(&data, 0, sizeof(data));

	va_start(marker, format);
	mon_trace_print_string(format, marker, data.string);
	va_end(marker);

	vmcs_obj = mon_gcpu_get_vmcs(guest_cpu);
	virtual_cpu_id = mon_guest_vcpu(guest_cpu);

	if (vmcs_obj != NULL) {
		data.exit_reason =
			mon_vmcs_read(vmcs_obj, VMCS_EXIT_INFO_REASON);
		data.guest_eip = mon_vmcs_read(vmcs_obj, VMCS_GUEST_RIP);
		data.tsc = 0;
	}

	if (virtual_cpu_id != NULL) {
		guest_id = virtual_cpu_id->guest_id;
		gcpu_id = virtual_cpu_id->guest_cpu_id;
	}
	return trace_add_record(guest_id, gcpu_id, 0, &data);
}

boolean_t mon_trace_print_all(uint32_t guest_num, char *guest_names[])
{
	trace_record_data_t record_data;
	uint32_t vm_index = 0, cpu_index = 0, buffer_index = 0, record_index =
		0;
	int cnt = 0;

	if (MON_TRACE_DISABLED == mon_trace_state) {
		return FALSE;
	}
	trace_lock();

	MON_LOG(mask_anonymous, level_trace, "\nTrace Events\n");

	while (trace_remove_oldest_record(&vm_index,
		       &cpu_index,
		       &buffer_index,
		       &record_index,
		       &record_data)) {
		char *vm_name;
		char buffer[5];

		if (0 == cnt++ % 0x1F) {
			MON_LOG(mask_anonymous,
				level_trace,
				"Buf   Index    TSC           | VM CPU  Exit Guest"
				"       EIP    | Message\n"
				"-----------------------------+--------------------"
				"-------------+---------------------\n");
		}

		if (vm_index < guest_num) {
			vm_name = guest_names[vm_index];
		} else {
			mon_sprintf_s(buffer, sizeof(buffer), "%4d", vm_index);
			vm_name = buffer;
		}

		MON_LOG(mask_anonymous,
			level_trace,
			"%2d %8d %016lx |%4s %1d  %4d  %018P | %s",
			buffer_index,
			record_index,
			record_data.tsc,
			vm_name,
			cpu_index,
			record_data.exit_reason,
			record_data.guest_eip,
			record_data.string);
	}

	trace_unlock();
	return TRUE;
}
