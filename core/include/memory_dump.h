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

#ifndef _MEMORY_DUMP_H_
#define _MEMORY_DUMP_H_

#define OFFSET_EXCEPTION    0x02C0
#define OFFSET_GPR          0x0300
#define OFFSET_STACK        0x0400
#define OFFSET_VMCS         0x0800

/* Number of stack trace entries copied to debug buffer
 * The buffer has space to save up to 128 entries */
#define STACK_TRACE_SIZE    48

/* Space in buffer reserved for VMCS */
#define VMCS_SIZE           2048

/* version log
 * 1.0 - 1st release
 * 1.5 - add complete exception info, stack trace to buffer
 * - change to have data saved in binary format
 * 1.7 - add support for 80 CPUs */
#define VERSION           "01.7"

typedef struct {
	char	file_code[4];
	char	line_number[4];
} file_line_info_t;

typedef struct {
	char			signature[8];
	char			flags[4];               /* debug buffer format flag */
	char			cpu_id[4];              /* cpu of the first deadloop/assert */
	file_line_info_t	file_line[MAX_CPUS];    /* support up to 80 CPUs */
} debug_info_t;

typedef struct {
	uint64_t			base_address;   /* Mon image base address */
	address_t			cr2;            /* page fault address */
	isr_parameters_on_stack_t	exception_stack;
} exception_info_t;

typedef struct {
	debug_info_t		header;
	exception_info_t	exception;
} deadloop_dump_t;


/* only the existing vmcs fields are copied to guest buffer */
typedef struct {
	uint16_t	index;          /* index to g_field_data */
	uint64_t	value;          /* vmcs value */
} PACKED vmcs_entry_t;

/* the vmcs fields are arranged in the order of control, guest, host area */
typedef struct {
	uint16_t	count;          /* number of entries copied to guest */
	vmcs_entry_t	entries;        /* list of entries */
} PACKED vmcs_group_t;

/* this is the layout of the 4K guest buffer
 * the actual starting offsets are defined by the symbolic constants */
typedef struct {
	deadloop_dump_t		deadloop_info;
	mon_gp_registers_t	gp_regs;
	uint64_t		stack[STACK_TRACE_SIZE];
	vmcs_group_t		vmcs_groups; /* list of groups */
} PACKED memory_dump_t;


#define DEADLOOP_SIGNATURE      "XMONASST"

extern uint64_t g_debug_gpa;
extern uint64_t g_initial_vmcs[MON_MAX_CPU_SUPPORTED];
extern uint64_t ept_compute_eptp(guest_handle_t guest,
				 uint64_t ept_root_table_hpa,
				 uint32_t gaw);
extern void ept_get_default_ept(guest_handle_t guest,
				uint64_t *ept_root_table_hpa,
				uint32_t *ept_gaw);
extern boolean_t vmcs_sw_shadow_disable[];

void mon_deadloop_internal(uint32_t file_code,
			   uint32_t line_num,
			   guest_cpu_handle_t gcpu);

#endif                          /* _MEMORY_DUMP_H_ */
