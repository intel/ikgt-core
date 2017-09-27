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

#ifndef VMM_STACK_H
#define VMM_STACK_H
#include "vmm_base.h"
#include "evmm_desc.h"

/*-------------------------------------------------------------------------
 * Function: stack_initialize
 *  Description: This function is called in order to initialize internal data
 *               structures.
 *  Input:
 *         startup_struct - pointer to startup structure
 *-------------------------------------------------------------------------*/
void stack_initialize(evmm_desc_t *evmm_desc);

/*-------------------------------------------------------------------------
 * Function: stack_get_cpu_sp
 *  Description: This function is called in order to retrieve the initial value
 *               of stack pointer of specific cpu.
 *  Input:
 *         cpu_id - index of cpu
 *  Return Value:
 *         stack pointer(virtual address) that should be put into
 *                         ESP/RSP;
 *-------------------------------------------------------------------------*/
uint64_t stack_get_cpu_sp(uint16_t cpu_id);

/*-------------------------------------------------------------------------
 * Function: stack_get_details
 *  Description: This function return details of allocated memory for stacks.
 *  Output:
 *          total_stack_base - Host Virtual Address (pointer) of lowest used
 *          address
 *          total_stack_size - size allocated for all stacks;
 *-------------------------------------------------------------------------*/
void stack_get_details(uint64_t *total_stack_base, uint32_t *total_stack_size);

/*-------------------------------------------------------------------------
 * Function: stack_get_exception_sp
 *  Description: This function return the initial page of the stack that must
 *               be unmapped in vmm page tables and re-mapped to higher
 *               addresses.
 *  Input:
 *          cpu_id - cpu number
 *          stack_num - number of exception stack
 *  Return Value:
 *          exception stack pointer
 *-------------------------------------------------------------------------*/
uint64_t stack_get_exception_sp(uint16_t cpu_id);

#define stack_get_zero_page() stack_get_cpu_sp(host_cpu_num - 1)

#ifdef DEBUG
/*-------------------------------------------------------------------------
 * Function: stack_print
 *  Description: Prints inner map of stacks area.
 *-------------------------------------------------------------------------*/
void stack_print(void);
#endif /* DEBUG */

#endif
