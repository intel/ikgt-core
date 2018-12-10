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
static void ap_continue(uint32_t cpu_id)
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
		print_warn("xd->num_of_cpu is 0, will 100ms delay to enumarate APs!\n");
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
	num_of_cpu = launch_aps(xd->sipi_ap_wkup_addr, xd->num_of_cpu, (uint64_t)ap_continue);
	if (num_of_cpu > MAX_CPU_NUM) {
		print_panic("%s: Failed to launch all APs\n", __func__);
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
