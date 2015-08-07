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

#ifndef MON_STACK_API_H
#define MON_STACK_API_H

#include <mon_defs.h>
#include <mon_startup.h>

/*-------------------------------------------------------------------------
 * Function: mon_stack_caclulate_stack_pointer
 *  Description: This function may be called at the first stages of the boot,
 *               when the mon_stack object is not initialized at all. It
 *               calculates the stack pointer of requested cpu.
 *  Input:
 *         startup_struct - pointer to startup structure
 *         cpu_id - index of the cpu
 *  Output:
 *         stack_pointer - value (virtual address) that should be put into
 *                         ESP/RSP
 *  Return Value: TRUE in case the calculation is successful.
 *-------------------------------------------------------------------------*/
boolean_t
mon_stack_caclulate_stack_pointer(IN const mon_startup_struct_t *startup_struct,
				  IN cpu_id_t cpu_id,
				  OUT hva_t *stack_pointer);


/*-------------------------------------------------------------------------
 * Function: mon_stack_initialize
 *  Description: This function is called in order to initialize internal data
 *               structures.
 *  Input:
 *         startup_struct - pointer to startup structure
 *  Return Value: TRUE in case the initialization is successful.
 *-------------------------------------------------------------------------*/
boolean_t mon_stack_initialize(IN const mon_startup_struct_t *startup_struct);


/*-------------------------------------------------------------------------
 * Function: mon_stack_is_initialized
 *  Description: Query whether the component is initialized.
 *  Return Value: TRUE in case the component was successfully initialized.
 *-------------------------------------------------------------------------*/
boolean_t mon_stack_is_initialized(void);


/*-------------------------------------------------------------------------
 * Function: mon_stack_get_stack_pointer_for_cpu
 *  Description: This function is called in order to retrieve the initial value
 *               of stack pointer of specific cpu.
 *  Input:
 *         cpu_id - index of cpu
 *  Output:
 *         stack_pointer - value (virtual address) that should be put into
 *                         ESP/RSP;
 *  Return Value: TRUE in case the query is successful.
 *                FALSE will be returned when the component wasn't initialized
 *                      or cpu_id has invalid value.
 *-------------------------------------------------------------------------*/
boolean_t mon_stack_get_stack_pointer_for_cpu(IN cpu_id_t cpu_id,
					      OUT hva_t *stack_pointer);

/*-------------------------------------------------------------------------
 * Function: mon_stacks_get_details
 *  Description: This function return details of allocated memory for stacks.
 *  Output:
 *          lowest_addr_used - Host Virtual Address (pointer) of lowest used
 *          address
 *          size - size allocated for all stacks;
 *  Return Value: Host Virtual Address (pointer) of the address
 *-------------------------------------------------------------------------*/
void mon_stacks_get_details(OUT hva_t *lowest_addr_used, OUT uint32_t *size);


/*-------------------------------------------------------------------------
 * Function: mon_stacks_get_exception_stack_for_cpu
 *  Description: This function return the initial page of the stack that must
 *               be unmapped in mon page tables and re-mapped to higher
 *               addresses.
 *  Input:
 *          cpu_id - cpu number
 *          stack_num - number of exception stack
 *  Output:
 *          page_addr - HVA of the page to guard;
 *  Return Value: TRUE in case of success.
 *                FALSE will be returned in cpu_id has invalid value
 *-------------------------------------------------------------------------*/
boolean_t mon_stacks_get_exception_stack_for_cpu(IN cpu_id_t cpu_id,
						 IN uint32_t stack_num,
						 OUT hva_t *page_addr);


#ifdef DEBUG
/*-------------------------------------------------------------------------
 * Function: mon_stacks_print
 *  Description: Prints inner map of stacks area.
 *-------------------------------------------------------------------------*/
void mon_stacks_print(void);
#endif /* DEBUG */

#endif
