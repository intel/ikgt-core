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
#include "vmm_arch.h"
#include "heap.h"
#include "vmm_util.h"
#include "gdt.h"
#include "dbg.h"
#include "host_cpu.h"
#include <lib/util.h>

typedef struct {
	uint32_t reserved_1;
	uint64_t rsp[3];
	uint64_t reserved_2;
	uint64_t ist[7];
	uint64_t reserved_3;
	uint16_t reserved_4;
	uint16_t io_bitmap_address; /* offset inside TSS */
	uint8_t io_bitmap_last_byte;
	uint8_t pad[7];
} PACKED tss64_t;

typedef struct {
	struct {
		uint64_t limit_00_15:16;
		uint64_t base_00_23:24;
		uint64_t attr_00_07:8; // type:4 mbz:1 dpl:2 present:1
		uint64_t limit_16_19:4;
		uint64_t attr_08_11:4; // avl:1 mbz:2 granularity:1
		uint64_t base_24_31:8;
	} lo_bits;
	struct {
		uint64_t base_32_63:32;
		uint64_t reserved:32;
	} hi_bits;
} tss64_descripor_t;

#ifdef STACK_PROTECTOR
struct stack_cookie{
	/* GCC hardcodes the stack cookie offset as 0x28 on x86-64 */
	uint8_t padding[40];
	uint64_t cookie;
};

static struct stack_cookie g_cookie;
#endif

#define TSS_ENTRY_SIZE (sizeof(tss64_descripor_t))
#define TSS_ENTRY_OFFSET(__cpuid)   \
	(GDT_TSS64_OFFSET + (__cpuid) * TSS_ENTRY_SIZE)

static uint8_t gdt[TSS_ENTRY_OFFSET(MAX_CPU_NUM)];
static tss64_t p_tss[MAX_CPU_NUM];
static gdtr64_t gdtr;

uint16_t calculate_cpu_id(uint16_t tr)
{
	return (tr - GDT_TSS64_OFFSET) / TSS_ENTRY_SIZE;
}

uint64_t get_tss_base(uint16_t cpu_id)
{
	VMM_ASSERT_EX((cpu_id < host_cpu_num),
		"cpu_id(%u) is invalid\n", cpu_id);
	return (uint64_t)&p_tss[cpu_id];
}

static void setup_null_seg(void)
{
	uint64_t *p_entry =
		(uint64_t *)&gdt[GDT_NULL_ENTRY_OFFSET];

	*p_entry = 0ULL;
}

static void setup_data_seg(void)
{
	uint64_t *p_data =
		(uint64_t *)&gdt[GDT_DATA_OFFSET];

	/*
	low 32bits data segement descriptor format
		limit_15_00 = 0xFFFF
		base_address_15_00 = 0

	high 32bits data segement descriptor format
		base_address_23_16 = 0
		accessed = 0
		writable = 1
		expansion_direction = 0
		mbz_11 = 0
		mbo_12 = 1
		dpl = 0
		present = 1
		limit_19_16 = 0xF
		avl = 0
		mbz_21 = 0
		big = 1
		granularity = 1
		base_address_31_24 = 0
	*/
	*p_data = 0xCF92000000FFFF;
}

static void setup_code64_seg(void)
{
	uint64_t *p_code64 =
		(uint64_t *)&gdt[GDT_CODE64_OFFSET];

	/*
	low 32-bit word is reserved, configure only high word
		reserved_00_07:
		accessed = 0
		readable = 1
		conforming = 1
		mbo_11 = 1
		mbo_12 = 1
		dpl = 0
		present = 1
		reserved_19_16:
		avl:  // Available to software
		long_mode = 1
		default_size = 0
		granularity = 0
		reserved_31_24:
	*/
	*p_code64 = 0x209E0000000000;
}

static void setup_tss64_seg(uint16_t cpu_id)
{
	tss64_descripor_t *p_tss64 =
		(tss64_descripor_t *)&gdt[TSS_ENTRY_OFFSET(cpu_id)];
	uint64_t base_address = get_tss_base(cpu_id);
	uint32_t segment_limit =
		OFFSET_OF(tss64_t, io_bitmap_last_byte);

	p_tss64->lo_bits.limit_00_15 = segment_limit & 0xFFFF;
	p_tss64->lo_bits.base_00_23 = base_address & 0xFFFFFF;
	/* type = 9, dpl = 0, present = 1 */
	p_tss64->lo_bits.attr_00_07 = 0x89;
	p_tss64->lo_bits.limit_16_19 = (segment_limit >> 16) & 0xF;
	/* avl = 0, granularity = 0 */
	p_tss64->lo_bits.attr_08_11 = 0;
	p_tss64->lo_bits.base_24_31 = (base_address >> 24) & 0xFF;

	p_tss64->hi_bits.base_32_63 = base_address >> 32;
	p_tss64->hi_bits.reserved = 0;

	/* that means no IO ports are blocked */
	p_tss[cpu_id].io_bitmap_address =
		OFFSET_OF(tss64_t, io_bitmap_last_byte);
	p_tss[cpu_id].io_bitmap_last_byte = 0xFF;
}

/*-------------------------------------------------------*
*  FUNCTION     : gdt_setup()
*  PURPOSE      : Setup GDT for all CPUs. Including entries for:
*               : 64-bit code segment
*               : 32-bit data segment (in compatibility mode,
*                 for both data and stack)
*               : one 64-bit for FS, which limit is used like index CPU ID
*  RETURNS      : void
*-------------------------------------------------------*/
void gdt_setup(void)
{
	uint16_t cpu_id;

	gdtr.limit = TSS_ENTRY_OFFSET(MAX_CPU_NUM) - 1;
	gdtr.base = (uint64_t)gdt;

	setup_null_seg();
	setup_data_seg();
	setup_code64_seg();

#ifdef STACK_PROTECTOR
	/* inherit the original stack cookie value */
	g_cookie.cookie = get_stack_cookie_value();
#endif

	for (cpu_id = 0; cpu_id < host_cpu_num; ++cpu_id)
		setup_tss64_seg(cpu_id);
}

/*-------------------------------------------------------*
*  FUNCTION     : gdt_load()
*  PURPOSE      : Load GDT on given CPU
*  ARGUMENTS    : IN uint16_t cpu_id
*  RETURNS      : void
*-------------------------------------------------------*/
void gdt_load(IN uint16_t cpu_id)
{

	VMM_ASSERT_EX((cpu_id < host_cpu_num),
		"cpu_id(%u) is invalid\n", cpu_id);

	asm_lgdt(&gdtr);

	asm_set_ds(GDT_DATA_OFFSET);
	asm_set_ss(GDT_DATA_OFFSET);
	hw_write_cs(GDT_CODE64_OFFSET);

	asm_ltr(TSS_ENTRY_OFFSET(cpu_id));

	asm_lldt(0);

	asm_set_es(0);
	asm_set_fs(0);
#ifdef STACK_PROTECTOR
	/* configure the new fs base */
	asm_wrmsr(MSR_FS_BASE, (uint64_t)&g_cookie);
#endif
	asm_set_gs(0);
}

/*-------------------------------------------------------*
*  FUNCTION     : set_tss_ist()
*  PURPOSE      : Assign address to specified IST
*  ARGUMENTS    : uint16_t cpu_id
*               : uint8_t ist_no - in range [0..7]
*               : uint64_t address - of specified Interrupt Stack
*  RETURNS      : void
*-------------------------------------------------------*/
void set_tss_ist(uint16_t cpu_id, uint8_t ist_no, uint64_t address)
{
	VMM_ASSERT_EX((ist_no < 7), "ist_no(%u) is invalid\n", ist_no);
	VMM_ASSERT_EX((cpu_id < host_cpu_num), "cpu_id(%u) is invalid\n", cpu_id);
	VMM_ASSERT_EX((NULL != p_tss), "p_tss is NULL\n");
	p_tss[cpu_id].ist[ist_no] = address;
}
