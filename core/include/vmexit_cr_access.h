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

#ifndef VMEXIT_CR_ACCESS_H
#define VMEXIT_CR_ACCESS_H

#include <mon_defs.h>
#include <mon_objects.h>
#include <mon_arch_defs.h>
#include <event_mgr.h>

mon_ia32_control_registers_t vmexit_cr_access_get_cr_from_qualification(
	uint64_t qualification);
mon_ia32_gp_registers_t vmexit_cr_access_get_operand_from_qualification(
	uint64_t qualification);
raise_event_retval_t cr_raise_write_events(guest_cpu_handle_t gcpu,
					   mon_ia32_control_registers_t reg_id,
					   address_t new_value);

#endif                          /* VMEXIT_CR_ACCESS_H */
