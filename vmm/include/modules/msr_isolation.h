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

#ifndef _MSR_ISOLATION_H_
#define _MSR_ISOLATION_H_

#ifndef MODULE_MSR_ISOLATION
#error "MODULE_MSR_ISOLATION is not defined"
#endif

typedef enum {
	GUESTS_ISOLATION = 0,
	GUEST_HOST_ISOLATION
} msr_policy_t;

void add_to_msr_isolation_list(uint32_t msr_index, uint64_t msr_value, msr_policy_t msr_policy);
void msr_isolation_init();

#endif /* _MSR_ISOLATION_H_ */
