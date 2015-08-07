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

#ifndef MTRR_ABSTRACTION_H
#define MTRR_ABSTRACTION_H

#include <mon_defs.h>
#include <mon_phys_mem_types.h>

#define IA32_MTRRCAP_ADDR  0xFE
#define IA32_MTRR_DEF_TYPE_ADDR 0x2FF
#define IA32_MTRR_FIX64K_00000_ADDR 0x250
#define IA32_MTRR_FIX16K_80000_ADDR 0x258
#define IA32_MTRR_FIX16K_A0000_ADDR 0x259
#define IA32_MTRR_FIX4K_C0000_ADDR  0x268
#define IA32_MTRR_FIX4K_C8000_ADDR  0x269
#define IA32_MTRR_FIX4K_D0000_ADDR  0x26A
#define IA32_MTRR_FIX4K_D8000_ADDR  0x26B
#define IA32_MTRR_FIX4K_E0000_ADDR  0x26C
#define IA32_MTRR_FIX4K_E8000_ADDR  0x26D
#define IA32_MTRR_FIX4K_F0000_ADDR  0x26E
#define IA32_MTRR_FIX4K_F8000_ADDR  0x26F
#define IA32_MTRR_PHYSBASE0_ADDR    0x200
#define IA32_MTRR_PHYSMASK0_ADDR    0x201
#define IA32_MTRR_PHYSBASE1_ADDR    0x202
#define IA32_MTRR_PHYSMASK1_ADDR    0x203
#define IA32_MTRR_PHYSBASE2_ADDR    0x204
#define IA32_MTRR_PHYSMASK2_ADDR    0x205
#define IA32_MTRR_PHYSBASE3_ADDR    0x206
#define IA32_MTRR_PHYSMASK3_ADDR    0x207
#define IA32_MTRR_PHYSBASE4_ADDR    0x208
#define IA32_MTRR_PHYSMASK4_ADDR    0x209
#define IA32_MTRR_PHYSBASE5_ADDR    0x20a
#define IA32_MTRR_PHYSMASK5_ADDR    0x20b
#define IA32_MTRR_PHYSBASE6_ADDR    0x20c
#define IA32_MTRR_PHYSMASK6_ADDR    0x20d
#define IA32_MTRR_PHYSBASE7_ADDR    0x20e
#define IA32_MTRR_PHYSMASK7_ADDR    0x20f
#define IA32_MTRR_PHYSBASE8_ADDR    0x210
#define IA32_MTRR_PHYSMASK8_ADDR    0x211
#define IA32_MTRR_PHYSBASE9_ADDR    0x212
#define IA32_MTRR_PHYSMASK9_ADDR    0x213

/*
 * Needs to be changed as per the MTRR value above
 */
#define IA32_MTRR_MAX_PHYSMASK_ADDR  IA32_MTRR_PHYSMASK9_ADDR


/*------------------------------------------------------------------------
 * Function: mtrrs_abstraction_bsp_initialize
 *  Description: This function must be called during initialization
 *               on BSP. It caches the MTRRs in its internal data structures.
 *  Return Value: TRUE in case of successful initialization, FALSE otherwise
 *
 *------------------------------------------------------------------------*/
boolean_t mtrrs_abstraction_bsp_initialize(void);

/*------------------------------------------------------------------------
 * Function: mtrrs_abstraction_ap_initialize
 *  Description: This function must be called during initialization
 *               on AP (not BSP). It checks whether cached information from
 *               BSP corresponds to current AP.
 *  Return Value: TRUE in case of successful initialization, FALSE otherwise
 *------------------------------------------------------------------------*/
boolean_t mtrrs_abstraction_ap_initialize(void);


/*------------------------------------------------------------------------
 * Function: mtrrs_abstraction_get_memory_type
 *  Description: This function returns the memory type of given Host
 *               Physical Address (HPA)
 *  Return Value: Memory type.
 *------------------------------------------------------------------------*/
mon_phys_mem_type_t mtrrs_abstraction_get_memory_type(hpa_t address);

mon_phys_mem_type_t mtrrs_abstraction_get_range_memory_type(hpa_t address,
							    OUT uint64_t *size,
							    uint64_t totalsize);

/*------------------------------------------------------------------------
 * Function: mtrrs_abstraction_track_mtrr_update
 *  Description: This function must be called if any of MTRRs is updated in
 *               order to update its internal caches.
 *  Return Value: TRUE in case the update is successful
 *                FALSE in case the mtrr_index is wrong or value is invalid
 *------------------------------------------------------------------------*/
boolean_t mtrrs_abstraction_track_mtrr_update(uint32_t mtrr_index,
					      uint64_t value);

/*------------------------------------------------------------------------
 * Function: mtrrs_abstraction_get_num_of_variable_range_regs
 *  Description: This function returns the number of variable MTRRs
 *               supported by the processor
 *  Return Value: number of variable MTRRs
 *------------------------------------------------------------------------*/
uint32_t mtrrs_abstraction_get_num_of_variable_range_regs(void);

/*------------------------------------------------------------------------
 * Function: mtrrs_is_variable_mtrrr_supported
 *  Description: This function returns whether the msr_id is
 *               supported by the processor or not
 *  Return Value: FALSE in case the MTRR MSR is unsupported on this processor
 *                TRUE in case the MSR is supported on this processor
 *------------------------------------------------------------------------*/
boolean_t mon_mtrrs_is_variable_mtrrr_supported(uint32_t msr_id);

#endif
