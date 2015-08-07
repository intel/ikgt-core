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

#ifndef _POOL_API_H_
#define _POOL_API_H_

#include <mon_defs.h>
#include <heap.h>

typedef void *pool_handle_t;
#define POOL_INVALID_HANDLE ((pool_handle_t)NULL)

pool_handle_t assync_pool_create(uint32_t size_of_single_element);

void *pool_allocate(pool_handle_t pool_handle);

void pool_free(pool_handle_t pool_handle, void *data);

#endif                          /* _POOL_API_H_ */
