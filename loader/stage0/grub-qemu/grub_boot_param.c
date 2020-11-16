/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "grub_boot_param.h"
#include "ldr_dbg.h"
#include "stage0_lib.h"

#include "lib/util.h"
#include "lib/string.h"

#define RUNTIME_MEMORY_ADDR         GRUB_HEAP_ADDR
#define EVMM_RUNTIME_SIZE           0x800000        //8M
#define LK_RUNTIME_SIZE             0x1000000       //16M
#define RUNTIME_MEMORY_SIZE         0x1400000       //20M

#define SIPI_AP_WKUP_ADDR           0x59000

#define STAGE1_IMG_SIZE             0xC000

/* The oscillator used by the PIT chip runs at 1.193182MHz */
#define INTERNAL_FREQ   1193182UL

/* Mode/Command register */
#define I8253_CONTROL_REG   0x43
/* Channel 0 data port */
#define I8253_DATA_REG      0x40

/* loader memory layout */
typedef struct {
	/* below must be page aligned for
	 * further ept protection */
	/* stage1 image in RAM */
	uint8_t stage1[STAGE1_IMG_SIZE];

	evmm_desc_t xd;

	uint8_t seed[BUP_MKHI_BOOTLOADER_SEED_LEN];
	/* add more if any */
} memory_layout_t;

static uint64_t heap_current;
static uint64_t heap_top;

void init_memory_manager(uint64_t heap_base_address, uint32_t heap_size)
{
	heap_current = heap_base_address;
	heap_top = heap_base_address + heap_size;
}

void *allocate_memory(uint32_t size_request)
{
	uint64_t address;

	if (heap_current + size_request > heap_top) {
		print_panic("memory allocation faied, current heap = 0x%llx, size = 0x%lx,"
				" heap top = 0x%llx\n", heap_current, size_request, heap_top);
		return NULL;
	}

	address = heap_current;
	heap_current += size_request;
	memset((void *)address, 0, size_request);
	return (void *)address;
}

static evmm_desc_t *init_evmm_desc(void)
{
	evmm_desc_t *evmm_desc = NULL;
	void *evmm_runtime_mem = NULL;
	memory_layout_t *loader_mem = NULL;

	/*allocate memory for every block*/
	evmm_runtime_mem = allocate_memory(EVMM_RUNTIME_SIZE);
	if (evmm_runtime_mem == NULL) {
		print_panic("allocate evmm runtime mem failed!\n");
		return NULL;
	}

	loader_mem = (memory_layout_t *)allocate_memory(sizeof(memory_layout_t));
	if (loader_mem == NULL) {
		print_panic("allocate loader mem failed!\n");
		return NULL;
	}

	/*fill evmm boot params*/
	evmm_desc = &(loader_mem->xd);

	evmm_desc->evmm_file.runtime_addr = (uint64_t)evmm_runtime_mem;
	evmm_desc->evmm_file.runtime_total_size = EVMM_RUNTIME_SIZE;

	evmm_desc->stage1_file.runtime_addr = (uint64_t)loader_mem->stage1;
	evmm_desc->stage1_file.runtime_total_size = STAGE1_IMG_SIZE;

	evmm_desc->sipi_ap_wkup_addr = (uint64_t)SIPI_AP_WKUP_ADDR;

#ifdef MODULE_TRUSTY_TEE
	/*fill trusty boot params*/
	evmm_desc->trusty_tee_desc.tee_file.runtime_addr = 0;
	evmm_desc->trusty_tee_desc.tee_file.runtime_total_size = LK_RUNTIME_SIZE;
#endif

	return evmm_desc;
}

static inline void serialing_instruction(void)
{
	cpuid_params_t info = {0, 0, 0, 0};

	asm_cpuid(&info);
}

static uint64_t calibrate_tsc_per_ms(void)
{
	uint64_t begin, end;
	uint8_t status = 0;

	/* Set PIT mode to count down and set OUT pin high when count reaches 0 */
	asm_out8(I8253_DATA_REG, 0x30);

	/*
	 * According to ISDM vol3 chapter 17.17:
	 *  The RDTSC instruction is not serializing or ordered with other
	 *  instructions. Subsequent instructions may begin execution before
	 *  the RDTSC instruction operation is performed.
	 *
	 * Insert serialing instruction before and after RDTSC to make
	 * calibration more accurate.
	 */
	serialing_instruction();
	begin = asm_rdtsc();
	serialing_instruction();

	/* Write MSB in counter 0 */
	asm_out8(I8253_DATA_REG, (INTERNAL_FREQ / 1000) >> 8);

	do {
		/* Read-back command, count MSB, counter 0 */
		asm_out8(I8253_CONTROL_REG, 0xE2);
		/* Wait till OUT pin goes high and null count goes low */
		status = asm_in8(I8253_DATA_REG);
	} while ((status & 0xC0) != 0x80);

	/* Make sure all instructions above executed and submitted */
	serialing_instruction();
	end = asm_rdtsc();
	serialing_instruction();

	/* Enable interrupt mode that will stop the decreasing counter of the PIT */
	asm_out8(I8253_CONTROL_REG, 0x30);

	return end - begin;
}

evmm_desc_t *boot_params_parse(multiboot_info_t *mbi)
{
	evmm_desc_t *evmm_desc = NULL;
	uint64_t top_of_mem = 0;
	uint64_t cali_tsc_per_ms = 0;

	evmm_desc = init_evmm_desc();
	if (!evmm_desc) {
		print_panic("Failed to init evmm desc!\n");
		return NULL;
	}

	top_of_mem = get_top_of_memory(mbi);
	if (top_of_mem == 0) {
		print_panic("Failed to get top_of memory from mbi!\n");
		return NULL;
	}

	cali_tsc_per_ms = calibrate_tsc_per_ms();

	evmm_desc->num_of_cpu = CPU_NUM;
	evmm_desc->tsc_per_ms = cali_tsc_per_ms;
	evmm_desc->top_of_mem = top_of_mem;

	return evmm_desc;
}

