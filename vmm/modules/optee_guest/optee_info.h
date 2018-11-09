/****************************************************************************
* Copyright (c) 2016 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0

* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
****************************************************************************/

#ifndef _OPTEE_INFO_H_
#define _OPTEE_INFO_H_

/* Different vmcall parameters structure from OSloader */
typedef struct {
	/* Size of this structure */
	uint64_t size_of_this_struct;
	/* Load time base address of trusty */
	uint32_t load_base;
	/* Load time size of trusty */
	uint32_t load_size;

	/* other fields will not used in EVMM */
} optee_boot_params_v0_t;

typedef struct {
	uint32_t size_of_this_struct;
	uint32_t version;

	/* 16MB under 4G */
	uint32_t runtime_addr;

	/* Entry address of trusty */
	uint32_t entry_point;
} optee_boot_params_v1_t;

#endif
