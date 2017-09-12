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

#ifndef _VMX_ASM_H_
#define _VMX_ASM_H_

/*------------------------------------------------------------------------*
*  FUNCTION : vmexit_func()
*  PURPOSE  : Called upon VMEXIT. It in turn calls vmexit_common_handler()
*  ARGUMENTS: none
*  RETURNS  : void
*------------------------------------------------------------------------*/
void vmexit_func(void);

/*------------------------------------------------------------------------*
*  FUNCTION : vmentry_func()
*  PURPOSE  : Called upon VMENTER.
*  ARGUMENTS: uint32_t launch - if not zero do VMLAUNCH, otherwise VMRESUME
*  RETURNS  : void
*------------------------------------------------------------------------*/
void vmentry_func(uint32_t launch);

#endif                          /* _VMX_ASM_H_ */
