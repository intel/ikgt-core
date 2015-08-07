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

#include "vmcall_api.h"
#include "mon_objects.h"

typedef mon_status_t (*vmcall_handler_t) (guest_cpu_handle_t gcpu,
					  address_t *arg1, address_t *arg2,
					  address_t *arg3);

void vmcall_intialize(void);

void vmcall_guest_intialize(guest_id_t guest_id);

void mon_vmcall_register(guest_id_t guest_id,
			 vmcall_id_t vmcall_id,
			 vmcall_handler_t handler,
			 boolean_t special_call);

#endif                          /* _VMCALL_H_ */
