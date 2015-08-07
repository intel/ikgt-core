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

#ifndef _HEAP_H_
#define _HEAP_H_

#include "mon_defs.h"

#define HEAP_PAGE_INT uint32_t

typedef struct {
	/* When in_use=1, this represents the number of allocated pages
	 * When in_use=0, represents the number of contiguos pages from
	 * this address */
	HEAP_PAGE_INT	number_of_pages : 31;
	/* 1=InUse */
	HEAP_PAGE_INT	in_use : 1;

#ifdef DEBUG
	int32_t		line_number;
	char		*file_name;
#endif
} heap_page_descriptor_t;

/*-------------------------------------------------------*
*  FUNCTION : mon_heap_get_max_used_pages()
*  PURPOSE  : Returns the max amount of Mon heap pages used
*             from post-launch mon
*  ARGUMENTS:
*  RETURNS  : HEAP max heap used in pages
*-------------------------------------------------------*/
HEAP_PAGE_INT mon_heap_get_max_used_pages(void);

/*-------------------------------------------------------*
*  FUNCTION : mon_heap_initialize()
*  PURPOSE  : Format memory block for memory allocation / free services.
*           : Calculate actual number of pages.
*  ARGUMENTS: IN address_t heap_base_address - address at which the heap is
*             located
*           : size_t    heap_size - in bytes
*  RETURNS  : Last occupied address
*-------------------------------------------------------*/
address_t mon_heap_initialize(IN address_t heap_base_address,
			      IN size_t heap_size);

/*-------------------------------------------------------*
*  FUNCTION : mon_heap_extend()
*  PURPOSE  : Extend the heap to an additional memory block
*			: update actual number of pages.
*  ARGUMENTS: IN address_t ex_heap_base_address - address at which the heap is
*             located
*           : size_t    ex_heap_size - in bytes
*  RETURNS  : Last occupied address
*-------------------------------------------------------*/
address_t mon_heap_extend(IN address_t ex_heap_buffer_address,
			  IN size_t ex_heap_buffer_size);

/*-------------------------------------------------------*
*  FUNCTION : mon_head_get_details()
*  PURPOSE  : Retrieve information about heap area.
*  ARGUMENTS: OUT hva_t* base_addr - address at which the heap is located
*           : uint32_t   size - in bytes
*-------------------------------------------------------*/
void mon_heap_get_details(OUT hva_t *base_addr, OUT uint32_t *size);

/*-------------------------------------------------------*
*  FUNCTION : mon_page_alloc()
*  PURPOSE  : Allocates contiguous buffer of given size
*  ARGUMENTS: IN HEAP_PAGE_INT number_of_pages - size of the buffer in 4K pages
*  RETURNS  : void*  address of allocted buffer if OK, NULL if failed
*-------------------------------------------------------*/
void *mon_page_allocate(
#ifdef DEBUG
	char *file_name, int32_t line_number,
#endif
	HEAP_PAGE_INT number_of_pages);

/*-------------------------------------------------------*
*  FUNCTION : mon_page_alloc_scattered()
*  PURPOSE  : Fills given array with addresses of allocated 4K pages
*  ARGUMENTS: IN HEAP_PAGE_INT number_of_pages - number of 4K pages
*           : OUT void * p_page_array[] - contains the addresses of allocated
*             pages
*  RETURNS  : number of successfully allocated pages
*-------------------------------------------------------*/
HEAP_PAGE_INT mon_page_allocate_scattered(
#ifdef DEBUG
	char *file_name, int32_t line_number,
#endif
	IN HEAP_PAGE_INT number_of_pages, OUT void *p_page_array[]);

/*-------------------------------------------------------*
*  FUNCTION : mon_page_free()
*  PURPOSE  : Release previously allocated buffer
*  ARGUMENTS: IN void *p_buffer - buffer to be released
*  RETURNS  : void
*-------------------------------------------------------*/
void mon_page_free(IN void *p_buffer);

/*-------------------------------------------------------*
*  FUNCTION : mon_page_buff_size()
*  PURPOSE  : Identify number of pages in previously allocated buffer
*  ARGUMENTS: IN void *p_buffer - the buffer
*  RETURNS  : uint32_t - Num pages this buffer is using
*-------------------------------------------------------*/
uint32_t mon_page_buff_size(IN void *p_buffer);

HEAP_PAGE_INT mon_heap_get_total_pages(void);

/*-------------------------------------------------------*
*  FUNCTION : mon_memory_allocate()
*  PURPOSE  : Allocates contiguous buffer of given size, filled with zeroes
*  ARGUMENTS: IN uint32_t size - size of the buffer in bytes
*  RETURNS  : void*  address of allocted buffer if OK, NULL if failed
*             returned buffer is always 4K page alinged
*-------------------------------------------------------*/
void *mon_memory_allocate(
#ifdef DEBUG
	char *file_name, int32_t line_number,
#endif
	IN uint32_t size);

/*-------------------------------------------------------*
*  FUNCTION : mon_memory_free()
*  PURPOSE  : Release previously allocated buffer
*  ARGUMENTS: IN void *p_buffer - buffer to be released
*  RETURNS  : void
*-------------------------------------------------------*/
#define mon_memory_free(p_buffer) mon_page_free(p_buffer)

typedef void (*func_mon_free_mem_callback_t) (IN void *context);
typedef uint32_t heap_alloc_handle_t;
#define HEAP_INVALID_ALLOC_HANDLE ((heap_alloc_handle_t)(~0))

/*-------------------------------------------------------*
*  FUNCTION : mon_memory_allocate_must_succeed()
*  PURPOSE  : The function tries to allocate requested memory. In case of
*             insufficient memory, the heap will call all the registered
*             deallocation functions except the one which was recorded under
*             the passed heap_alloc_handle_t.
*  ARGUMENTS: IN heap_alloc_handle_t handle - handle returned by
*                 "mon_heap_register_free_mem_callback".
*             It is possible to pass HEAP_INVALID_ALLOC_HANDLE, but in this
*             case all the recorded callbacks will be called, no exceptions.
*             IN uint32_t size - requested size.
*  RETURNS  : void* - allocated memory.
*-------------------------------------------------------*/
void *mon_memory_allocate_must_succeed(
#ifdef DEBUG
	char *file_name, int32_t line_number,
#endif
	heap_alloc_handle_t handle, uint32_t size);

#ifdef DEBUG

#define mon_page_alloc(__num_of_pages)                                         \
	mon_page_allocate(__FILE__, __LINE__, __num_of_pages)

#define mon_memory_alloc(__size)                                               \
	mon_memory_allocate(__FILE__, __LINE__, __size)

#define mon_page_alloc_scattered(__num_of_pages, __p_page_array)               \
	mon_page_allocate_scattered(__FILE__, \
	__LINE__, \
	__num_of_pages, \
	__p_page_array)

#define mon_memory_alloc_must_succeed(__handle, __size)                        \
	mon_memory_allocate_must_succeed(__FILE__, __LINE__, __handle, __size)

void mon_heap_show(void);

#else

#define mon_page_alloc                  mon_page_allocate
#define mon_memory_alloc                mon_memory_allocate
#define mon_page_alloc_scattered        mon_page_allocate_scattered
#define mon_memory_alloc_must_succeed   mon_memory_allocate_must_succeed

#define mon_heap_show()

#endif

#endif  /* _HEAP_H_ */
