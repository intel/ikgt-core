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

#include "mon_defs.h"
#include "common_libc.h"
#include "lock.h"
#include "heap.h"
#include "mon_dbg.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(HEAP_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(HEAP_C, __condition)

#define CHECK_ADDRESS_IN_RANGE(addr, range_start, size) \
	(((uint64_t)addr) >= ((uint64_t)range_start) && ((uint64_t)addr) <= \
	 ((uint64_t)range_start) + (size))

#define HEAP_PAGE_TO_POINTER(__page_no) \
	((__page_no >= ex_heap_start_page) ? \
	(void *)(address_t)(ex_heap_base + \
			    ((__page_no - ex_heap_start_page - \
			      1) * PAGE_4KB_SIZE)) : \
	(void *)(address_t)(heap_base + (__page_no * PAGE_4KB_SIZE)))
#define HEAP_POINTER_TO_PAGE(__pointer) \
	((CHECK_ADDRESS_IN_RANGE(__pointer, ex_heap_base, \
		 ex_heap_pages * PAGE_4KB_SIZE)) ? \
	(HEAP_PAGE_INT)((((address_t)(__pointer) - ex_heap_base)) / \
			PAGE_4KB_SIZE) + ex_heap_start_page + 1 : \
	(HEAP_PAGE_INT)((((address_t)(__pointer) - heap_base)) / PAGE_4KB_SIZE))

typedef struct {
	func_mon_free_mem_callback_t	callback_func;
	void				*context;
} free_mem_callback_desc_t;

#define HEAP_MAX_NUM_OF_RECORDED_CALLBACKS 20

static heap_page_descriptor_t *heap_array;
/* address at which the heap is located */
static address_t heap_base;
/* actual number of pages */
static HEAP_PAGE_INT heap_total_pages;
static uint32_t heap_total_size;
static mon_lock_t heap_lock;
static free_mem_callback_desc_t
	free_mem_callbacks[HEAP_MAX_NUM_OF_RECORDED_CALLBACKS];
static uint32_t num_of_registered_callbacks;

static HEAP_PAGE_INT heap_pages;
static address_t ex_heap_base;
static HEAP_PAGE_INT ex_heap_pages;
static HEAP_PAGE_INT ex_heap_start_page;
static HEAP_PAGE_INT max_used_pages;

extern uint32_t g_heap_pa_num;

HEAP_PAGE_INT mon_heap_get_total_pages(void)
{
	return heap_total_pages;
}

/*-------------------------------------------------------*
*  FUNCTION : mon_heap_get_max_used_pages()
*  PURPOSE  : Returns the max amount of Mon heap pages used
*             from post-launch mon
*  ARGUMENTS:
*  RETURNS  : HEAP max heap used in pages
*-------------------------------------------------------*/
HEAP_PAGE_INT mon_heap_get_max_used_pages(void)
{
	return max_used_pages;
}

/*-------------------------------------------------------*
*  FUNCTION : mon_heap_initialize()
*  PURPOSE  : Partition memory for memory allocation / free services.
*           : Calculate actual number of pages.
*  ARGUMENTS: IN address_t heap_buffer_address - address at which the heap is
*             located
*           : IN size_t    heap_buffer_size - in bytes
*  RETURNS  : Last occupied address
*-------------------------------------------------------*/
address_t mon_heap_initialize(IN address_t heap_buffer_address,
			      IN size_t heap_buffer_size)
{
	address_t unaligned_heap_base;
	HEAP_PAGE_INT number_of_pages;
	HEAP_PAGE_INT i;

	/* MON_LOG(mask_anonymous, level_trace,"HEAP INIT: heap_buffer_address = %P
	 * heap_buffer_size = %d\n", heap_buffer_address, heap_buffer_size); */

	/* to be on the safe side */
	heap_buffer_address =
		ALIGN_FORWARD(heap_buffer_address, sizeof(address_t));

	/* record total size of whole heap area */
	heap_total_size = (uint32_t)ALIGN_FORWARD(heap_buffer_size,
		PAGE_4KB_SIZE);

	/* heap descriptors placed at the beginning */
	heap_array = (heap_page_descriptor_t *)heap_buffer_address;

	/* MON_LOG(mask_anonymous, level_trace,"HEAP INIT: heap_array is at %P\n",
	 * heap_array); */

	/* calculate how many unaligned pages we can support */
	number_of_pages =
		(HEAP_PAGE_INT)((heap_buffer_size +
				 (g_heap_pa_num * PAGE_4KB_SIZE))
				/ (PAGE_4KB_SIZE +
				   sizeof(heap_page_descriptor_t)));
	ex_heap_start_page = number_of_pages + 1;
	MON_LOG(mask_anonymous,
		level_trace,
		"HEAP INIT: number_of_pages = %d\n",
		number_of_pages);

	/* heap_base can start immediately after the end of heap_array */
	unaligned_heap_base = (address_t)&heap_array[number_of_pages];

	/* but on the 1st 4K boundary address
	 * here 4K pages start */
	heap_base = ALIGN_FORWARD(unaligned_heap_base, PAGE_4KB_SIZE);
	/* MON_LOG(mask_anonymous, level_trace,"HEAP INIT: heap_base is at %P\n",
	 * heap_base); */

	/* decrement heap size, due to descriptor allocation and alignment */
	heap_buffer_size -= heap_base - heap_buffer_address;

	/* MON_LOG(mask_anonymous, level_trace,"HEAP INIT: heap_buffer_size =
	 * %P\n", heap_buffer_size); */

	/* now we can get actual number of available 4K pages */
	heap_total_pages = (HEAP_PAGE_INT)(heap_buffer_size / PAGE_4KB_SIZE);
	heap_pages = heap_total_pages;
	MON_LOG(mask_anonymous,
		level_trace,
		"HEAP INIT: heap_total_pages = %P\n",
		heap_total_pages);

	MON_ASSERT(heap_total_pages > 0);

	for (i = 0; i < heap_total_pages; ++i) {
		heap_array[i].in_use = 0;
		heap_array[i].number_of_pages = (heap_total_pages - i);
	}

	/* MON_DEBUG_CODE(mon_heap_show()); */

	lock_initialize(&heap_lock);

	return heap_base + (heap_total_pages * PAGE_4KB_SIZE);
}

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
			  IN size_t ex_heap_buffer_size)
{
	size_t heap_buffer_size;
	HEAP_PAGE_INT i;

	lock_acquire(&heap_lock);

	MON_LOG(mask_anonymous, level_print_always,
		"HEAP EXT: Max Used Initial Memory %dKB\n",
		(max_used_pages * 4));

	/* extend can be called only once. */
	MON_ASSERT(ex_heap_base == 0);

	MON_ASSERT(!CHECK_ADDRESS_IN_RANGE
			(ex_heap_buffer_address, heap_array, heap_total_size));
	MON_ASSERT(!CHECK_ADDRESS_IN_RANGE
			(heap_array,
			ex_heap_buffer_address,
			ex_heap_buffer_size));

	/* MON_DEBUG_CODE(mon_heap_show()); */
	ex_heap_base = ALIGN_FORWARD(ex_heap_buffer_address, sizeof(address_t));
	/* record total size of whole heap area */
	heap_total_size +=
		(uint32_t)ALIGN_FORWARD(ex_heap_buffer_size, PAGE_4KB_SIZE);

	heap_buffer_size =
		ex_heap_buffer_size - (ex_heap_base - ex_heap_buffer_address);

	/* leave one dummy page for boundry which is always marked as used. */
	ex_heap_pages = (HEAP_PAGE_INT)(heap_buffer_size / PAGE_4KB_SIZE) + 1;

	ex_heap_start_page = heap_total_pages;
	heap_total_pages += ex_heap_pages;

	MON_ASSERT(heap_total_pages > 0);

	heap_array[ex_heap_start_page].in_use = 1;
	heap_array[ex_heap_start_page].number_of_pages = 1;

	for (i = ex_heap_start_page + 1; i < heap_total_pages; ++i) {
		heap_array[i].in_use = 0;
		heap_array[i].number_of_pages = (heap_total_pages - i);
	}

	lock_release(&heap_lock);

	return ex_heap_base + (ex_heap_pages * PAGE_4KB_SIZE);
}

#if defined DEBUG
void mon_heap_get_details(OUT hva_t *base_addr, OUT uint32_t *size)
{
	*base_addr = (hva_t)heap_array;
	*size = heap_total_size;
}
#endif

static void *page_alloc_unprotected(
#ifdef DEBUG
	char *file_name, int32_t line_number,
#endif
	HEAP_PAGE_INT number_of_pages)
{
	HEAP_PAGE_INT i;
	HEAP_PAGE_INT allocated_page_no;
	void *p_buffer = NULL;

	if (number_of_pages == 0) {
		return NULL;
	}

	for (i = 0; i < heap_total_pages; ++i) {
		if ((0 == heap_array[i].in_use) &&
		    (number_of_pages <= heap_array[i].number_of_pages)) {
			/* validity check */
			MON_ASSERT(
				(i + heap_array[i].number_of_pages) <=
				heap_total_pages);

			/* found the suitable buffer */
			allocated_page_no = i;
			p_buffer = HEAP_PAGE_TO_POINTER(allocated_page_no);
			heap_array[allocated_page_no].in_use = 1;
			heap_array[allocated_page_no].number_of_pages =
				number_of_pages;
#ifdef DEBUG
			heap_array[i].file_name = file_name;
			heap_array[i].line_number = line_number;
#endif
			/* MON_LOG(mask_anonymous, level_trace,"HEAP: Successfully
			 * allocated %d pages at %P\n", number_of_pages, p_buffer);
			 * MON_LOG(mask_anonymous, level_trace,"HEAP
			 * page_alloc_unprotected: i=%d, heap_array[i].in_use=%d
			 * heap_array[i].number_of_pages=%d\n", i, heap_array[i].in_use,
			 * heap_array[i].number_of_pages); */
			/* mark next number_of_pages-1 pages as in_use */
			for (i = allocated_page_no + 1;
			     i < (allocated_page_no + number_of_pages); ++i) {
				heap_array[i].in_use = 1;
				heap_array[i].number_of_pages = 0;
			}

			if (max_used_pages <
			    (allocated_page_no + number_of_pages)) {
				max_used_pages = allocated_page_no +
						 number_of_pages;
			}
			/* MON_LOG(mask_anonymous, level_trace,"HEAP next free: i=%d,
			 * heap_array[i].in_use=%d heap_array[i].number_of_pages=%d\n", i,
			 * heap_array[i].in_use, heap_array[i].number_of_pages); */

			/* MON_DEBUG_CODE(mon_heap_show()); */
			/* leave the outer loop */
			break;
		}
	}

	if (NULL == p_buffer) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ERROR: (%s %d)  Failed to allocate %d pages\n",
			__FILE__,
			__LINE__,
			number_of_pages);
	}

	return p_buffer;
}

/*-------------------------------------------------------*
*  FUNCTION : mon_page_allocate()
*  PURPOSE  : Allocates contiguous buffer of given size, and fill it with zeroes
*  ARGUMENTS: IN HEAP_PAGE_INT number_of_pages - size of the buffer in 4K pages
*  RETURNS  : void*  address of allocted buffer if OK, NULL if failed
*-------------------------------------------------------*/
void *mon_page_allocate(
#ifdef DEBUG
	char *file_name, int32_t line_number,
#endif
	IN HEAP_PAGE_INT number_of_pages)
{
	void *p_buffer = NULL;

	lock_acquire(&heap_lock);
	p_buffer = page_alloc_unprotected(
#ifdef DEBUG
		file_name, line_number,
#endif
		number_of_pages);
	lock_release(&heap_lock);

	return p_buffer;
}

/*-------------------------------------------------------*
*  FUNCTION : mon_page_allocate_scattered()
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
	IN HEAP_PAGE_INT number_of_pages,
	OUT void *p_page_array[])
{
	HEAP_PAGE_INT i;
	HEAP_PAGE_INT number_of_allocated_pages;

	lock_acquire(&heap_lock);

	for (i = 0; i < number_of_pages; ++i) {
		p_page_array[i] = page_alloc_unprotected(
#ifdef DEBUG
			file_name, line_number,
#endif
			1);
		if (NULL == p_page_array[i]) {
			MON_LOG(mask_anonymous,
				level_trace,
				"ERROR: (%s %d)  Failed to allocate pages %d..%d\n",
				__FILE__,
				__LINE__,
				i + 1,
				number_of_pages);
			/* leave the loop */
			break;
		}
	}
	lock_release(&heap_lock);

	number_of_allocated_pages = i;

	/* fill the pages which failed to be allocated with NULLs */
	for (; i < number_of_pages; ++i)
		p_page_array[i] = NULL;
	return number_of_allocated_pages;
}

static void mon_mark_pages_free(HEAP_PAGE_INT page_from,
				HEAP_PAGE_INT page_to,
				HEAP_PAGE_INT pages_to_release)
{
	HEAP_PAGE_INT i;

	for (i = page_from; i < page_to; ++i) {
		heap_array[i].in_use = 0;
		heap_array[i].number_of_pages = pages_to_release -
						(i - page_from);
	}
}

/*-------------------------------------------------------*
*  FUNCTION : mon_page_free()
*  PURPOSE  : Release previously allocated buffer
*  ARGUMENTS: IN void *p_buffer - buffer to be released
*  RETURNS  : void
*-------------------------------------------------------*/
void mon_page_free(IN void *p_buffer)
{
	HEAP_PAGE_INT release_from_page_id;     /* first page to release */
	HEAP_PAGE_INT release_to_page_id;       /* page next to last to release */
	HEAP_PAGE_INT pages_to_release;         /* num of pages, to be released */
	address_t address;

	address = (address_t)(size_t)p_buffer;

	if (!(CHECK_ADDRESS_IN_RANGE(address, heap_base, heap_pages *
		      PAGE_4KB_SIZE)
	      || CHECK_ADDRESS_IN_RANGE(address, ex_heap_base,
		      ex_heap_pages * PAGE_4KB_SIZE))
	    || (address & PAGE_4KB_MASK) != 0) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ERROR: (%s %d)  Buffer %p is out of heap space\n",
			__FILE__,
			__LINE__,
			p_buffer);
		MON_DEADLOOP();
		return;
	}
	lock_acquire(&heap_lock);

	release_from_page_id = HEAP_POINTER_TO_PAGE(p_buffer);

	/* MON_LOG(mask_anonymous, level_trace,"HEAP: trying to free page_id %d\n",
	 * release_from_page_id); */

	if (0 == heap_array[release_from_page_id].in_use ||
	    0 == heap_array[release_from_page_id].number_of_pages) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ERROR: (%s %d)  Page %d is not in use\n",
			__FILE__,
			__LINE__,
			release_from_page_id);
		MON_DEADLOOP();
		return;
	}

	pages_to_release = heap_array[release_from_page_id].number_of_pages;

	/* check if the next to the last released page is free */
	/* and if so merge both regions */

	release_to_page_id = release_from_page_id + pages_to_release;

	if (release_to_page_id < heap_total_pages &&
	    0 == heap_array[release_to_page_id].in_use &&
	    (release_to_page_id +
	     heap_array[release_to_page_id].number_of_pages) <=
	    heap_total_pages) {
		pages_to_release +=
			heap_array[release_to_page_id].number_of_pages;
	}

	/* move backward, to grab all free pages, trying to prevent fragmentation.
	 * 3rd check is for sanity only */
	while (release_from_page_id > 0 &&
	       0 == heap_array[release_from_page_id - 1].in_use &&
	       0 != heap_array[release_from_page_id - 1].number_of_pages) {
		release_from_page_id--;
		pages_to_release++;
	}

	mon_mark_pages_free(release_from_page_id, release_to_page_id,
		pages_to_release);

	lock_release(&heap_lock);
}

/*-------------------------------------------------------*
*  FUNCTION : mon_page_buff_size()
*  PURPOSE  : Identify number of pages in previously allocated buffer
*  ARGUMENTS: IN void *p_buffer - the buffer
*  RETURNS  : uint32_t - Num pages this buffer is using
*-------------------------------------------------------*/
uint32_t mon_page_buff_size(IN void *p_buffer)
{
	HEAP_PAGE_INT release_from_page_id;     /* first page to release */
	uint32_t num_pages;                     /* num of pages, to be released */
	address_t address;

	address = (address_t)(size_t)p_buffer;

	if (!(CHECK_ADDRESS_IN_RANGE(address, heap_base, heap_pages *
		      PAGE_4KB_SIZE)
	      || CHECK_ADDRESS_IN_RANGE(address, ex_heap_base,
		      ex_heap_pages * PAGE_4KB_SIZE))
	    || (address & PAGE_4KB_MASK) != 0) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ERROR: (%s %d)  Buffer %p is out of heap space\n",
			__FILE__,
			__LINE__,
			p_buffer);
		MON_DEADLOOP();
		return 0;
	}

	release_from_page_id = HEAP_POINTER_TO_PAGE(p_buffer);

	/* MON_LOG(mask_anonymous, level_trace,"HEAP: trying to free page_id %d\n",
	 * release_from_page_id); */

	if (0 == heap_array[release_from_page_id].in_use ||
	    0 == heap_array[release_from_page_id].number_of_pages) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ERROR: (%s %d)  Page %d is not in use\n",
			__FILE__,
			__LINE__,
			release_from_page_id);
		MON_DEADLOOP();
		return 0;
	}

	num_pages = (uint32_t)heap_array[release_from_page_id].number_of_pages;
	return num_pages;
}

/*-------------------------------------------------------*
*  FUNCTION : mon_memory_allocate()
*  PURPOSE  : Allocates contiguous buffer of given size, filled with zeroes
*  ARGUMENTS: IN uint32_t size - size of the buffer in bytes
*  RETURNS  : void*  address of allocted buffer if OK, NULL if failed
*-------------------------------------------------------*/
void *mon_memory_allocate(
#ifdef DEBUG
	char *file_name, int32_t line_number,
#endif
	IN uint32_t size)
{
	void *p_buffer = NULL;

	if (size == 0) {
		return NULL;
	}

	size = (uint32_t)ALIGN_FORWARD(size, PAGE_4KB_SIZE);
	p_buffer = mon_page_allocate(
#ifdef DEBUG
		file_name, line_number,
#endif
		(HEAP_PAGE_INT)(size / PAGE_4KB_SIZE));

	if (NULL != p_buffer) {
		mon_memset(p_buffer, 0, size);
	}

	return p_buffer;
}

void *mon_memory_allocate_must_succeed(
#ifdef DEBUG
	char *file_name, int32_t line_number,
#endif
	heap_alloc_handle_t handle, uint32_t size)
{
	void *allcated_mem = mon_memory_allocate(
#ifdef DEBUG
		file_name,
		line_number,
#endif
		size);
	uint32_t request_owner = (uint32_t)handle;

	if (allcated_mem == NULL) {
		uint32_t i;

		for (i = 0; i < num_of_registered_callbacks; i++) {
			if (i == request_owner) {
				continue;
			}

			free_mem_callbacks[i].callback_func(
				free_mem_callbacks[i].context);
		}

		allcated_mem = mon_memory_allocate(
#ifdef DEBUG
			file_name, line_number,
#endif
			size);
		MON_ASSERT(allcated_mem != NULL);
	}
	return allcated_mem;
}

#ifdef DEBUG

void mon_heap_show(void)
{
	HEAP_PAGE_INT i;

	MON_LOG(mask_anonymous, level_trace, "Heap Show: total_pages=%d\n",
		heap_total_pages);
	MON_LOG(mask_anonymous, level_trace, "---------------------\n");

	for (i = 0; i < heap_total_pages; ) {
		MON_LOG(mask_anonymous, level_trace, "Pages %d..%d ", i,
			i + heap_array[i].number_of_pages - 1);

		if (heap_array[i].in_use) {
			MON_LOG(mask_anonymous,
				level_trace,
				"allocated in %s line=%d\n",
				heap_array[i].file_name,
				heap_array[i].line_number);
		} else {
			MON_LOG(mask_anonymous, level_trace, "free\n");
		}

		i += heap_array[i].number_of_pages;
	}
	MON_LOG(mask_anonymous, level_trace, "---------------------\n");
}
#endif
