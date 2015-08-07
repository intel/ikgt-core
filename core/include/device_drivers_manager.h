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

#ifndef DEVICE_DRIVERS_MANAGER_H
#define DEVICE_DRIVERS_MANAGER_H

#include <mon_defs.h>
#include <guest.h>

void ddm_initialize(void);
void ddm_register_guest(guest_handle_t guest_handle);
boolean_t ddm_notify_driver(uint64_t descriptor_handle, uint32_t component_id);

#endif
