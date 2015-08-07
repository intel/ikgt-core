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

#ifndef _VMCS_SW_OBJECT_H_
#define _VMCS_SW_OBJECT_H_

#include "mon_objects.h"

vmcs_object_t *vmcs_0_create(vmcs_object_t *vmcs_origin);
vmcs_object_t *vmcs_1_create(guest_cpu_handle_t gcpu, address_t gpa);

#endif                          /* _VMCS_SW_OBJECT_H_ */
