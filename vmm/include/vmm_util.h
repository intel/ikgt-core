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

#ifndef _HW_UTILS_H_
#define _HW_UTILS_H_

#include "vmm_asm.h"
#include "vmm_base.h"
#include "gdt.h"



/*-------------------------------------------------------------------------
 * Perform IRET instruction.
 * void hw_perform_asm_iret(void);
 *------------------------------------------------------------------------- */
void hw_perform_asm_iret(void);

/*-------------------------------------------------------------------------
 * read/write segment registers
 *------------------------------------------------------------------------- */
void hw_write_cs(uint16_t);
/*-------------------------------------------------------------------------
 * sets new hw stack pointer (esp/rsp), jumps to the given function and
 * passes the given param to the function. This function never returns.
 * the function "func" should also never return.
 *
 *------------------------------------------------------------------------- */
typedef void (*func_main_continue_t) (void *params);
void hw_set_stack_pointer(uint64_t new_stack_pointer,
				       func_main_continue_t func,
				       void *params);

#define hw_flash_tlb()      asm_set_cr3(asm_get_cr3())

uint32_t get_max_phy_addr(void);

#endif   /* _HW_UTILS_H_ */
