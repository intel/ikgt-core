/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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
