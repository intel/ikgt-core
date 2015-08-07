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

#ifndef _IDT_H_
#define _IDT_H_

/*-------------------------------------------------------*
*  FUNCTION     : hw_idt_register_handler()
*  PURPOSE      : Register interrupt handler at spec. vector
*  ARGUMENTS    : uint8_t vector_id
*               : address_t handler - address of function
*  RETURNS      : void
*-------------------------------------------------------*/
void hw_idt_register_handler(vector_id_t vector_id,
			     address_t isr_handler_address);

/*-------------------------------------------------------*
*  FUNCTION     : hw_idt_load()
*  PURPOSE      : Load IDT descriptor into IDTR on given CPU
*  ARGUMENTS    : void
*  RETURNS      : void
*-------------------------------------------------------*/
void hw_idt_load(void);

/*-------------------------------------------------------*
*  FUNCTION     : hw_idt_setup()
*  PURPOSE      : Build and populate IDT tables, one per CPU
*  ARGUMENTS    : void
*  RETURNS      : void
*-------------------------------------------------------*/
void hw_idt_setup(void);

/*----------------------------------------------------*
 *  FUNCTION     : idt_get_extra_stacks_required()
 *  PURPOSE      : Returns the number of extra stacks required by ISRs
 *  ARGUMENTS    : void
 *  RETURNS      : number between 0..7
 *  NOTES        : per CPU
 *-------------------------------------------------------*/
uint8_t idt_get_extra_stacks_required(void);

#endif                          /* _IDT_H_ */
