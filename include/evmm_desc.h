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

#ifndef _EVMM_DESC_H_
#define _EVMM_DESC_H_

#include "vmm_base.h"
#include "vmm_arch.h"

/*--------------------------------------------------------------------------
 *
 * gcpu_state_t: Initial Guest CPU State
 *
 * Specified per each active CPU for each guest, and will be put into Guest
 * VMCS
 * at guest launch time. All addresses are absolute values that should be put
 * in VMCS.
 *
 * If for some guest CPU gcpu_state_t is not specified,
 * this guest CPU is put into the Wait-for-SIPI state.
 * gcpu_state_t must be specified for at least first processor
 * (BSP) of each guest.
 *
 * Guest initial entry point should be set as the CS:RIP pair:
 *
 * - CS is specified in the segment[SEG_CS].selector value
 *
 * These values are specified as:
 *
 * 1. If guest paging is active CS:RIP is in the GVA notation
 * 2. If guest is in protected non-paged mode CS:RIP is in the GPA notation
 * 3. If guest is in the real mode CS:RIP is in the GPA notation and CS
 *    specifies the GPA value of the segment base, shifted right 4 bits.
 *
 *------------------------------------------------------------------------- */

typedef struct {
	uint64_t     gp_reg[REG_GP_COUNT];
	uint64_t     rip;
	uint64_t     rflags;
	segment_t    segment[SEG_COUNT];
	uint64_t     cr0;
	uint64_t     cr3;
	uint64_t     cr4;
	uint64_t     msr_efer;
	uint16_t    pad_gdtr[3];
	gdtr64_t    gdtr;
	uint16_t    pad_idtr[3];
	idtr64_t    idtr;
} gcpu_state_t;

/* file modules (raw binary) mapped in memory/RAM */
typedef struct {
	/* where it is before relocate */
	uint64_t loadtime_addr;
	/* the size of the binary before relocate */
	uint64_t loadtime_size;
	/* where it is after relocate */
	uint64_t runtime_addr;
	/* size excluding heap/stack after relocate */
	uint64_t runtime_image_size;
	/* size including heap/stack after relocate */
	uint64_t runtime_total_size;
} module_file_info_t;

typedef struct {
	/* trusty or others */
	module_file_info_t lk_file;
	gcpu_state_t gcpu0_state;
	void *dev_sec_info;
} trusty_desc_t;

typedef struct {
	/* op-tee or others */
	module_file_info_t optee_file;
	gcpu_state_t gcpu0_state;
	void *dev_sec_info;
} optee_desc_t;

typedef struct {
	uint8_t num_of_cpu; /* filled in stage0/1 */
	uint8_t pad[3];
	uint32_t sipi_ap_wkup_addr; /* filled in stage0 */
	/* top of usable memory in E820 */
	uint64_t top_of_mem; /* filled in stage0 */
	uint64_t tsc_per_ms; /* filled in stage0 */
	module_file_info_t stage1_file; /* filled in stage0 */
	module_file_info_t evmm_file; /* filled in stage0 */
	gcpu_state_t guest0_gcpu0_state; /* filled in stage0 */
#ifdef MODULE_APS_STATE
	gcpu_state_t guest0_aps_state[MAX_CPU_NUM - 1]; /* filled in stage1 */
#endif

#ifdef MODULE_TRUSTY_GUEST
	/* trusty related info */
	trusty_desc_t trusty_desc; /* filled in stage0 */
#endif

#ifdef MODULE_OPTEE_GUEST
	/* op-tee related info */
	optee_desc_t optee_desc; /* filled in stage0 */
#endif

#ifdef LIB_EFI_SERVICES
	/* System table base address. Only for EFI platform. */
	uint64_t system_table_base;
#endif
} evmm_desc_t;

#endif  /* _EVMM_DESC_H_ */
