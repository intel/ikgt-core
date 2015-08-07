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

#include <pool_api.h>
#include <hash64_api.h>
#include <heap.h>
#include <common_libc.h>
#include "pool.h"
#include "mon_dbg.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(POOL_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(POOL_C, __condition)


/* Get Release Mutual exclussion lock macros. */
#define POOL_AQUIRE_LOCK(_pool_)             \
	do {                                 \
		if ((_pool_)->mutex_flag) {  \
			lock_acquire(&(_pool_)->access_lock); } \
	} while (0)

#define POOL_RELEASE_LOCK(_pool_)            \
	do {                                 \
		if ((_pool_)->mutex_flag) {  \
			lock_release(&(_pool_)->access_lock); } \
	} while (0)

/* forward declarations: */
static
void pool_insert_node_into_list(pool_list_head_t *list_head,
				pool_list_element_t *element);

INLINE uint64_t pool_ptr_to_uint64(void *ptr)
{
	return (uint64_t)ptr;
}

INLINE void *pool_uint64_to_ptr(uint64_t value)
{
	return (void *)value;
}

INLINE void pool_init_list_head(pool_list_head_t *list_head)
{
	pool_list_head_set_first_element(list_head, NULL);
	pool_list_head_set_last_element(list_head, NULL);
	pool_list_head_set_num_of_elements(list_head, 0);
}

INLINE boolean_t pool_is_list_empty(const pool_list_head_t *list_head)
{
	boolean_t res = (pool_list_head_get_num_of_elements(list_head) == 0);

	MON_ASSERT((!res) ||
		(pool_list_head_get_first_element(list_head) == NULL));
	MON_ASSERT((!res) || (pool_list_head_get_last_element(
				      list_head) == NULL));
	return res;
}

INLINE void pool_allocate_single_page_from_heap(pool_t *pool)
{
	void *page = mon_page_alloc(1);
	uint32_t num_of_allocated_pages = pool_get_num_of_allocated_pages(pool);
	pool_list_head_t *free_pages_list = pool_get_free_pages_list(pool);

	if (page != NULL) {
		num_of_allocated_pages++;
		pool_set_num_of_allocated_pages(pool, num_of_allocated_pages);

		pool_insert_node_into_list(free_pages_list,
			(pool_list_element_t *)page);
	}
}

/* -----------------------------------------------------------------------
 * INLINE uint32_t pool_calculate_number_of_free_pages_to_keep(pool_t* pool) {
 *     uint32_t num_of_allocated_pages = pool_get_num_of_allocated_pages(pool);
 *     uint32_t num = (num_of_allocated_pages / POOL_PAGES_TO_KEEP_THRESHOLD);
 *
 *     if (num < POOL_MIN_NUMBER_OF_FREE_PAGES) {
 *         num = POOL_MIN_NUMBER_OF_FREE_PAGES;
 *     }
 * return num; }
 * -----------------------------------------------------------------------*/
INLINE uint32_t pool_calculate_number_of_new_pages_to_allocate(pool_t *pool)
{
	uint32_t num_of_allocated_pages = pool_get_num_of_allocated_pages(pool);
	uint32_t num =
		(num_of_allocated_pages / POOL_PAGES_TO_ALLOCATE_THRESHOLD);

	if (num > POOL_MAX_NUM_OF_PAGES_TO_ALLOCATE) {
		num = POOL_MAX_NUM_OF_PAGES_TO_ALLOCATE;
	} else if (num == 0) {
		num = POOL_PAGES_TO_KEEP_THRESHOLD;
	}

	return num;
}

/*----------------------------------------------*/

static
boolean_t pool_is_allocation_counters_ok(pool_t *pool)
{
	uint32_t allocated_num;

	allocated_num = pool_get_num_of_pages_used_for_hash_nodes(pool) +
			pool_get_num_of_pages_used_for_pool_elements(pool) +
			pool_list_head_get_num_of_elements(pool_get_free_pages_list(
			pool));
	return pool_get_num_of_allocated_pages(pool) == allocated_num;
}

static
pool_list_element_t *pool_allocate_node_from_list(pool_list_head_t *list_head)
{
	pool_list_element_t *first_element;
	pool_list_element_t *second_element;
	uint32_t num_of_elements;

	MON_ASSERT(!pool_is_list_empty(list_head));
	first_element = pool_list_head_get_first_element(list_head);
	MON_ASSERT(first_element != NULL);
	second_element = pool_list_element_get_next(first_element);
	if (second_element != NULL) {
		pool_list_element_set_prev(second_element, NULL);
	}

	pool_list_head_set_first_element(list_head, second_element);
	if (second_element == NULL) {
		pool_list_head_set_last_element(list_head, NULL);
	}
	num_of_elements = pool_list_head_get_num_of_elements(list_head);
	pool_list_head_set_num_of_elements(list_head, num_of_elements - 1);
	return first_element;
}

static
void pool_insert_node_into_list_at_tail(pool_list_head_t *list_head,
					pool_list_element_t *element)
{
	uint32_t num_of_elements;
	pool_list_element_t *curr_last_element;

	curr_last_element = pool_list_head_get_last_element(list_head);
	if (curr_last_element != NULL) {
		pool_list_element_set_next(curr_last_element, element);
	} else {
		MON_ASSERT(pool_list_head_get_first_element(list_head) == NULL);
		pool_list_head_set_first_element(list_head, element);
	}

	pool_list_element_set_prev(element, curr_last_element);
	pool_list_element_set_next(element, NULL);
	pool_list_head_set_last_element(list_head, element);

	num_of_elements = pool_list_head_get_num_of_elements(list_head);
	num_of_elements++;
	pool_list_head_set_num_of_elements(list_head, num_of_elements);

	MON_ASSERT(pool_list_head_get_last_element(list_head) == element);
}

static
void pool_insert_node_into_list(pool_list_head_t *list_head,
				pool_list_element_t *element)
{
	uint32_t num_of_elements;
	pool_list_element_t *curr_first_element;

	curr_first_element = pool_list_head_get_first_element(list_head);
	if (curr_first_element != NULL) {
		pool_list_element_set_prev(curr_first_element, element);
	} else {
		MON_ASSERT(pool_list_head_get_last_element(list_head) == NULL);
		pool_list_head_set_last_element(list_head, element);
	}

	pool_list_element_set_next(element, curr_first_element);
	pool_list_element_set_prev(element, NULL);
	pool_list_head_set_first_element(list_head, element);
	num_of_elements = pool_list_head_get_num_of_elements(list_head);
	num_of_elements++;
	pool_list_head_set_num_of_elements(list_head, num_of_elements);

	MON_ASSERT(pool_list_head_get_first_element(list_head) == element);
}

static
void pool_remove_element_from_the_list(pool_list_head_t *list_head,
				       pool_list_element_t *element)
{
	uint32_t num_of_elements;

	MON_ASSERT(!pool_is_list_empty(list_head));
	if (pool_list_head_get_first_element(list_head) == element) {
		pool_list_element_t *next_element = pool_list_element_get_next(
			element);
		if (next_element != NULL) {
			pool_list_element_set_prev(next_element, NULL);
		} else {
			MON_ASSERT(pool_list_head_get_last_element(
					list_head) == element);
			pool_list_head_set_last_element(list_head, NULL);
		}
		pool_list_head_set_first_element(list_head, next_element);
	} else if (pool_list_head_get_last_element(list_head) == element) {
		pool_list_element_t *prev_element = pool_list_element_get_prev(
			element);
		MON_ASSERT(prev_element != NULL);
		pool_list_element_set_next(prev_element, NULL);
		pool_list_head_set_last_element(list_head, prev_element);
	} else {
		pool_list_element_t *prev_element = pool_list_element_get_prev(
			element);
		pool_list_element_t *next_element = pool_list_element_get_next(
			element);

		MON_ASSERT(prev_element != NULL);
		MON_ASSERT(next_element != NULL);
		pool_list_element_set_next(prev_element, next_element);
		pool_list_element_set_prev(next_element, prev_element);
	}

	num_of_elements = pool_list_head_get_num_of_elements(list_head);
	num_of_elements--;
	pool_list_head_set_num_of_elements(list_head, num_of_elements);
}

static
void pool_free_nodes_in_page_from_list(pool_list_head_t *list_head,
				       uint32_t size_of_element, void *page)
{
	uint64_t page_addr = (uint64_t)page;
	uint32_t covered_size;
	uint32_t counter = 0;

	for (covered_size = 0;
	     covered_size < (PAGE_4KB_SIZE - size_of_element + 1);
	     covered_size += size_of_element) {
		uint64_t curr_element_addr = page_addr + covered_size;
		pool_list_element_t *element =
			(pool_list_element_t *)pool_uint64_to_ptr(
				curr_element_addr);

		pool_remove_element_from_the_list(list_head, element);

		counter++;
	}
	MON_ASSERT(counter == (PAGE_4KB_SIZE / size_of_element));
}

static
void pool_allocate_several_pages_from_heap(pool_t *pool)
{
	void *pages[POOL_MAX_NUM_OF_PAGES_TO_ALLOCATE];
	pool_list_head_t *free_pages_list = pool_get_free_pages_list(pool);
	uint32_t i;
	uint32_t requested_num_of_pages =
		pool_calculate_number_of_new_pages_to_allocate(pool);
	uint32_t num_of_pages;
	uint32_t num_of_allocated_pages;

	MON_ASSERT(requested_num_of_pages <= POOL_MAX_NUM_OF_PAGES_TO_ALLOCATE);
	num_of_pages = mon_page_alloc_scattered(requested_num_of_pages, pages);
	MON_ASSERT(num_of_pages <= requested_num_of_pages);

	for (i = 0; i < num_of_pages; i++) {
		void *curr_page = pages[i];

		pool_insert_node_into_list(free_pages_list,
			(pool_list_element_t *)curr_page);
	}

	num_of_allocated_pages = pool_get_num_of_allocated_pages(pool);
	num_of_allocated_pages += num_of_pages;
	pool_set_num_of_allocated_pages(pool, num_of_allocated_pages);
}

static
void pool_free_several_pages_into_heap(pool_t *pool,
				       uint32_t num_of_pages_to_free)
{
	pool_list_head_t *free_pages_list = pool_get_free_pages_list(pool);
	uint32_t num_of_freed_pages = 0;
	uint32_t num_of_allocated_pages;

	while ((!pool_is_list_empty(free_pages_list)) &&
	       (num_of_pages_to_free > 0)) {
		void *page = (void *)pool_allocate_node_from_list(
			free_pages_list);
		mon_page_free(page);
		num_of_freed_pages++;
		num_of_pages_to_free--;
	}

	num_of_allocated_pages = pool_get_num_of_allocated_pages(pool);
	MON_ASSERT(num_of_allocated_pages >= num_of_freed_pages);
	num_of_allocated_pages -= num_of_freed_pages;
	pool_set_num_of_allocated_pages(pool, num_of_allocated_pages);
}

static
boolean_t pool_is_power_of_2(uint32_t value)
{
	return (value > 1 && IS_POW_OF_2(value)) ? TRUE : FALSE;
}

static
void pool_split_page_to_elements(pool_list_head_t *list_head,
				 uint32_t size_of_element, void *page)
{
	uint32_t covered_size = size_of_element; /* start from the second element */
	uint32_t num_of_elements = PAGE_4KB_SIZE / size_of_element;
	uint64_t page_addr = (uint64_t)page;
	pool_list_element_t *first_element = (pool_list_element_t *)page;
	uint64_t last_element_addr;
	pool_list_element_t *last_element;
	uint32_t i;
	uint32_t num_of_elements_in_list;

	MON_ASSERT(num_of_elements > 0);

	pool_list_element_set_prev(first_element, NULL);
	for (i = 1; i < num_of_elements; i++) {
		uint64_t curr_element_addr = page_addr + covered_size;
		uint64_t prev_element_addr = curr_element_addr -
					     size_of_element;
		pool_list_element_t *curr_element =
			(pool_list_element_t *)pool_uint64_to_ptr(
				curr_element_addr);
		pool_list_element_t *prev_element =
			(pool_list_element_t *)pool_uint64_to_ptr(
				prev_element_addr);

		pool_list_element_set_next(prev_element, curr_element);
		pool_list_element_set_prev(curr_element, prev_element);

		covered_size += size_of_element;
	}

	last_element_addr = page_addr + covered_size - size_of_element;
	last_element = (pool_list_element_t *)pool_uint64_to_ptr(
		last_element_addr);

	if (pool_is_list_empty(list_head)) {
		pool_list_head_set_last_element(list_head, last_element);
	}
	pool_list_element_set_next(last_element,
		pool_list_head_get_first_element(list_head));
	pool_list_head_set_first_element(list_head, first_element);

	num_of_elements_in_list = pool_list_head_get_num_of_elements(list_head);
	num_of_elements_in_list += num_of_elements;
	pool_list_head_set_num_of_elements(list_head, num_of_elements_in_list);
}

static
uint32_t pool_hash_func(uint64_t key, uint32_t size)
{
	uint32_t hash_mask = (size - 1);
	uint64_t index_tmp;

	MON_ASSERT(pool_is_power_of_2(size));
	MON_ASSERT(ALIGN_BACKWARD(key, PAGE_4KB_SIZE) == key);

	index_tmp = ((key >> 12) & hash_mask);
	return (uint32_t)index_tmp;
}

static
void pool_allocate_single_page_from_heap_with_must_succeed(pool_t *pool)
{
	void *page;
	uint32_t num_of_allocated_pages;
	pool_list_head_t *free_pages_list = pool_get_free_pages_list(pool);

	page =
		mon_memory_alloc_must_succeed(pool_get_must_succeed_alloc_handle(
				pool),
			PAGE_4KB_SIZE);
	if (page == NULL) {
		MON_ASSERT(0);
		return;
	}

	pool_insert_node_into_list(free_pages_list,
		(pool_list_element_t *)page);

	num_of_allocated_pages = pool_get_num_of_allocated_pages(pool);
	num_of_allocated_pages++;
	pool_set_num_of_allocated_pages(pool, num_of_allocated_pages);
}

static
void pool_try_to_free_unused_page_from_elements_list(pool_t *pool,
						     boolean_t full_clean)
{
	pool_list_head_t *free_pages_list = pool_get_free_pages_list(pool);
	pool_list_head_t *free_pool_elements_list =
		pool_get_free_pool_elements_list(pool);
	hash64_handle_t hash = pool_get_hash(pool);
	pool_list_element_t *node;
	uint32_t size_of_element = pool_get_size_of_single_element(pool);
	uint32_t num_of_elements_per_page = pool_get_num_of_elements_per_page(
		pool);

	node = pool_list_head_get_last_element(free_pool_elements_list);
	while (node != NULL) {
		pool_list_element_t *prev_node =
			pool_list_element_get_prev(node);
		uint64_t node_addr = pool_ptr_to_uint64(node);
		uint64_t num_of_elements;
		boolean_t res;

		if (ALIGN_BACKWARD(node_addr, PAGE_4KB_SIZE) != node_addr) {
			/* arrived to non aligned elements; */
			break;
		}

		res =
			(boolean_t)hash64_lookup(hash,
				node_addr,
				&num_of_elements);
		MON_ASSERT(res);

		if (num_of_elements == num_of_elements_per_page) {
			res = (boolean_t)hash64_remove(hash, node_addr);
			MON_ASSERT(res);

			pool_free_nodes_in_page_from_list(
				free_pool_elements_list,
				size_of_element,
				node);
			pool_insert_node_into_list(free_pages_list, node);

			pool_dec_num_of_pages_used_for_pool_elements(pool);

			if (!full_clean) {
				break;
			}
		}

		node = prev_node;
	}
}

static
void *pool_allocate_hash_node(void *context)
{
	pool_t *pool = (pool_t *)context;
	hash64_handle_t hash = pool_get_hash(pool);
	pool_list_head_t *hash_elements_list_head =
		pool_get_free_hash_elements_list(pool);

	if (pool_get_hash_element_to_allocate(pool) != NULL) {
		pool_list_element_t *free_element =
			pool_get_hash_element_to_allocate(pool);
		pool_set_hash_element_to_allocate(pool, NULL);
		return free_element;
	}

	if (!pool_is_list_empty(hash_elements_list_head)) {
		pool_list_element_t *free_element;
		uint64_t free_element_u64;
		uint64_t page_addr;
		uint64_t element_counter;
		boolean_t res;

		free_element = pool_allocate_node_from_list(
			hash_elements_list_head);
		MON_ASSERT(free_element != NULL);
		free_element_u64 = (uint64_t)free_element;
		page_addr = ALIGN_BACKWARD(free_element_u64, PAGE_4KB_SIZE);

		res = hash64_lookup(hash, page_addr, &element_counter);
		MON_ASSERT(res);

		MON_ASSERT(element_counter > 0);
		element_counter--;

		if (!hash64_update(hash, page_addr, element_counter)) {
			MON_ASSERT(0);
			return NULL;
		}

		return free_element;
	} else {
		/* Pool is empty */
		pool_list_head_t *free_pages_list_head =
			pool_get_free_pages_list(pool);
		void *free_page = NULL;
		uint64_t free_page_addr;
		uint32_t num_of_hash_elements;
		uint32_t size_of_hash_element;
		pool_list_element_t *free_element;
		uint64_t element_counter_tmp;

		if (pool_is_list_empty(free_pages_list_head)) {
			pool_try_to_free_unused_page_from_elements_list(pool,
				FALSE);

			if (pool_is_list_empty(free_pages_list_head)) {
				pool_allocate_single_page_from_heap(pool);

				if (pool_is_list_empty(free_pages_list_head)) {
					if (!pool_is_must_succeed_allocation(
						    pool)) {
						return NULL;
					}

					pool_allocate_single_page_from_heap_with_must_succeed(
						pool);
					if (pool_is_list_empty(
						    free_pages_list_head)) {
						MON_ASSERT(0);
						return NULL;
					}
				}
			}
		}

		free_page = pool_allocate_node_from_list(free_pages_list_head);
		pool_inc_num_of_pages_used_for_hash_nodes(pool);
		MON_ASSERT(free_page != NULL);

		free_page_addr = (uint64_t)free_page;
		MON_ASSERT(ALIGN_BACKWARD(free_page_addr, PAGE_4KB_SIZE) ==
			free_page_addr);

		MON_ASSERT(!hash64_lookup(hash, free_page_addr,
				&element_counter_tmp));

		size_of_hash_element = pool_get_size_of_hash_element(pool);
		pool_split_page_to_elements(hash_elements_list_head,
			size_of_hash_element, free_page);
		MON_ASSERT(!pool_is_list_empty(hash_elements_list_head));

		/* number of elements currently in the list */
		num_of_hash_elements =
			pool_list_head_get_num_of_elements(
				hash_elements_list_head);
		MON_ASSERT(num_of_hash_elements > 2);

		/* Allocating 2 elements: first in order to record current page and
		 * second in order to return the requested element */

		num_of_hash_elements -= 2;

		/* Allocate element for allocated page */
		free_element = pool_allocate_node_from_list(
			hash_elements_list_head);

		/* Must be first element of the page */
		MON_ASSERT((uint64_t)free_element == free_page_addr);

		/* Cache this element for the following insert into hash */
		pool_set_hash_element_to_allocate(pool, free_element);

		/* Record the page */
		if (!hash64_insert(hash, free_page_addr,
			    num_of_hash_elements)) {
			MON_ASSERT(0); /* should not be here */
			return NULL;
		}

		MON_ASSERT(pool_get_hash_element_to_allocate(pool) == NULL);

		/* Allocate the requested element */
		free_element = pool_allocate_node_from_list(
			hash_elements_list_head);

		MON_ASSERT(pool_is_allocation_counters_ok(pool));

		return free_element;
	}
}

static
void pool_free_hash_node(void *context, void *element)
{
	pool_t *pool = (pool_t *)context;
	hash64_handle_t hash = pool_get_hash(pool);
	pool_list_head_t *hash_elements_list_head =
		pool_get_free_hash_elements_list(pool);
	pool_list_element_t *hash_element = (pool_list_element_t *)element;
	uint64_t num_of_elements = 0;
	uint64_t element_addr = pool_ptr_to_uint64(element);
	uint64_t page_addr = ALIGN_BACKWARD(element_addr, PAGE_4KB_SIZE);
	boolean_t res;

	if (page_addr == element_addr) {
		/* The first node always describes the page of hash nodes */
		uint64_t num_of_elements_tmp;
		/* insert first node in the page to the tail */
		pool_insert_node_into_list_at_tail(hash_elements_list_head,
			hash_element);

		/* Make sure that hash doesn't contain node for the page */
		MON_ASSERT(!hash64_lookup(hash, page_addr,
				&num_of_elements_tmp));
		return;
	}

	pool_insert_node_into_list(hash_elements_list_head, hash_element);

	res = (boolean_t)hash64_lookup(hash, page_addr, &num_of_elements);
	MON_ASSERT(res);

	num_of_elements++;

	if (num_of_elements <
	    (pool_get_num_of_hash_elements_per_page(pool) - 1)) {
		res = hash64_update(hash, page_addr, num_of_elements);
		MON_ASSERT(res);
	} else {
		void *page = pool_uint64_to_ptr(page_addr);

		res = hash64_remove(hash, page_addr);
		MON_ASSERT(res);

		pool_free_nodes_in_page_from_list(pool_get_free_hash_elements_list
			(
				pool),
			pool_get_size_of_hash_element(pool),
			page);
		pool_insert_node_into_list(pool_get_free_pages_list(pool),
			page);
		pool_dec_num_of_pages_used_for_hash_nodes(pool);
	}

	MON_ASSERT(pool_is_allocation_counters_ok(pool));
}

static
uint32_t pool_get_num_of_pages_to_free_to_heap(pool_t *pool)
{
	uint32_t num_of_allocated_pages = pool_get_num_of_allocated_pages(pool);
	pool_list_head_t *free_pages_list = pool_get_free_pages_list(pool);
	uint32_t num_of_free_pages =
		pool_list_head_get_num_of_elements(free_pages_list);
	uint32_t num_of_pages_to_free = 0;

	if ((num_of_free_pages >=
	     (num_of_allocated_pages / POOL_PAGES_TO_FREE_THRESHOLD))
	    && (num_of_allocated_pages >= POOL_MIN_NUMBER_OF_FREE_PAGES)) {
		num_of_pages_to_free =
			(num_of_allocated_pages / POOL_PAGES_TO_FREE_THRESHOLD);
	}

	if ((num_of_free_pages - num_of_pages_to_free) >
	    POOL_MAX_NUM_OF_PAGES_TO_ALLOCATE) {
		num_of_pages_to_free =
			num_of_free_pages - POOL_MAX_NUM_OF_PAGES_TO_ALLOCATE;
	}

	return num_of_pages_to_free;
}

static
void pool_report_alloc_free_op(pool_t *pool)
{
	pool_inc_alloc_free_ops_counter(pool);
	if (pool_get_alloc_free_ops_counter(pool) >=
	    POOL_FREE_UNUSED_PAGES_THRESHOLD) {
		uint32_t num_of_pages_to_free;

		pool_clear_alloc_free_ops_counter(pool);

		MON_ASSERT(pool_is_allocation_counters_ok(pool));
		pool_try_to_free_unused_page_from_elements_list(pool, FALSE);
		MON_ASSERT(pool_is_allocation_counters_ok(pool));

		num_of_pages_to_free = pool_get_num_of_pages_to_free_to_heap(
			pool);
		if (num_of_pages_to_free > 0) {
			pool_free_several_pages_into_heap(pool,
				num_of_pages_to_free);
		}
		MON_ASSERT(pool_is_allocation_counters_ok(pool));
	}
}

static
void *pool_allocate_internal(pool_t *pool)
{
	hash64_handle_t hash = pool_get_hash(pool);
	pool_list_head_t *free_pool_elements_list =
		pool_get_free_pool_elements_list(pool);
	pool_list_head_t *free_pages_list;
	void *page;
	uint64_t page_addr;
	void *element;
	uint64_t num_of_elements;
	boolean_t res;
	uint64_t num_of_elements_tmp;

	MON_ASSERT(pool_is_allocation_counters_ok(pool));

	pool_report_alloc_free_op(pool);

	if (!pool_is_list_empty(free_pool_elements_list)) {
		uint64_t elem_addr;

		element = (void *)pool_allocate_node_from_list(
			free_pool_elements_list);
		MON_ASSERT(element != NULL);
		elem_addr = (uint64_t)element;
		page_addr = ALIGN_BACKWARD(elem_addr, PAGE_4KB_SIZE);

		res =
			(boolean_t)hash64_lookup(hash,
				page_addr,
				&num_of_elements);
		MON_ASSERT(res);
		MON_ASSERT(num_of_elements > 0);
		num_of_elements--;
		res =
			(boolean_t)hash64_update(hash,
				page_addr,
				num_of_elements);
		MON_ASSERT(res);
		pool_inc_num_of_allocated_elements(pool);
		MON_ASSERT(pool_is_allocation_counters_ok(pool));
		return element;
	}

	/* elements bigger than page are not supported yet */
	MON_ASSERT(pool_get_num_of_elements_per_page(pool) > 0);
	free_pages_list = pool_get_free_pages_list(pool);
	if (pool_is_list_empty(free_pages_list)) {
		pool_allocate_several_pages_from_heap(pool);
	}

	if (pool_is_list_empty(free_pages_list)) {
		if (!pool_is_must_succeed_allocation(pool)) {
			MON_ASSERT(pool_is_allocation_counters_ok(pool));
			return NULL;
		}

		pool_allocate_single_page_from_heap_with_must_succeed(pool);

		if (pool_is_list_empty(free_pages_list)) {
			MON_ASSERT(0);
			return NULL;
		}
	}

	page = (void *)pool_allocate_node_from_list(free_pages_list);
	pool_inc_num_of_pages_used_for_pool_elements(pool);

	page_addr = (uint64_t)page;
	MON_ASSERT(page != NULL);
	MON_ASSERT(ALIGN_BACKWARD(page_addr, PAGE_4KB_SIZE) == page_addr);

	pool_split_page_to_elements(free_pool_elements_list,
		pool_get_size_of_single_element(pool), page);
	MON_ASSERT(!pool_is_list_empty(free_pool_elements_list));

	MON_ASSERT(pool_list_head_get_num_of_elements(
			free_pool_elements_list) > 0);
	element = (void *)pool_allocate_node_from_list(free_pool_elements_list);
	MON_ASSERT(element != NULL);
	MON_ASSERT(pool_ptr_to_uint64(element) == page_addr);

	num_of_elements =
		pool_list_head_get_num_of_elements(free_pool_elements_list);
	MON_ASSERT(num_of_elements < pool_get_num_of_elements_per_page(pool));
	MON_ASSERT(!hash64_lookup(hash, page_addr, &num_of_elements_tmp));
	MON_ASSERT(pool_is_allocation_counters_ok(pool));
	res = hash64_insert(hash, page_addr, num_of_elements);

	if (!res) {
		MON_ASSERT(pool_is_allocation_counters_ok(pool));
		/* Failed to insert page into hash */
		pool_insert_node_into_list(free_pool_elements_list, element);
		pool_free_nodes_in_page_from_list(free_pool_elements_list,
			pool_get_size_of_single_element(pool),
			page);
		pool_dec_num_of_pages_used_for_pool_elements(pool);
		MON_DEBUG_CODE(mon_zeromem((void *)page, PAGE_4KB_SIZE));
		pool_insert_node_into_list(free_pages_list,
			(pool_list_element_t *)page);
		MON_ASSERT(pool_is_allocation_counters_ok(pool));
		return NULL;
	}

	if (hash64_get_num_of_elements(hash) >=
	    (pool_get_current_hash_size(pool) * POOL_REHASH_THRESHOLD)) {
		uint32_t new_size =
			(pool_get_current_hash_size(pool) *
			 POOL_REHASH_THRESHOLD);
		boolean_t res = hash64_change_size_and_rehash(hash, new_size);

		if (res) {
			pool_set_current_hash_size(pool, new_size);
			MON_LOG(mask_anonymous,
				level_trace,
				"POOL: Changed size of pool's hash to %d\n",
				new_size);
		} else {
			MON_LOG(mask_anonymous,
				level_trace,
				"POOL: Failed to change size of pool's hash to %d\n",
				new_size);
		}
	}

	pool_inc_num_of_allocated_elements(pool);
	MON_ASSERT(pool_is_allocation_counters_ok(pool));
	return element;
}

pool_handle_t pool_create_internal(uint32_t size_of_single_element,
				   boolean_t mutex_flag)
{
	pool_t *pool = mon_memory_alloc(sizeof(pool_t));
	uint32_t final_size_of_single_element;
	uint32_t size_of_hash_element;
	uint32_t final_size_of_hash_element;
	hash64_handle_t hash;

	if (pool == NULL) {
		return POOL_INVALID_HANDLE;
	}

	pool_init_list_head(pool_get_free_hash_elements_list(pool));
	pool_init_list_head(pool_get_free_pool_elements_list(pool));
	pool_init_list_head(pool_get_free_pages_list(pool));
	pool_set_hash_element_to_allocate(pool, NULL);

	final_size_of_single_element =
		(size_of_single_element >=
		 sizeof(pool_list_element_t)) ? size_of_single_element :
		sizeof(pool_list_element_t);
	pool_set_size_of_single_element(pool, final_size_of_single_element);
	pool_set_num_of_elements_per_page(pool,
		PAGE_4KB_SIZE /
		final_size_of_single_element);

	size_of_hash_element = hash64_get_node_size();
	final_size_of_hash_element =
		(size_of_hash_element >=
		 sizeof(pool_list_element_t)) ? size_of_hash_element :
		sizeof(pool_list_element_t);
	pool_set_size_of_hash_element(pool, final_size_of_hash_element);
	pool_set_num_of_hash_elements_per_page(pool,
		PAGE_4KB_SIZE /
		final_size_of_hash_element);

	hash = hash64_create_hash(pool_hash_func,
		NULL,
		NULL,
		pool_allocate_hash_node,
		pool_free_hash_node,
		pool, POOL_HASH_NUM_OF_CELLS);
	if (hash == HASH64_INVALID_HANDLE) {
		mon_memory_free(pool);
		pool = (pool_t *)POOL_INVALID_HANDLE;
	} else {
		pool->mutex_flag = mutex_flag;;
		if (mutex_flag) {
			lock_initialize(&pool->access_lock);
		}

		pool_set_num_of_allocated_pages(pool, 0);

		pool_clear_num_of_allocated_elements(pool);
		pool_clear_num_of_pages_used_for_hash_nodes(pool);
		pool_clear_num_of_pages_used_for_pool_elements(pool);

		pool_set_hash(pool, hash);
		pool_set_current_hash_size(pool, POOL_HASH_NUM_OF_CELLS);

		pool_set_must_succeed_alloc_handle(pool,
			HEAP_INVALID_ALLOC_HANDLE);
		pool_clear_must_succeed_allocation(pool);
		pool_clear_alloc_free_ops_counter(pool);

		MON_ASSERT(pool_is_allocation_counters_ok(pool));
	}

	return (pool_handle_t)pool;
}

/*----------------------------------------------*/

/* Create pool with no by mutual exclussion guard. */
pool_handle_t assync_pool_create(uint32_t size_of_single_element)
{
	return pool_create_internal(size_of_single_element, FALSE);
}

void *pool_allocate(pool_handle_t pool_handle)
{
	pool_t *pool = (pool_t *)pool_handle;
	void *tmp;

	MON_ASSERT(pool != NULL);

	if (pool == NULL) {
		return NULL;
	}

	POOL_AQUIRE_LOCK(pool);

	tmp = pool_allocate_internal(pool);

	POOL_RELEASE_LOCK(pool);

	return tmp;
}

void pool_free(pool_handle_t pool_handle, void *data)
{
	pool_t *pool = (pool_t *)pool_handle;
	pool_list_element_t *element = (pool_list_element_t *)data;
	uint64_t element_addr = (uint64_t)element;
	uint64_t page_addr = ALIGN_BACKWARD(element_addr, PAGE_4KB_SIZE);
	pool_list_head_t *free_elements_list;
	hash64_handle_t hash;
	boolean_t res;
	uint64_t num_of_elements;

	MON_ASSERT(pool != NULL);

	if (pool == NULL) {
		return;
	}

	POOL_AQUIRE_LOCK(pool);

	free_elements_list = pool_get_free_pool_elements_list(pool);
	hash = pool_get_hash(pool);

	MON_ASSERT(pool_is_allocation_counters_ok(pool));

	pool_dec_num_of_allocated_elements(pool);
	MON_ASSERT(pool != NULL);

	if (element_addr == page_addr) {
		pool_insert_node_into_list_at_tail(free_elements_list, element);
	} else {
		pool_insert_node_into_list(free_elements_list, element);
	}

	MON_ASSERT(pool_is_allocation_counters_ok(pool));
	MON_ASSERT(!pool_is_list_empty(free_elements_list));

	res = (boolean_t)hash64_lookup(hash, page_addr, &num_of_elements);
	MON_ASSERT(res);

	num_of_elements++;

	res = (boolean_t)hash64_update(hash, page_addr, num_of_elements);
	MON_ASSERT(res);

	/* if (pool_must_free_pages(pool)) {
	 * pool_free_several_pages_into_heap(pool); } */

	MON_ASSERT(pool_is_allocation_counters_ok(pool));

	pool_report_alloc_free_op(pool);

	POOL_RELEASE_LOCK(pool);
}

