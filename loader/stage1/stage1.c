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

#include "vmm_asm.h"
#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "ldr_dbg.h"

#include "lib/image_loader.h"
#ifdef LIB_MP_INIT
#include "lib/mp_init.h"
#endif
#include "lib/util.h"

/* If CPU_NUM is defined, it must be equal to MAX_CPU_NUM */
#ifdef CPU_NUM
#if CPU_NUM != MAX_CPU_NUM
#error "Bad CPU_NUM definition!"
#endif
#endif

typedef void (* vmm_entry_t)(uint32_t cpuid, void *evmm_desc);
static void *evmm_desc;
static vmm_entry_t vmm_main;

#if (CPU_NUM != 1)

/* Initial AP setup in protected mode - should never return */
static void ap_continue_wakeup_code_C(uint32_t cpu_id)
{
	if (cpu_id == MAX_CPU_NUM) {
		printf("Actual cpu number is larger than MAX_CPU_NUM(%d).\n", MAX_CPU_NUM);
		__STOP_HERE__;
	}

	/* launch vmm on ap */
	vmm_main(cpu_id, evmm_desc);
	printf("ap(%d) launch vmm fail.\n", cpu_id);
	__STOP_HERE__;
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
static uint32_t launch_aps(uint32_t sipi_page, uint8_t expected_cpu_num)
{
	uint32_t num_of_cpu;
	uint32_t i, esp;
	uint64_t tsc_start;

	if (expected_cpu_num == 1)
		return 1;

	/* prepare for sipi */
	for (i=0; i<=(MAX_CPU_NUM); i++) {
		esp = (uint32_t)(uint64_t)(&ap_stack[i*STARTAP_STACK_SIZE]);
		setup_cpu_startup_stack(i, esp);
	}
	setup_sipi_page(sipi_page, FALSE, (uint64_t)ap_continue_wakeup_code_C);

	/* send sipi */
	wakeup_aps(sipi_page);

	if(expected_cpu_num != 0) {
		tsc_start = asm_rdtsc();
		do {
			num_of_cpu = get_active_cpu_num();

			/* According to IA32 Spec, aps should all up in 100ms */
			if ((asm_rdtsc() - tsc_start) > tsc_per_ms * 100) {
				print_panic("Expected %d cpus, but only %d enumarated!\n",
						expected_cpu_num, num_of_cpu);
				return (uint32_t)-1;
			}

		} while (num_of_cpu != expected_cpu_num);
	} else {
		print_warn("Expected CPU_NUM not defined, wait 100ms to enumarate all APs!\n");
		/* wait for predefined timeout 100ms.
		 * See IA32 spec volume 3 chapter 8 multiple-processor management
		 * 8.4 MP initialization 8.4.4 MP initialization example */
		wait_us(100000);

		num_of_cpu = get_active_cpu_num();
	}

	return num_of_cpu;
}
#endif

void stage1_main(evmm_desc_t *xd)
{
#if (CPU_NUM != 1)
	uint32_t num_of_cpu;
#endif
	print_init(FALSE);

	if (xd->top_of_mem == 0)
		return;

	if (xd->tsc_per_ms == 0)
		xd->tsc_per_ms = determine_nominal_tsc_freq()/1000ULL;

	tsc_per_ms = xd->tsc_per_ms;
	if (tsc_per_ms == 0) {
		print_panic("%s: Invalid TSC frequency!\n", __func__);
		return;
	}

#ifdef CPU_NUM
	if ((xd->num_of_cpu != 0) &&
		(xd->num_of_cpu != CPU_NUM)) {
		print_warn("xd->num_of_cpu is not 0 and not equal to CPU_NUM, it will be ignored!\n");
	}
	xd->num_of_cpu = CPU_NUM;
#else
	if (xd->num_of_cpu == 0)
	{
		print_warn("xd->num_of_cpu is 0, will 100ms delay to enumarate APs!\n");
	}
#endif

	/* Load evmm image */
	if (!relocate_elf_image(&(xd->evmm_file), (uint64_t *)&vmm_main)) {
		print_panic("relocate evmm file fail\n");
		return;
	}
	evmm_desc = (void *)xd;

/* From https://gcc.gnu.org/onlinedocs/cpp/Defined.html,
 * we know if CPU_NUM is not defined, it will be interpreted
 * as having the value zero.so #if (CPU_NUM == 1) means that
 * #if defined (CPU_NUM) && (CPU_NUM == 1). So #if (CPU_NUM != 1)
 * means CPU_NUM is not defined or defined but not equal to 1.*/
#if (CPU_NUM != 1)
	num_of_cpu = launch_aps(xd->sipi_ap_wkup_addr, xd->num_of_cpu);
	if (num_of_cpu > MAX_CPU_NUM) {
		print_panic("num of cpus is larger than MAX_CPU_NUM\n");
		return;
	}
	if (xd->num_of_cpu == 0)
		xd->num_of_cpu = num_of_cpu;
#endif

	print_info("Loader: Launch VMM\n");

	/* launch vmm on BSP */
	vmm_main(0, evmm_desc);
	/*should never return here!*/
	return;
}
/* End of file */
