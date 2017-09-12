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

#define VMM_IDT_EXTRA_STACKS 1

/*-------------------------------------------------------*
*  FUNCTION     : idt_load()
*  PURPOSE      : Load IDT descriptor into IDTR on given CPU
*  ARGUMENTS    : void
*  RETURNS      : void
*-------------------------------------------------------*/
void idt_load(void);

/*-------------------------------------------------------*
*  FUNCTION     : idt_setup()
*  PURPOSE      : Build and populate IDT tables, one per CPU
*  ARGUMENTS    : void
*  RETURNS      : void
*-------------------------------------------------------*/
void idt_setup(void);

#endif                          /* _IDT_H_ */
