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

#ifndef _HASH64_H_
#define _HASH64_H_

#include <mon_defs.h>
#include <hash64_api.h>

typedef struct hash64_node_t {
	struct hash64_node_t	*next;
	uint64_t		key;
	uint64_t		value;
} hash64_node_t;

INLINE hash64_node_t *hash64_node_get_next(hash64_node_t *cell)
{
	return cell->next;
}

INLINE void hash64_node_set_next(hash64_node_t *cell, hash64_node_t *next)
{
	cell->next = next;
}

INLINE uint64_t hash64_node_get_key(hash64_node_t *cell)
{
	return cell->key;
}

INLINE void hash64_node_set_key(hash64_node_t *cell, uint64_t key)
{
	cell->key = key;
}

INLINE uint64_t hash64_node_get_value(hash64_node_t *cell)
{
	return cell->value;
}

INLINE void hash64_node_set_value(hash64_node_t *cell, uint64_t value)
{
	cell->value = value;
}

typedef struct {
	hash64_node_t				**array;
	func_hash64_t				hash_func;
	func_hash64_internal_mem_allocation_t	mem_alloc_func;
	func_hash64_internal_mem_deallocation_t mem_dealloc_func;
	func_hash64_node_allocation_t		node_alloc_func;
	func_hash64_node_deallocation_t		node_dealloc_func;
	void					*node_allocation_deallocation_context;
	uint32_t				size;
	uint32_t				element_count;
	boolean_t				is_multiple_values_hash;
	uint32_t				padding; /* not in use */
} hash64_table_t;

INLINE uint32_t hash64_get_hash_size(hash64_table_t *hash)
{
	return hash->size;
}

INLINE void hash64_set_hash_size(hash64_table_t *hash, uint32_t size)
{
	hash->size = size;
}

INLINE hash64_node_t **hash64_get_array(hash64_table_t *hash)
{
	return hash->array;
}

INLINE void hash64_set_array(hash64_table_t *hash, hash64_node_t **array)
{
	hash->array = array;
}

INLINE func_hash64_t hash64_get_hash_func(hash64_table_t *hash)
{
	return hash->hash_func;
}

INLINE void hash64_set_hash_func(hash64_table_t *hash, func_hash64_t hash_func)
{
	hash->hash_func = hash_func;
}

INLINE func_hash64_internal_mem_allocation_t
hash64_get_mem_alloc_func(hash64_table_t *hash)
{
	return hash->mem_alloc_func;
}

INLINE void hash64_set_mem_alloc_func(hash64_table_t *hash,
				      func_hash64_internal_mem_allocation_t
				      mem_alloc_func)
{
	hash->mem_alloc_func = mem_alloc_func;
}

INLINE func_hash64_internal_mem_deallocation_t
hash64_get_mem_dealloc_func(hash64_table_t *hash)
{
	return hash->mem_dealloc_func;
}

INLINE void hash64_set_mem_dealloc_func(hash64_table_t *hash,
					func_hash64_internal_mem_deallocation_t
					mem_dealloc_func)
{
	hash->mem_dealloc_func = mem_dealloc_func;
}

INLINE func_hash64_node_allocation_t
hash64_get_node_alloc_func(hash64_table_t *hash)
{
	return hash->node_alloc_func;
}

INLINE void hash64_set_node_alloc_func(hash64_table_t *hash,
				       func_hash64_node_allocation_t
				       node_alloc_func)
{
	hash->node_alloc_func = node_alloc_func;
}

INLINE func_hash64_node_deallocation_t
hash64_get_node_dealloc_func(hash64_table_t *hash)
{
	return hash->node_dealloc_func;
}

INLINE void hash64_set_node_dealloc_func(hash64_table_t *hash,
					 func_hash64_node_deallocation_t
					 node_dealloc_func)
{
	hash->node_dealloc_func = node_dealloc_func;
}

INLINE void *hash64_get_allocation_deallocation_context(hash64_table_t *hash)
{
	return hash->node_allocation_deallocation_context;
}

INLINE void hash64_set_allocation_deallocation_context(hash64_table_t *hash,
						       void *context)
{
	hash->node_allocation_deallocation_context = context;
}

INLINE uint32_t hash64_get_element_count(hash64_table_t *hash)
{
	return hash->element_count;
}

INLINE void hash64_clear_element_count(hash64_table_t *hash)
{
	hash->element_count = 0;
}

INLINE void hash64_inc_element_count(hash64_table_t *hash)
{
	hash->element_count += 1;
}

INLINE void hash64_dec_element_count(hash64_table_t *hash)
{
	hash->element_count -= 1;
}

INLINE boolean_t hash64_is_multiple_values_hash(hash64_table_t *hash)
{
	return hash->is_multiple_values_hash;
}

INLINE void hash64_set_multiple_values_hash(hash64_table_t *hash)
{
	hash->is_multiple_values_hash = TRUE;
}

INLINE void hash64_set_single_value_hash(hash64_table_t *hash)
{
	hash->is_multiple_values_hash = FALSE;
}

#endif
