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

#ifndef _GDT_H_
#define _GDT_H_

#define TSS_ENTRY_SIZE (sizeof(address_t) * 2)
#define TSS_ENTRY_OFFSET(__cpuid)   \
	(TSS_FIRST_GDT_ENTRY_OFFSET + (__cpuid) * TSS_ENTRY_SIZE)

enum {
	NULL_GDT_ENTRY_OFFSET = 0,
	DATA32_GDT_ENTRY_OFFSET = 8,
	CODE32_GDT_ENTRY_OFFSET = 0x10,
	CODE64_GDT_ENTRY_OFFSET = 0x18,
	TSS_FIRST_GDT_ENTRY_OFFSET = 0x20,
	/* this value is used in assembler. */
	CPU_LOCATOR_GDT_ENTRY_OFFSET = TSS_FIRST_GDT_ENTRY_OFFSET
};

void hw_gdt_setup(cpu_id_t number_of_cpus);
void hw_gdt_load(cpu_id_t cpu_id);
void hw_gdt_set_ist_pointer(cpu_id_t cpu_id, uint8_t ist_no, address_t address);
mon_status_t hw_gdt_parse_entry(IN uint8_t *p_gdt,
				IN uint16_t selector,
				OUT address_t *p_base,
				OUT uint32_t *p_limit,
				OUT uint32_t *p_attributes);

#endif /*_GDT_H_ */
