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

#ifndef VMCS_MERGE_SPLIT_H
#define VMCS_MERGE_SPLIT_H

#include <mon_defs.h>
#include <vmcs_api.h>

void ms_merge_to_level2(IN guest_cpu_handle_t gcpu,
			IN boolean_t merge_only_dirty);

void ms_split_from_level2(IN guest_cpu_handle_t gcpu);

void ms_merge_to_level1(IN guest_cpu_handle_t gcpu,
			IN boolean_t was_vmexit_from_level1,
			IN boolean_t merge_only_dirty);

#endif                          /* VMCS_MERGE_SPLIT_H */
