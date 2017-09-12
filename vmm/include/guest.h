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

#ifndef _GUEST_H_
#define _GUEST_H_

#include "vmm_base.h"
#include "vmm_objects.h"
#include "vmexit.h"
#include "evmm_desc.h"
#include "ept.h"
#include "vmexit_cr_access.h"
#include "mam.h"

#define CR_HANDLER_NUM 4

struct guest_descriptor_t {
	guest_cpu_handle_t          gcpu_list;
	mam_handle_t                gpa_to_hpa;
	uint64_t                    cr0_mask;
	uint64_t                    cr4_mask;
	cr_write_handler            cr0_handlers[CR_HANDLER_NUM];
	cr_write_handler            cr4_handlers[CR_HANDLER_NUM];
	uint16_t                    id;
	uint16_t                    padding;
	ept_policy_t                ept_policy;
	uint64_t                    eptp;
	struct guest_descriptor_t   *next_guest;
};

guest_handle_t guest_handle(uint16_t guest_id);

/* there're many settings for a guest, such as number of gcpu, which gcpu register to which host cpu, ept policy, guest physical mapping.
** for this create_guest(), only number of gcpu is specified, other settings are:
** 1. gcpu register to the host cpu with same cpu id
** 2. guest physical mapping is 1:1 mapping for top_of_memory, and remove evmm's range
** 3. ept policy is EPT_POLICY, defined in .cfg
** In future, if there's new request to change these settings, please implement other versions of create_guest() api */
guest_handle_t create_guest(uint32_t gcpu_count, const module_file_info_t *evmm_file);

#endif                          /* _GUEST_H_ */
