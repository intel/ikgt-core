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

#include "memory_allocator.h"
#include "pool_api.h"
#include "hw_utils.h"
#include "mon_dbg.h"
#include "common_libc.h"
#include "lock.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(MEMORY_ALLOCATOR_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(MEMORY_ALLOCATOR_C, __condition)

typedef struct {
	uint32_t	size;
	uint32_t	offset;
} mem_allocation_info_t;

/* pool per element size (2^x bytes, x = 0, 1,...11) */
#define NUMBER_OF_POOLS     12

/* pool per element size (2^x bytes, x = 0, 1,...11) */
static pool_handle_t pools[NUMBER_OF_POOLS] = { POOL_INVALID_HANDLE };

static mon_lock_t lock = LOCK_INIT_STATE;

static uint32_t buffer_size_to_pool_index(uint32_t size)
{
	uint32_t pool_index = 0;
	uint32_t pool_element_size = 0;

	MON_ASSERT(size != 0);

	hw_scan_bit_backward((uint32_t *)&pool_index, size);
	pool_element_size = 1 << pool_index;
	if (pool_element_size < size) {
		pool_element_size = pool_element_size << 1;
		pool_index++;
	}

	return pool_index;
}


static
void *mon_mem_allocate_internal(char *file_name,
				int32_t line_number,
				IN uint32_t size, IN uint32_t alignment)
{
	pool_handle_t pool = NULL;
	uint32_t pool_index = 0;
	uint32_t pool_element_size = 0;
	void *ptr;
	uint64_t allocated_addr;
	mem_allocation_info_t *alloc_info;
	uint32_t size_to_request;

	if (size > ((2 KILOBYTE) - sizeof(mem_allocation_info_t))) {
		/* starting from 2KB+1 need a full page */

		MON_LOG(mask_anonymous,
			level_trace,
			"%s: WARNING: Memory allocator supports allocations of"
			" sizes up to 2040 bytes (requested size = 0x%x from %s:%d)\n",
			__FUNCTION__,
			size,
			file_name,
			line_number);
		/* remove when encountered, make sure to treat this case
		 * in the caller */
		MON_ASSERT(0);
		return NULL;
	}

	if (alignment >= PAGE_4KB_SIZE) {
		MON_LOG(mask_anonymous,
			level_trace,
			"%s: WARNING: Requested alignment is 4K or more, use full page"
			" allocation (requested alignment = 0x%x)\n",
			__FUNCTION__,
			alignment);
		/* remove when encountered, make sure to treat this case
		 * in the caller */
		MON_ASSERT(0);
		return NULL;
	}

	MON_ASSERT(IS_POW_OF_2(alignment));
	MON_ASSERT(alignment >= sizeof(mem_allocation_info_t));

	if (alignment > sizeof(mem_allocation_info_t)) {
		uint32_t adjusted_size = (size < alignment) ? alignment : size;

		size_to_request = adjusted_size * 2;
	} else {
		size_to_request = size + sizeof(mem_allocation_info_t);
	}

	pool_index = buffer_size_to_pool_index(size_to_request);
	pool_element_size = 1 << pool_index;

	lock_acquire(&lock);
	pool = pools[pool_index];
	if (NULL == pool) {
		pool = pools[pool_index] =
			       assync_pool_create((uint32_t)pool_element_size);
		MON_ASSERT(pool);
	}

	ptr = pool_allocate(pool);
	lock_release(&lock);
	if (NULL == ptr) {
		return NULL;
	}

	mon_zeromem(ptr, pool_element_size);

	allocated_addr = (uint64_t)ptr;

	/* Check alignment */
	MON_ASSERT(ALIGN_BACKWARD(allocated_addr, (uint64_t)alignment) ==
		allocated_addr);

	alloc_info =
		(mem_allocation_info_t *)(allocated_addr + alignment -
					  sizeof(mem_allocation_info_t));

	alloc_info->size = pool_element_size;
	alloc_info->offset = alignment;

	return (void *)(allocated_addr + alignment);
}

/* Following functions- mon_mem_allocate() and mon_mem_free() have an
 * allocation limit of 2040 bytes and need to be extended in future.
 * mon_page_alloc() and mon_page_free() are used as a temporary solution for
 * allocation of more than 2040 bytes using page alignment of buffer for
 * differentiating between mon_malloc allocation() and mon_page_alloc(). */
void *mon_mem_allocate(char *file_name, int32_t line_number, IN uint32_t size)
{
	return mon_mem_allocate_internal(file_name,
		line_number,
		size, sizeof(mem_allocation_info_t));
}

void mon_mem_free(char *file_name, int32_t line_number, IN void *buff)
{
	mem_allocation_info_t *alloc_info;
	void *allocated_buffer;
	uint32_t pool_element_size = 0;
	uint32_t pool_index = 0;
	pool_handle_t pool = NULL;

	if (buff == NULL) {
		MON_LOG(mask_anonymous,
			level_trace,
			"In %s#%d try to free NULL\n",
			file_name,
			line_number);
		return;
	}

	alloc_info =
		(mem_allocation_info_t *)((uint64_t)buff -
					  sizeof(mem_allocation_info_t));
	pool_element_size = alloc_info->size;
	MON_ASSERT(IS_POW_OF_2(pool_element_size));

	pool_index = buffer_size_to_pool_index(pool_element_size);
	allocated_buffer = (void *)((uint64_t)buff - alloc_info->offset);

	lock_acquire(&lock);
	pool = pools[pool_index];
	MON_ASSERT(pool != NULL);

	pool_free(pool, allocated_buffer);
	lock_release(&lock);
}

void *mon_mem_allocate_aligned(char *file_name,
			       int32_t line_number,
			       IN uint32_t size, IN uint32_t alignment)
{
	if (!IS_POW_OF_2(alignment)) {
		MON_LOG(mask_anonymous, level_trace,
			"%s: WARNING: Requested alignment is not power of 2\n",
			__FUNCTION__);
		return NULL;
	}

	return mon_mem_allocate_internal(file_name, line_number, size,
		alignment);
}

uint32_t mon_mem_buff_size(char *file_name, int32_t line_number, IN void *buff)
{
	mem_allocation_info_t *alloc_info;
	uint32_t pool_element_size = 0;

	if (buff == NULL) {
		MON_LOG(mask_anonymous,
			level_trace,
			"In %s#%d try to access NULL\n",
			file_name,
			line_number);
		return 0;
	}

	alloc_info =
		(mem_allocation_info_t *)((uint64_t)buff -
					  sizeof(mem_allocation_info_t));
	pool_element_size = alloc_info->size;
	MON_ASSERT(IS_POW_OF_2(pool_element_size));

	return pool_element_size;
}

static
uint32_t mon_mem_pool_size_internal(char *file_name,
				    int32_t line_number,
				    IN uint32_t size, IN uint32_t alignment)
{
	uint32_t pool_index = 0;
	uint32_t pool_element_size = 0;
	uint32_t size_to_request;

	if (alignment > sizeof(mem_allocation_info_t)) {
		uint32_t adjusted_size = (size < alignment) ? alignment : size;

		size_to_request = adjusted_size * 2;
	} else {
		size_to_request = size + sizeof(mem_allocation_info_t);
	}

	pool_index = buffer_size_to_pool_index(size_to_request);
	pool_element_size = 1 << pool_index;
	return pool_element_size;
}

uint32_t mon_mem_pool_size(char *file_name,
			   int32_t line_number,
			   IN uint32_t size)
{
	return mon_mem_pool_size_internal(file_name,
		line_number,
		size, sizeof(mem_allocation_info_t));
}

