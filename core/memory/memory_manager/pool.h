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

#ifndef POOL_H
#define POOL_H

#include <mon_defs.h>
#include <pool_api.h>
#include <hash64_api.h>
#include <lock.h>

typedef struct pool_list_element_t {
	struct pool_list_element_t *prev;
	struct pool_list_element_t *next;
} pool_list_element_t;

/* pool_list_element_t* pool_list_element_get_prev(pool_list_element_t* element) */
#define pool_list_element_get_prev(element_) (element_->prev)

/* void pool_list_element_set_prev(pool_list_element_t* element,
 * pool_list_element_t* prev) */
#define pool_list_element_set_prev(element_, prev_) { element_->prev = prev_; }

/* pool_list_element_t* pool_list_element_get_next(pool_list_element_t* element) */
#define pool_list_element_get_next(element_) (element_->next)

/* void pool_list_element_set_next(pool_list_element_t* element,
 * pool_list_element_t* next) */
#define pool_list_element_set_next(element_, next_) { element_->next = next_; }

/*----------------------------------------------------------*/
typedef struct {
	pool_list_element_t	*first_element;
	pool_list_element_t	*last_element;
	uint32_t		num_of_elements;
	uint32_t		padding; /* not for use */
} pool_list_head_t;

/* pool_list_element_t* pool_list_head_get_first_element(const pool_list_head_t*
 * list_head) */
#define pool_list_head_get_first_element(list_head_) (list_head_->first_element)

/* void pool_list_head_set_first_element (pool_list_head_t* list_head,
 * pool_list_element_t* first_element) */
#define pool_list_head_set_first_element(list_head_, first_element_) \
	{ list_head_->first_element = first_element_; }

/* pool_list_element_t* pool_list_head_get_last_element(const pool_list_head_t*
 * list_head) */
#define pool_list_head_get_last_element(list_head_) (list_head_->last_element)

/* void pool_list_head_set_last_element (pool_list_head_t* list_head,
 * pool_list_element_t* last_element) */
#define pool_list_head_set_last_element(list_head_, last_element_) \
	{ list_head_->last_element = last_element_; }

/* uint32_t pool_list_head_get_num_of_elements(const pool_list_head_t* list_head) */
#define pool_list_head_get_num_of_elements(list_head_) \
	(list_head_->num_of_elements)

/* void pool_list_head_set_num_of_elements (pool_list_head_t* list_head, uint32_t
 * num_of_elements) */
#define pool_list_head_set_num_of_elements(list_head_, num_of_elements_) \
	{ list_head_->num_of_elements = num_of_elements_; }

/*----------------------------------------------------------*/
typedef struct {
	pool_list_head_t	free_hash_elements;
	pool_list_head_t	free_pool_elements;
	pool_list_head_t	free_pages;
	pool_list_element_t	*hash_element_to_allocate;
	hash64_handle_t		hash;
	heap_alloc_handle_t	must_succeed_alloc_handle;
	uint32_t		size_of_single_element;
	uint32_t		num_of_elements_per_page;
	uint32_t		size_of_hash_element;
	uint32_t		num_of_hash_elements_per_page;
	uint32_t		num_of_allocated_pages;
	uint32_t		current_hash_size;
	boolean_t		must_succeed_allocation;
	uint32_t		alloc_free_ops_counter;
	uint32_t		num_of_allocated_elements;
	uint32_t		num_of_pages_used_for_hash_nodes;
	uint32_t		num_of_pages_used_for_pool_elements;
	mon_lock_t		access_lock;
	boolean_t		mutex_flag;
	uint32_t		pad0;
} pool_t;

/* pool_list_head_t* pool_get_free_hash_elements_list(pool_t* pool) */
#define pool_get_free_hash_elements_list(pool_) (&(pool_->free_hash_elements))

/* pool_list_head_t* pool_get_free_pool_elements_list(pool_t* pool) */
#define pool_get_free_pool_elements_list(pool_) (&(pool_->free_pool_elements))

/* pool_list_head_t* pool_get_free_pages_list(pool_t* pool) */
#define pool_get_free_pages_list(pool_) (&(pool_->free_pages))

/* uint32_t pool_get_size_of_single_element(const pool_t* pool) */
#define pool_get_size_of_single_element(pool_) (pool_->size_of_single_element)

/* void pool_set_size_of_single_element(pool_t* pool, uint32_t size) */
#define pool_set_size_of_single_element(pool_, size_) \
	{ pool_->size_of_single_element = size_; }

/* pool_list_element_t* pool_get_hash_element_to_allocate(pool_t* pool) */
#define pool_get_hash_element_to_allocate(pool_) \
	(pool_->hash_element_to_allocate)

/* void pool_set_hash_element_to_allocate(pool_t* pool, pool_list_element_t*
 * element) */
#define pool_set_hash_element_to_allocate(pool_, element_) \
	{ pool_->hash_element_to_allocate = element_; }

/* uint32_t pool_get_num_of_elements_per_page(const pool_t* pool) */
#define pool_get_num_of_elements_per_page(pool_) \
	(pool_->num_of_elements_per_page)

/* void pool_set_num_of_elements_per_page(pool_t* pool, uint32_t num) */
#define pool_set_num_of_elements_per_page(pool_, num_) \
	{ pool_->num_of_elements_per_page = num_; }

/* uint32_t pool_get_size_of_hash_element(const pool_t* pool) */
#define pool_get_size_of_hash_element(pool_) (pool_->size_of_hash_element)

/* void pool_set_size_of_hash_element(pool_t* pool, uint32_t size) */
#define pool_set_size_of_hash_element(pool_, size_) \
	{ pool_->size_of_hash_element = size_; }

/* uint32_t pool_get_num_of_hash_elements_per_page(const pool_t* pool) */
#define pool_get_num_of_hash_elements_per_page(pool_) \
	(pool_->num_of_hash_elements_per_page)

/* void pool_set_num_of_hash_elements_per_page(pool_t* pool, uint32_t num) */
#define pool_set_num_of_hash_elements_per_page(pool_, num_) \
	{ pool_->num_of_hash_elements_per_page = num_; }

/* hash64_handle_t pool_get_hash(const pool_t* pool) */
#define pool_get_hash(pool_) (pool_->hash)

/* void pool_set_hash(pool_t* pool, hash64_handle_t hash) */
#define pool_set_hash(pool_, hash_) { pool_->hash = hash_; }

/* uint32_t pool_get_current_hash_size(const pool_t* pool) */
#define pool_get_current_hash_size(pool_) (pool_->current_hash_size)

/* void pool_set_current_hash_size(pool_t* pool, uint32_t new_size) */
#define pool_set_current_hash_size(pool_, new_size_) \
	{ pool_->current_hash_size = new_size_; }

/* uint32_t pool_get_num_of_allocated_pages(const pool_t* pool) */
#define pool_get_num_of_allocated_pages(pool_) (pool_->num_of_allocated_pages)

/* void pool_set_num_of_allocated_pages(pool_t* pool, uint32_t num) */
#define pool_set_num_of_allocated_pages(pool_, num_) \
	{ pool_->num_of_allocated_pages = num_; }

/* heap_alloc_handle_t pool_get_must_succeed_alloc_handle(const pool_t* pool) */
#define pool_get_must_succeed_alloc_handle(pool_) \
	(pool_->must_succeed_alloc_handle)

/* void pool_set_must_succeed_alloc_handle(pool_t* pool, heap_alloc_handle_t
 * handle) */
#define pool_set_must_succeed_alloc_handle(pool_, handle_) \
	{ pool_->must_succeed_alloc_handle = handle_; }

/* boolean_t pool_is_must_succeed_allocation(const pool_t* pool) */
#define pool_is_must_succeed_allocation(pool_) (pool_->must_succeed_allocation)

/* void pool_set_must_succeed_allocation(pool_t* pool) */
#define pool_set_must_succeed_allocation(pool_) \
	{ pool_->must_succeed_allocation = TRUE; }

/* void pool_clear_must_succeed_allocation(pool_t* pool) */
#define pool_clear_must_succeed_allocation(pool_) \
	{ pool_->must_succeed_allocation = FALSE; }

/* uint32_t pool_get_alloc_free_ops_counter(const pool_t* pool) */
#define pool_get_alloc_free_ops_counter(pool_) (pool_->alloc_free_ops_counter)

/* void pool_clear_alloc_free_ops_counter(pool_t* pool) */
#define pool_clear_alloc_free_ops_counter(pool_) \
	{ pool_->alloc_free_ops_counter = 0; }

/* void pool_inc_alloc_free_ops_counter(pool_t* pool) */
#define pool_inc_alloc_free_ops_counter(pool_) \
	{ pool_->alloc_free_ops_counter += 1; }

/* uint32_t pool_get_num_of_allocated_elements(const pool_t* pool) */
#define pool_get_num_of_allocated_elements(pool_) \
	(pool_->num_of_allocated_elements)

/* void pool_clear_num_of_allocated_elements(pool_t* pool) */
#define pool_clear_num_of_allocated_elements(pool_) \
	{ pool_->num_of_allocated_elements = 0; }

/* void pool_inc_num_of_allocated_elements(pool_t* pool) */
#define pool_inc_num_of_allocated_elements(pool_) \
	{ pool_->num_of_allocated_elements += 1; }

/* void pool_dec_num_of_allocated_elements(pool_t* pool) */
#define pool_dec_num_of_allocated_elements(pool_) \
	{ pool_->num_of_allocated_elements -= 1; }

/* uint32_t pool_get_num_of_pages_used_for_hash_nodes(pool_t* pool) */
#define pool_get_num_of_pages_used_for_hash_nodes(pool_) \
	(pool_->num_of_pages_used_for_hash_nodes)

/* void pool_clear_num_of_pages_used_for_hash_nodes(pool_t* pool) */
#define pool_clear_num_of_pages_used_for_hash_nodes(pool_) \
	{ pool_->num_of_pages_used_for_hash_nodes = 0; }

/* void pool_inc_num_of_pages_used_for_hash_nodes(pool_t* pool) */
#define pool_inc_num_of_pages_used_for_hash_nodes(pool_) \
	{ pool_->num_of_pages_used_for_hash_nodes += 1; }

/* void pool_dec_num_of_pages_used_for_hash_nodes(pool_t* pool) */
#define pool_dec_num_of_pages_used_for_hash_nodes(pool_) \
	{ pool_->num_of_pages_used_for_hash_nodes -= 1; }

/* uint32_t pool_get_num_of_pages_used_for_pool_elements(pool_t* pool) */
#define pool_get_num_of_pages_used_for_pool_elements(pool_) \
	(pool_->num_of_pages_used_for_pool_elements)

/* void pool_clear_num_of_pages_used_for_pool_elements(pool_t* pool) */
#define pool_clear_num_of_pages_used_for_pool_elements(pool_) \
	{ pool_->num_of_pages_used_for_pool_elements = 0; }

/* void pool_inc_num_of_pages_used_for_pool_elements(pool_t* pool) */
#define pool_inc_num_of_pages_used_for_pool_elements(pool_) \
	{ pool_->num_of_pages_used_for_pool_elements += 1; }

/* void pool_dec_num_of_pages_used_for_pool_elements(pool_t* pool) */
#define pool_dec_num_of_pages_used_for_pool_elements(pool_) \
	{ pool_->num_of_pages_used_for_pool_elements -= 1; }

/*----------------------------------------------------------*/

#define POOL_HASH_NUM_OF_CELLS 512
#define POOL_PAGES_TO_FREE_THRESHOLD 4
#define POOL_PAGES_TO_KEEP_THRESHOLD 4
#define POOL_PAGES_TO_ALLOCATE_THRESHOLD 8
#define POOL_MAX_NUM_OF_PAGES_TO_ALLOCATE 100
#define POOL_MIN_NUMBER_OF_FREE_PAGES 4
#define POOL_MIN_NUMBER_OF_PAGES_TO_FREE 6
#define POOL_REHASH_THRESHOLD 4
#define POOL_FREE_UNUSED_PAGES_THRESHOLD 5000

#endif
