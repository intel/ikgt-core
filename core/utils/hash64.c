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

#include <mon_defs.h>
#include <heap.h>
#include <hash64_api.h>
#include <common_libc.h>
#include <mon_dbg.h>
#include "hash64.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(HASH64_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(HASH64_C, __condition)

INLINE void *hash64_uint64_to_ptr(uint64_t value)
{
	return (void *)(value);
}

INLINE uint64_t hash64_ptr_to_uint64(void *ptr)
{
	return (uint64_t)ptr;
}

INLINE void *hash64_allocate_node(hash64_table_t *hash)
{
	func_hash64_node_allocation_t node_alloc_func =
		hash64_get_node_alloc_func(hash);
	void *context = hash64_get_allocation_deallocation_context(hash);

	return node_alloc_func(context);
}

INLINE void hash64_free_node(hash64_table_t *hash, void *data)
{
	func_hash64_node_deallocation_t node_dealloc_func =
		hash64_get_node_dealloc_func(hash);
	void *context = hash64_get_allocation_deallocation_context(hash);

	node_dealloc_func(context, data);
}

INLINE void *hash64_mem_alloc(hash64_table_t *hash, uint32_t size)
{
	func_hash64_internal_mem_allocation_t mem_alloc_func =
		hash64_get_mem_alloc_func(hash);

	if (mem_alloc_func == NULL) {
		return mon_memory_alloc(size);
	} else {
		return mem_alloc_func(size);
	}
}

INLINE void hash64_mem_free(hash64_table_t *hash, void *data)
{
	func_hash64_internal_mem_deallocation_t mem_dealloc_func =
		hash64_get_mem_dealloc_func(hash);

	if (mem_dealloc_func == NULL) {
		mon_memory_free(data);
	} else {
		mem_dealloc_func(data);
	}
}

static
hash64_node_t **hash64_retrieve_appropriate_array_cell(hash64_table_t *hash,
						       uint64_t key)
{
	func_hash64_t hash_func;
	uint32_t cell_index;
	hash64_node_t **array;

	hash_func = hash64_get_hash_func(hash);
	cell_index = hash_func(key, hash64_get_hash_size(hash));
	array = hash64_get_array(hash);
	return &(array[cell_index]);
}

static
hash64_node_t *hash64_find(hash64_table_t *hash, uint64_t key)
{
	hash64_node_t **cell;
	hash64_node_t *node;

	cell = hash64_retrieve_appropriate_array_cell(hash, key);
	node = *cell;

	while (node != NULL) {
		if (hash64_node_get_key(node) == key) {
			break;
		}
		node = hash64_node_get_next(node);
	}
	return node;
}

static
boolean_t hash64_insert_internal(hash64_table_t *hash,
				 uint64_t key,
				 uint64_t value, boolean_t update_when_found)
{
	hash64_node_t *node = NULL;

	if (update_when_found) {
		node = hash64_find(hash, key);
	} else {
		MON_ASSERT(hash64_find(hash, key) == NULL);
	}

	if (node == NULL) {
		hash64_node_t **cell;

		node = hash64_allocate_node(hash);
		if (node == NULL) {
			return FALSE;
		}
		cell = hash64_retrieve_appropriate_array_cell(hash, key);

		hash64_node_set_next(node, *cell);
		*cell = node;

		hash64_node_set_key(node, key);

		hash64_inc_element_count(hash);
	} else {
		MON_ASSERT(hash64_node_get_key(node) == key);
	}

	hash64_node_set_value(node, value);

	MON_ASSERT(hash64_find(hash, key) != NULL);

	return TRUE;
}

static
hash64_handle_t hash64_create_hash_internal(func_hash64_t hash_func,
					    func_hash64_internal_mem_allocation_t mem_alloc_func,
					    func_hash64_internal_mem_deallocation_t mem_dealloc_func,
					    func_hash64_node_allocation_t node_alloc_func,
					    func_hash64_node_deallocation_t node_dealloc_func,
					    void *node_allocation_deallocation_context,
					    uint32_t hash_size,
					    boolean_t is_multiple_values_hash)
{
	hash64_table_t *hash;
	hash64_node_t **array;
	uint32_t index;

	if (mem_alloc_func == NULL) {
		hash =
			(hash64_table_t *)mon_memory_alloc(
				sizeof(hash64_table_t));
	} else {
		hash = (hash64_table_t *)mem_alloc_func(sizeof(hash64_table_t));
	}

	if (hash == NULL) {
		goto hash_allocation_failed;
	}

	if (mem_alloc_func == NULL) {
		array =
			(hash64_node_t **)mon_memory_alloc(sizeof(hash64_node_t
								  *) *
				hash_size);
	} else {
		array =
			(hash64_node_t **)mem_alloc_func(
				sizeof(hash64_node_t *) * hash_size);
	}

	if (array == NULL) {
		goto array_allocation_failed;
	}
	for (index = 0; index < hash_size; index++)
		array[index] = NULL;

	MON_ASSERT(node_alloc_func != NULL);
	MON_ASSERT(node_dealloc_func != NULL);

	hash64_set_hash_size(hash, hash_size);
	hash64_set_array(hash, array);
	MON_ASSERT(hash_func != NULL);
	hash64_set_hash_func(hash, hash_func);
	hash64_set_mem_alloc_func(hash, mem_alloc_func);
	hash64_set_mem_dealloc_func(hash, mem_dealloc_func);
	hash64_set_node_alloc_func(hash, node_alloc_func);
	hash64_set_node_dealloc_func(hash, node_dealloc_func);
	hash64_set_allocation_deallocation_context(hash,
		node_allocation_deallocation_context);
	hash64_clear_element_count(hash);
	if (is_multiple_values_hash) {
		hash64_set_multiple_values_hash(hash);
	} else {
		hash64_set_single_value_hash(hash);
	}
	return (hash64_handle_t)hash;

array_allocation_failed:
	MON_ASSERT(hash != NULL);
	if (mem_dealloc_func == NULL) {
		mon_memory_free(hash);
	} else {
		mem_dealloc_func(hash);
	}
hash_allocation_failed:
	return HASH64_INVALID_HANDLE;
}

static
void hash64_destroy_hash_internal(hash64_table_t *hash)
{
	func_hash64_internal_mem_deallocation_t mem_dealloc_func;
	func_hash64_node_deallocation_t node_dealloc_func;
	hash64_node_t **array;
	uint32_t i;

	array = hash64_get_array(hash);
	mem_dealloc_func = hash64_get_mem_dealloc_func(hash);
	node_dealloc_func = hash64_get_node_dealloc_func(hash);
	for (i = 0; i < hash64_get_hash_size(hash); i++) {
		hash64_node_t *node = array[i];

		if (hash64_get_element_count(hash) == 0) {
			MON_ASSERT(node == NULL);
			break;
		}

		while (node != NULL) {
			hash64_node_t *next_node = hash64_node_get_next(node);

			MON_ASSERT(hash64_get_element_count(hash) != 0);

			if (hash64_is_multiple_values_hash(hash)) {
				uint64_t node_value =
					hash64_node_get_value(node);
				hash64_node_t *internal_node =
					(hash64_node_t *)hash64_uint64_to_ptr(
						node_value);
				while (internal_node != NULL) {
					hash64_node_t *next_internal_node =
						hash64_node_get_next(
							internal_node);
					node_dealloc_func(hash64_get_allocation_deallocation_context
						(
							hash),
						internal_node);
					internal_node = next_internal_node;
				}
			}
			node_dealloc_func(hash64_get_allocation_deallocation_context(
					hash),
				node);
			hash64_dec_element_count(hash);
			node = next_node;
		}
	}

	MON_ASSERT(hash64_get_element_count(hash) == 0);

	if (mem_dealloc_func == NULL) {
		mon_memory_free(array);
		mon_memory_free(hash);
	} else {
		mem_dealloc_func(array);
		mem_dealloc_func(hash);
	}
}

/*-----------------------------------------------------*/

uint32_t hash64_get_node_size(void)
{
	return sizeof(hash64_node_t);
}

hash64_handle_t hash64_create_hash(func_hash64_t hash_func,
				   func_hash64_internal_mem_allocation_t
				   mem_alloc_func,
				   func_hash64_internal_mem_deallocation_t
				   mem_dealloc_func,
				   func_hash64_node_allocation_t node_alloc_func,
				   func_hash64_node_deallocation_t
				   node_dealloc_func,
				   void *node_allocation_deallocation_context,
				   uint32_t hash_size)
{
	return hash64_create_hash_internal(hash_func,
		mem_alloc_func,
		mem_dealloc_func,
		node_alloc_func,
		node_dealloc_func,
		node_allocation_deallocation_context,
		hash_size, FALSE);
}

void hash64_destroy_hash(hash64_handle_t hash_handle)
{
	hash64_table_t *hash = (hash64_table_t *)hash_handle;

	MON_ASSERT(!hash64_is_multiple_values_hash(hash));
	hash64_destroy_hash_internal(hash);
}

uint32_t hash64_default_hash_func(uint64_t key, uint32_t size)
{
	return (uint32_t)(key % size);
}


void *hash64_default_node_alloc_func(void *context UNUSED)
{
	return mon_memory_alloc(hash64_get_node_size());
}

void hash64_default_node_dealloc_func(void *context UNUSED, void *data)
{
	mon_memory_free(data);
}


hash64_handle_t hash64_create_default_hash(uint32_t hash_size)
{
	return hash64_create_hash(hash64_default_hash_func,
		NULL,
		NULL,
		hash64_default_node_alloc_func,
		hash64_default_node_dealloc_func,
		NULL, hash_size);
}

boolean_t hash64_lookup(hash64_handle_t hash_handle,
			uint64_t key,
			uint64_t *value)
{
	hash64_table_t *hash = (hash64_table_t *)hash_handle;
	hash64_node_t *node;

	if (hash == NULL) {
		return FALSE;
	}

	node = hash64_find(hash, key);
	if (node != NULL) {
		MON_ASSERT(hash64_node_get_key(node) == key);
		*value = hash64_node_get_value(node);
		return TRUE;
	}
	return FALSE;
}

boolean_t hash64_insert(hash64_handle_t hash_handle,
			uint64_t key,
			uint64_t value)
{
	hash64_table_t *hash = (hash64_table_t *)hash_handle;

	if (hash == NULL) {
		return FALSE;
	}

	return hash64_insert_internal(hash, key, value, FALSE);
}

boolean_t hash64_update(hash64_handle_t hash_handle,
			uint64_t key,
			uint64_t value)
{
	hash64_table_t *hash = (hash64_table_t *)hash_handle;

	if (hash == NULL) {
		return FALSE;
	}

	return hash64_insert_internal(hash, key, value, TRUE);
}

boolean_t hash64_remove(hash64_handle_t hash_handle, uint64_t key)
{
	hash64_table_t *hash = (hash64_table_t *)hash_handle;
	hash64_node_t *node;
	hash64_node_t **cell;

	if (hash == NULL) {
		return FALSE;
	}

	MON_ASSERT(hash64_find(hash, key) != NULL);

	cell = hash64_retrieve_appropriate_array_cell(hash, key);
	node = *cell;
	if (node == NULL) {
		return FALSE;
	}

	if (hash64_node_get_key(node) == key) {
		*cell = hash64_node_get_next(node);
		MON_ASSERT(hash64_find(hash, key) == NULL);
		hash64_free_node(hash, node);
		MON_ASSERT(hash64_get_element_count(hash) > 0);
		hash64_dec_element_count(hash);
		return TRUE;
	}

	while (node != NULL) {
		hash64_node_t *prev_node = node;
		node = hash64_node_get_next(node);

		if ((node != NULL) && (hash64_node_get_key(node) == key)) {
			hash64_node_set_next(prev_node,
				hash64_node_get_next(node));
			MON_ASSERT(hash64_find(hash, key) == NULL);
			hash64_free_node(hash, node);
			MON_ASSERT(hash64_get_element_count(hash) > 0);
			hash64_dec_element_count(hash);
			return TRUE;
		}
	}

	return FALSE;
}

boolean_t hash64_is_empty(hash64_handle_t hash_handle)
{
	hash64_table_t *hash = (hash64_table_t *)hash_handle;

	if (hash == NULL) {
		return FALSE;
	}

	return hash64_get_element_count(hash) == 0;
}

boolean_t hash64_change_size_and_rehash(hash64_handle_t hash_handle,
					uint32_t hash_size)
{
	hash64_table_t *hash = (hash64_table_t *)hash_handle;
	hash64_node_t **old_array;
	hash64_node_t **new_array;
	uint32_t old_hash_size;
	uint32_t i;

	if (hash == NULL) {
		return FALSE;
	}

	new_array =
		(hash64_node_t **)hash64_mem_alloc(hash,
			sizeof(hash64_node_t *) * hash_size);

	if (new_array == NULL) {
		return FALSE;
	}

	mon_zeromem(new_array, sizeof(hash64_node_t *) * hash_size);

	old_array = hash64_get_array(hash);
	old_hash_size = hash64_get_hash_size(hash);

	hash64_set_array(hash, new_array);
	hash64_set_hash_size(hash, hash_size);

	for (i = 0; i < old_hash_size; i++) {
		hash64_node_t *node = old_array[i];
		while (node != NULL) {
			hash64_node_t *next_node = hash64_node_get_next(node);
			uint64_t key;
			hash64_node_t **new_cell;

			key = hash64_node_get_key(node);
			new_cell = hash64_retrieve_appropriate_array_cell(hash,
				key);
			hash64_node_set_next(node, *new_cell);
			*new_cell = node;

			node = next_node;
		}
		old_array[i] = NULL;
	}

	hash64_mem_free(hash, old_array);
	return TRUE;
}

uint32_t hash64_get_num_of_elements(hash64_handle_t hash_handle)
{
	hash64_table_t *hash = (hash64_table_t *)hash_handle;

	MON_ASSERT(hash != NULL);
	return hash64_get_element_count(hash);
}

void hash64_destroy_multiple_values_hash(hash64_handle_t hash_handle)
{
	hash64_table_t *hash = (hash64_table_t *)hash_handle;

	MON_ASSERT(hash64_is_multiple_values_hash(hash));
	hash64_destroy_hash_internal(hash);
}

#ifdef DEBUG
void hash64_print(hash64_handle_t hash_handle)
{
	hash64_table_t *hash = (hash64_table_t *)hash_handle;
	hash64_node_t **array;
	uint32_t i;

	MON_LOG(mask_anonymous, level_trace, "Hash64:\n");
	MON_LOG(mask_anonymous, level_trace, "========================\n");
	if (hash == NULL) {
		MON_LOG(mask_anonymous, level_trace, "%s: ERROR in parameter\n",
			__FUNCTION__);
		return;
	}
	MON_LOG(mask_anonymous, level_trace, "Num of cells: %d\n",
		hash64_get_hash_size(hash));
	MON_LOG(mask_anonymous, level_trace, "Num of elements: %d\n",
		hash64_get_element_count(hash));

	array = hash64_get_array(hash);
	for (i = 0; i < hash64_get_hash_size(hash); i++) {
		if (array[i] != NULL) {
			hash64_node_t *node = array[i];
			MON_LOG(mask_anonymous, level_trace, "[%d]: ", i);

			while (node != NULL) {
				if (hash64_is_multiple_values_hash(hash)) {
					uint32_t counter = 0;
					hash64_node_t *node_value =
						hash64_uint64_to_ptr(hash64_node_get_value(
								node));
					while (node_value != NULL) {
						counter++;
						node_value =
							hash64_node_get_next(
								node_value);
					}
					MON_LOG(mask_anonymous,
						level_trace,
						"(%P : %d); ",
						hash64_node_get_key(node),
						counter);
				} else {
					MON_LOG(mask_anonymous,
						level_trace,
						"(%P : %P); ",
						hash64_node_get_key(node),
						hash64_node_get_value(node));
				}
				node = hash64_node_get_next(node);
			}

			MON_LOG(mask_anonymous, level_trace, "\n");
		}
	}
}
#endif
