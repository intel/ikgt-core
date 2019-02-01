/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "vmm_asm.h"
#include "vmm_arch.h"

#include "lib/mp_init.h"
#include "lib/util.h"
#include "lib/lapic_ipi.h"

#define PTCH 0x00

#ifdef BREAK_AT_FIRST_COMMAND
#define REAL_MODE_CODE_START 2
#else
#define REAL_MODE_CODE_START 0
#endif

#define REAL_MODE_STARTUP				( 1 + REAL_MODE_CODE_START)
#define GDTR_OFFSET_IN_CODE				( 7 + REAL_MODE_CODE_START)
#define CPU_STARTUP_DATA				(22 + REAL_MODE_CODE_START)
#define CPU_STARTUP64					(28 + REAL_MODE_CODE_START)
#define CPU_STARTUP32					(34 + REAL_MODE_CODE_START)

#define GDTR_OFFSET_IN_PAGE				ALIGN_F(sizeof(real_mode_code), 0x8)
#define GDT_OFFSET_IN_PAGE				(GDTR_OFFSET_IN_PAGE + 8)

typedef struct {	                /* Index */
	gdtr64_t host_gdtr;             /* 00 */
	uint16_t cs;                    /* 10 */
	uint16_t ds;                    /* 12 */
	uint16_t ss;                    /* 14 */
	uint32_t host_cr3;              /* 16 */
	uint32_t cpu_num;               /* 20 */
	uint64_t pat;                   /* 24 */
	uint64_t fs_base;               /* 32: for stack protect */
	uint32_t esp[MAX_CPU_NUM+1];    /* 40 */
#if (((MAX_CPU_NUM+1)%2) != 0)
	uint32_t padding;
#endif
} cpu_startup_data_t;

extern void cpu_startup32();
extern void cpu_startup64();

static uint8_t real_mode_code[] = {
#ifdef BREAK_AT_FIRST_COMMAND
	0xEB, 0xFE,                     /* jmp $ */
#endif
	0xB8, PTCH, PTCH,               /* 00: mov REAL_MODE_START_UP, %ax */
	0x8E, 0xD8,                     /* 03: mov %ax, %ds */
	0x8D, 0x36, PTCH, PTCH,         /* 05: lea GDTR_OFFSET_IN_PAGE, %si */
	0x0F, 0x01, 0x14,               /* 09: lgdt fword ptr [si] */
	0x0F, 0x20, 0xC0,               /* 12: mov %cr0, %eax */
	0x0C, 0x01,                     /* 15: or $1, %al */
	0x0F, 0x22, 0xC0,               /* 17: mov %eax, %cr0 */
	0x66, 0xbf, PTCH, PTCH,         /* 20: mov CPU_STARTUP_DATA, %edi */
	PTCH, PTCH,
	0x66, 0xbe, PTCH, PTCH,         /* 26: mov CPU_STARTUP64, %esi */
	PTCH, PTCH,
	0x66, 0xEA,                     /* 32: fjmp CS:CPU_STARTUP32 */
	PTCH, PTCH, PTCH, PTCH,         /* 34: CPU_STARTUP32 */
	0x10, 0x00,                     /* 38: CS_VALUE=0x10 */
};

static cpu_startup_data_t g_startup_data;

typedef void (*hand_over_c_func)(uint32_t cpu_id);
hand_over_c_func hand_over_entry;

void setup_cpu_startup_stack(uint32_t cpu_id, uint32_t esp)
{
	g_startup_data.esp[cpu_id] = esp;
}

uint32_t get_active_cpu_num(void)
{
	return g_startup_data.cpu_num;
}

void setup_sipi_page(uint64_t sipi_page, boolean_t need_wakeup_bsp, uint64_t c_entry)
{
	uint8_t *code_to_patch = (uint8_t *)sipi_page;
	gdtr32_t *gdtr_32;

	static const uint64_t gdt_32_table[] __attribute__ ((aligned(16))) = {
		0,
		0,
		0x00cf9a000000ffff, /* 32bit CS */
		0x00cf93000000ffff  /* 32bit DS */
	};

	memcpy(code_to_patch, (const void *)real_mode_code, (uint64_t)sizeof(real_mode_code));

	*((uint16_t *)(code_to_patch + REAL_MODE_STARTUP)) = (uint16_t)(sipi_page>>4);
	*((uint16_t *)(code_to_patch + GDTR_OFFSET_IN_CODE)) = (uint16_t)(GDTR_OFFSET_IN_PAGE);
	*((uint32_t *)(code_to_patch + CPU_STARTUP_DATA)) = (uint32_t)(uint64_t)(&g_startup_data);
	*((uint32_t *)(code_to_patch + CPU_STARTUP64)) = (uint32_t)(uint64_t)(&cpu_startup64);
	*((uint32_t *)(code_to_patch + CPU_STARTUP32)) = (uint32_t)(uint64_t)(&cpu_startup32);

	memcpy(code_to_patch + GDT_OFFSET_IN_PAGE, (uint8_t *)(uint64_t)&gdt_32_table[0], sizeof(gdt_32_table));
	gdtr_32 = (gdtr32_t *)(code_to_patch + GDTR_OFFSET_IN_PAGE);
	gdtr_32->base = (uint32_t)((uint64_t)code_to_patch + GDT_OFFSET_IN_PAGE);
	gdtr_32->limit = sizeof(gdt_32_table) - 1;

	/* prepare startup data */
	asm_sgdt(&g_startup_data.host_gdtr);
	g_startup_data.cs = asm_get_cs();
	g_startup_data.ds = asm_get_ds();
	g_startup_data.ss = asm_get_ss();
	g_startup_data.host_cr3 = (uint32_t)asm_get_cr3();
	g_startup_data.cpu_num = need_wakeup_bsp ? 0 : 1;

#ifdef STACK_PROTECTOR
	/* delive original fs base to each AP */
	g_startup_data.fs_base = asm_rdmsr(MSR_FS_BASE);
#endif

	hand_over_entry = (hand_over_c_func)c_entry;
}

/* Wakeup Application Processors(APs) */
void wakeup_aps(uint32_t sipi_page)
{
	broadcast_init();
	wait_us(10000);
	broadcast_startup(sipi_page >> 12);
	/* Note: According to IA32 spec, the sequence to wakeup APs should be INIT-SIPI-SIPI.
		 But, because: 1. this code will be used in S3, the first SIPI will wakeup APs and
		 resume to guest with "wait-for-sipi" state, then the second SIPI might trigger SIPI
		 VMExit, which is not expected; 2. for modern CPU, 1 SIPI is enough.
		 So, the second SIPI is skipped here. */
	//wait_us(10000);
	//broadcast_startup(sipi_page >> 12);
}

#define STARTAP_STACK_SIZE 0x400
static uint8_t ap_stack[STARTAP_STACK_SIZE * (MAX_CPU_NUM+1)];
/*---------------------------------------------------------------------------
 * Input:
 * sipi_page        - address to be used for bootstap.
 * expected_cpu_num - expected cpu num to launch
 * Return:
 * number of cpus that were init (including BSP).
 *---------------------------------------------------------------------------*/
uint32_t launch_aps(uint32_t sipi_page, uint8_t expected_cpu_num, uint64_t c_entry)
{
	uint32_t num_of_cpu;
	uint32_t i, esp;
	uint64_t tsc_start;

	if (expected_cpu_num == 1)
		return 1;

	/* prepare for sipi */
	for (i=0; i <= (MAX_CPU_NUM); i++) {
		esp = (uint32_t)(uint64_t)(&ap_stack[i*STARTAP_STACK_SIZE]);
		setup_cpu_startup_stack(i, esp);
	}
	setup_sipi_page(sipi_page, FALSE, c_entry);

	/* send sipi */
	wakeup_aps(sipi_page);

	if(expected_cpu_num != 0) {
		tsc_start = asm_rdtsc();
		do {
			num_of_cpu = get_active_cpu_num();

			/* According to IA32 Spec, aps should all up in 100ms */
			if ((asm_rdtsc() - tsc_start) > tsc_per_ms * 100) {
				return (uint32_t)-1;
			}

		} while (num_of_cpu != expected_cpu_num);
	} else {
		/* wait for predefined timeout 100ms.
		 * See IA32 spec volume 3 chapter 8 multiple-processor management
		 * 8.4 MP initialization 8.4.4 MP initialization example */
		wait_us(100000);

		num_of_cpu = get_active_cpu_num();
	}

	return num_of_cpu;
}
