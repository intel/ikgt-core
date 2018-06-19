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

#ifndef _ISR_H_
#define _ISR_H_

/*----------------------------------------------------*
 *  FUNCTION     : isr_setup()
 *  PURPOSE      : Builds ISR wrappers, IDT tables and
 *               : default ISR handlers for all CPUs.
 *  ARGUMENTS    : IN uint8_t number_of_cpus
 *  RETURNS      : void
 *-------------------------------------------------------*/
void isr_setup(void);

#endif   /* _ISR_H_ */
