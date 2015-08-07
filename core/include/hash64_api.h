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

#ifndef HASH64_INTERFACE_H
#define HASH64_INTERFACE_H

#include <mon_defs.h>

typedef uint32_t (*func_hash64_t) (uint64_t key, uint32_t size);
typedef void *(*func_hash64_internal_mem_allocation_t) (uint32_t size);
typedef void (*func_hash64_internal_mem_deallocation_t) (void *data);
typedef void *(*func_hash64_node_allocation_t) (void *context);
typedef void (*func_hash64_node_deallocation_t) (void *context, void *data);

typedef void *hash64_handle_t;
typedef void *hash64_multiple_values_hash_iterator_t;
#define HASH64_INVALID_HANDLE ((hash64_handle_t)NULL)
#define HASH64_NULL_ITERATOR  ((hash64_multiple_values_hash_iterator_t)NULL)

/* Function: hash64_get_node_size
 * Description: This function returns the size of hash node.
 * Output: Size
 */
uint32_t hash64_get_node_size(void);

hash64_handle_t hash64_create_default_hash(uint32_t hash_size);

/*------------------------------------------------------------*
* Function: hash64_create_hash
* Description: This function is used in order to create 1-1 hash
* Input: hash_func - hash function which returns index in
*                    array which is lower than "hash_size" parameter.
*        mem_alloc_func - function which will be used for allocation of inner
*                         data structures. If it is NULL then allocation will
*                         be performed directly from heap.
*        mem_dealloc_func - function which will be used for deallocation of
*                           inner data structures. If it is NULL then
*                           deallocation will be performed directly to heap.
*        node_alloc_func - function which will be used for allocation of
*                          hash nodes. Node that function doesn't receive the
*                          size as parameter. In order to know the required
*                          size, use "hash64_get_node_size" function.
*        node_dealloc_func - function which will be used for deallocation of
*                            each node, when necessary.
*        node_allocation_deallocation_context - context which will be passed
*            to "node_alloc_func" and "node_dealloc_func" * functions as
*            parameter.
*        hash_size - number of cells in hash array.
* Return value: Hash handle which should be used as parameter for other
*               functions. In case of failure, HASH64_INVALID_HANDLE will be
*               returned
*------------------------------------------------------------*/
hash64_handle_t hash64_create_hash(func_hash64_t hash_func,
				   func_hash64_internal_mem_allocation_t
				   mem_alloc_func,
				   func_hash64_internal_mem_deallocation_t
				   mem_dealloc_func,
				   func_hash64_node_allocation_t node_alloc_func,
				   func_hash64_node_deallocation_t
				   node_dealloc_func,
				   void *node_allocation_deallocation_context,
				   uint32_t hash_size);

/*------------------------------------------------------------*
* Function: hash64_create_hash
* Description: This function is used in order to destroy 1-1 hash
* Input: hash_handle - handle returned by "hash64_create_hash" function
*------------------------------------------------------------*/
void hash64_destroy_hash(hash64_handle_t hash_handle);

/*------------------------------------------------------------*
* Function: hash64_lookup
* Description: This function is used in order to find the value in 1-1 hash
*              for given key.
* Input: hash_handle - handle returned by "hash64_create_hash" function
*        key -
* Output: value -
* Return value: TRUE in case the value is found
*------------------------------------------------------------*/
boolean_t hash64_lookup(hash64_handle_t hash_handle,
			uint64_t key,
			uint64_t *value);

/*------------------------------------------------------------*
* Function: hash64_insert
* Description: This function is used in order to insert the value into
*              1-1 hash. If some value for given key exists, FALSE
*              will be returned.
* Input: hash_handle - handle returned by "hash64_create_hash" function
*        key -
*        value -
* Return value: TRUE in case the operation is successful
*------------------------------------------------------------*/
boolean_t hash64_insert(hash64_handle_t hash_handle,
			uint64_t key,
			uint64_t value);

/*------------------------------------------------------------*
* Function: hash64_update
* Description: This function is used in order to update the value in 1-1 hash.
*              If the value doesn't exist it will be inserted.
* Input: hash_handle - handle returned by "hash64_create_hash" function
*        key -
*        value - * Return value: TRUE in case the operation is successful
*------------------------------------------------------------*/
boolean_t hash64_update(hash64_handle_t hash_handle,
			uint64_t key,
			uint64_t value);

/*------------------------------------------------------------*
* Function: hash64_remove
* Description: This function is used in order to remove the value from
*              1-1 hash.
* Input: hash_handle - handle returned by "hash64_create_hash" function
*        key -
* Return value: TRUE in case the operation is successful
*------------------------------------------------------------*/
boolean_t hash64_remove(hash64_handle_t hash_handle, uint64_t key);

/*------------------------------------------------------------*
* Function: hash64_is_empty
* Description: This function is used in order check whether 1-1 hash is empty.
* Input: hash_handle - handle returned by "hash64_create_hash" function
* Return value: TRUE in case the hash is empty.
*------------------------------------------------------------*/
boolean_t hash64_is_empty(hash64_handle_t hash_handle);

/*------------------------------------------------------------*
* Function: hash64_change_size_and_rehash
* Description: This function is used in order to change the size of the hash
*              and rehash it.
* Input: hash_handle - handle returned by "hash64_create_hash" function
*        hash_size - new size
* Return value: TRUE in case the operation is successfull.
*------------------------------------------------------------*/
boolean_t hash64_change_size_and_rehash(hash64_handle_t hash_handle,
					uint32_t hash_size);

/*------------------------------------------------------------*
* Function: hash64_change_size_and_rehash
* Description: This function is used in order to change the size of the hash
*              and rehash it.
* Input: hash_handle - handle returned by "hash64_create_hash" function
* Return value: Number of elements in hash.
*------------------------------------------------------------*/
uint32_t hash64_get_num_of_elements(hash64_handle_t hash_handle);

/*------------------------------------------------------------*
* Function: hash64_destroy_multiple_values_hash
* Description: This function is used in order to destroy 1-n hash
* Input: hash_handle - handle returned by "hash64_create_multiple_values_hash"
*                      function
*------------------------------------------------------------*/
void hash64_destroy_multiple_values_hash(hash64_handle_t hash_handle);

#ifdef DEBUG
void hash64_print(hash64_handle_t hash_handle);
#endif
#endif
