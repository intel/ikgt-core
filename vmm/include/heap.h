/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _HEAP_H_
#define _HEAP_H_

#include "vmm_base.h"

/*-------------------------------------------------------*
*  FUNCTION : vmm_heap_initialize()
*  PURPOSE  : Format memory block for memory allocation / free services.
*           : Calculate actual number of pages.
*  ARGUMENTS: IN uint64_t heap_base_address - address at which the heap is
*             located
*           : uint64_t    heap_size - in bytes
*  RETURNS  : void
*-------------------------------------------------------*/
void vmm_heap_initialize(IN uint64_t heap_base_address,
				  IN uint64_t heap_size);

/*-------------------------------------------------------*
*  FUNCTION : page_alloc()
*  PURPOSE  : Allocates contiguous buffer of given size
*  ARGUMENTS: IN HEAP_PAGE_INT number_of_pages - size of the buffer in 4K pages
*  RETURNS  : void*  address of allocted buffer if OK, NULL if failed
*-------------------------------------------------------*/
void *page_alloc(uint32_t number_of_pages);

/*-------------------------------------------------------*
*  FUNCTION : page_free()
*  PURPOSE  : Release previously allocated buffer
*  ARGUMENTS: IN void *p_buffer - buffer to be released
*  RETURNS  : void
*-------------------------------------------------------*/
void page_free(IN void *p_buffer);

/*-------------------------------------------------------*
*  FUNCTION : mem_alloc()
*  PURPOSE  : Allocates contiguous buffer of given size, filled with zeroes
*  ARGUMENTS: IN uint32_t size - size of the buffer in bytes
*  RETURNS  : void*  address of allocted buffer if OK, NULL if failed
*-------------------------------------------------------*/
void *mem_alloc(IN uint32_t size);

/*-------------------------------------------------------*
*  FUNCTION : mem_free()
*  PURPOSE  : Release previously allocated buffer
*  ARGUMENTS: IN void *p_buffer - buffer to be released
*  RETURNS  : void
*-------------------------------------------------------*/
void mem_free(IN void *buff);

/*-------------------------------------------------------*
*  FUNCTION : vmm_pool_initialize()
*  PURPOSE  : init pool lock
*  ARGUMENTS: void
*  RETURNS  : void
*-------------------------------------------------------*/
void vmm_pool_initialize(void);

#endif  /* _HEAP_H_ */
