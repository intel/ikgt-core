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

#define GDT_NULL_ENTRY_OFFSET 0x00
#define GDT_DATA_OFFSET 0x08
#define GDT_CODE64_OFFSET 0x10
#define GDT_TSS64_OFFSET 0x18

void gdt_setup(void);
void gdt_load(uint16_t cpu_id);
void set_tss_ist(uint16_t cpu_id, uint8_t ist_no, uint64_t address);
uint64_t get_tss_base(uint16_t cpu_id);
uint16_t calculate_cpu_id(uint16_t tr);

#endif /*_GDT_H_ */
