/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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
