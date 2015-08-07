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

#ifndef _MEMORY_ALLOCATOR_H
#define _MEMORY_ALLOCATOR_H

#include "mon_defs.h"
#include "heap.h"

/*-------------------------------------------------------*
*  FUNCTION : mon_memory_allocate()
*  PURPOSE  : Allocates contiguous buffer of given size, filled with zeroes
*  ARGUMENTS: IN uint32_t size - size of the buffer in bytes
*  RETURNS  : void*  address of allocted buffer if OK, NULL if failed
*-------------------------------------------------------*/
void *mon_mem_allocate(char *file_name, int32_t line_number, IN uint32_t size);

/*-------------------------------------------------------*
*  FUNCTION : mon_memory_free()
*  PURPOSE  : Release previously allocated buffer
*  ARGUMENTS: IN void *p_buffer - buffer to be released
*  RETURNS  : void
*-------------------------------------------------------*/
void mon_mem_free(char *file_name, int32_t line_number, IN void *buff);

void *mon_mem_allocate_aligned(char *file_name,
			       int32_t line_number,
			       IN uint32_t size,
			       IN uint32_t alignment);

/*-------------------------------------------------------*
*  FUNCTION : mon_mem_buff_size()
*  PURPOSE  : Get size of buff
*  ARGUMENTS: IN void *p_buffer - the buffer
*  RETURNS  : uint32_t - size
*-------------------------------------------------------*/
uint32_t mon_mem_buff_size(char *file_name, int32_t line_number, IN void *buff);

/*-------------------------------------------------------*
*  FUNCTION : mon_mem_pool_size()
*  PURPOSE  : Get the size of pool that will be needed to alloc a buff of
*             given size
*  ARGUMENTS: IN uint32_t size - size
*  RETURNS  : uint32_t - pool size
*-------------------------------------------------------*/
uint32_t mon_mem_pool_size(char *file_name,
			   int32_t line_number,
			   IN uint32_t size);

#if defined DEBUG
/* This is done to remove out the file name and line number (present in
 * strings) from the release build */
#define mon_malloc(__size)                                               \
	mon_mem_allocate(__FILE__, __LINE__, __size)

#define mon_malloc_aligned(__size, __alignment)                          \
	mon_mem_allocate_aligned(__FILE__, __LINE__, __size, __alignment)

#define mon_mfree(__buff)                                                \
	mon_mem_free(__FILE__, __LINE__, __buff)

#define mon_mem_alloc_size(__size)                                       \
	mon_mem_pool_size(__FILE__, __LINE__, __size)

#define mon_mem_free_size(__buff)                                        \
	mon_mem_buff_size(__FILE__, __LINE__, __buff)
#else
#define mon_malloc(__size)                                               \
	mon_mem_allocate(NULL, 0, __size)

#define mon_malloc_aligned(__size, __alignment)                          \
	mon_mem_allocate_aligned(NULL, 0, __size, __alignment)

#define mon_mfree(__buff)                                                \
	mon_mem_free(NULL, 0, __buff)

#define mon_mem_alloc_size(__size)                                       \
	mon_mem_pool_size(NULL, 0, __size)

#define mon_mem_free_size(__buff)                                        \
	mon_mem_buff_size(NULL, 0, __buff)
#endif

void memory_allocator_print(void);

#endif
