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

#ifndef _VMCALL_H_
#define _VMCALL_H_

#ifndef MODULE_VMCALL
#error "MODULE_VMCALL is not defined"
#endif

#include "vmm_objects.h"

typedef void (*vmcall_handler_t) (guest_cpu_handle_t gcpu);

void vmcall_register(uint16_t guest_id, uint32_t vmcall_id,
			 vmcall_handler_t handler);
void vmcall_init();

#endif                          /* _VMCALL_H_ */
