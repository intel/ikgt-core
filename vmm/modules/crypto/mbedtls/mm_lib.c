/*******************************************************************************
* Copyright (c) 2018 Intel Corporation
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

#include "vmm_base.h"
#include "heap.h"
#include "lib/util.h"

void *calloc(IN uint32_t num_elements, IN uint32_t element_size)
{
	void *memory = mem_alloc(num_elements * element_size);

	/* Determine if memory was allocated */
	if (memory != NULL) {
		/* Zero all the memory */
		(void)memset(memory, 0U, num_elements * element_size);
	}

	/* Return pointer to memory */
	return memory;
}
