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

#include "vmm_base.h"
#include "lock.h"
#include "heap.h"
#include "dbg.h"
#include "vmm_asm.h"
#include "lib/util.h"

#define local_print(fmt, ...)
//#define local_print(fmt, ...) vmm_printf(fmt, ##__VA_ARGS__)

#define HEAP_POINTER_TO_ID(__base, __pointer, __shift) \
	(((uint32_t)((uint64_t)(__pointer) - (__base))) >> (__shift))
#define CHECK_ADDRESS_IN_RANGE(addr, range_start, size) \
	(((uint64_t)(addr)) >= ((uint64_t)(range_start)) && ((uint64_t)(addr)) < \
	 ((uint64_t)(range_start)) + (size))

static inline void heap_clear_bits(uint64_t *mask, uint32_t from, uint32_t to)
{
	uint32_t i = 0;

	for (i = from; i < to; i++) {
		BITARRAY_CLR(mask, i);
	}
}

static inline void heap_set_bits(uint64_t *mask, uint32_t from, uint32_t to)
{
	uint32_t i = 0;

	for (i = from; i < to; i++) {
		BITARRAY_SET(mask, i);
	}
}

#define PAGE_ID_TO_POINTER(__base, __id) \
	((void *)(uint64_t)(__base + (__id * PAGE_4K_SIZE)))

typedef struct {
	/* address at which the heap is located */
	uint64_t heap_base;
	/* actual number of pages */
	uint32_t page_num;
	uint32_t pad;
	/* the array contains the number of continuous available pages */
	uint32_t *cont_num_array;
	/* array for page status bit, 1:used, 0: avail*/
	uint64_t *status_array;
	/* this is a temporary backup for status_array, only used in the page_alloc_unprotected */
	uint64_t *status_array_copy;
	/* the number of page status array */
	uint64_t status_array_len;

} heap_page_descriptor_t;

static heap_page_descriptor_t page_descriptor;
static vmm_lock_t heap_lock = {0};

static void vmm_heap_show(void)
{
	uint32_t i;

	print_info("Heap Show: total_pages=%d\n", page_descriptor.page_num);
	print_info("---------------------\n");

	for (i = 0; i < page_descriptor.page_num; ) {
		print_info("Pages %d..%d ", i,
			i + page_descriptor.cont_num_array[i] - 1);

		if (BITARRAY_GET(&page_descriptor.status_array[0], i) == 0) {
			print_info("used\n");
		} else {
			print_info("avail\n");
		}

		i += page_descriptor.cont_num_array[i];
	}
	print_info("---------------------\n");
}

void vmm_heap_initialize(IN uint64_t heap_buffer_address,
				  IN uint64_t heap_buffer_size)
{
	uint64_t unaligned_heap_base;
	uint32_t number_of_pages;
	uint32_t i;

	print_trace( "HEAP: heap base address = 0x%llX, total size = 0x%llx\n",
		heap_buffer_address, heap_buffer_size);

	/* to be on the safe side */
	heap_buffer_address = ALIGN_F(heap_buffer_address, sizeof(uint64_t));

	/* calculate how many unaligned pages we can support */
	number_of_pages = (uint32_t)(heap_buffer_size >> PAGE_4K_SHIFT);
	page_descriptor.status_array_len = (number_of_pages + ((1 << 6) - 1)) >> 6;

	/* heap descriptors placed at the beginning */
	page_descriptor.status_array = (uint64_t *)heap_buffer_address;
	unaligned_heap_base = (uint64_t)&page_descriptor.status_array[page_descriptor.status_array_len];
	page_descriptor.status_array_copy = (uint64_t *)unaligned_heap_base;
	unaligned_heap_base = (uint64_t)&page_descriptor.status_array_copy[page_descriptor.status_array_len];
	page_descriptor.cont_num_array = (uint32_t *)unaligned_heap_base;
	unaligned_heap_base = (uint64_t)&page_descriptor.cont_num_array[number_of_pages];

	/* but on the 1st 4K boundary address
	 * here 4K pages start */
	page_descriptor.heap_base = ALIGN_F(unaligned_heap_base, PAGE_4K_SIZE);

	/* decrement heap size, due to descriptor allocation and alignment */
	heap_buffer_size -= page_descriptor.heap_base - heap_buffer_address;

	/* now we can get actual number of available 4K pages */
	page_descriptor.page_num = (uint32_t)(heap_buffer_size >> PAGE_4K_SHIFT);
	page_descriptor.status_array_len = (page_descriptor.page_num + ((1 << 6) - 1)) >> 6;

	D(VMM_ASSERT(page_descriptor.page_num > 0));

	/* mark all page status as available */
	memset(page_descriptor.status_array, 0xff, (page_descriptor.status_array_len - 1) * 8);
	page_descriptor.status_array[page_descriptor.status_array_len - 1] = MASK64_LOW(page_descriptor.page_num & 0x3f);

	/* calculate the size of continuous available pages */
	for (i = 0; i < page_descriptor.page_num; i++) {
		page_descriptor.cont_num_array[i] = (page_descriptor.page_num - i);
	}

	/* there's no need to add continuous block count in the last available block */
	/* *(uint32_t *)(page_descriptor.heap_base + ((page_descriptor.page_num - 1) * PAGE_4K_SIZE)) = page_descriptor.page_num; */

	lock_init(&heap_lock, "heap_lock");

	print_trace("HEAP: heap_base = 0x%llX, page_num = %d, cont_num_array = 0x%llX, status_array = 0x%llX, status_array_copy = 0x%llX, status_array_len = %d\n",
		page_descriptor.heap_base, page_descriptor.page_num, page_descriptor.cont_num_array,
		page_descriptor.status_array, page_descriptor.status_array_copy, page_descriptor.status_array_len);
}

static uint32_t find_avail_page(uint32_t page_num)
{
	uint32_t i = 0;
	uint32_t page_id = 0;

	memcpy(page_descriptor.status_array_copy, page_descriptor.status_array, page_descriptor.status_array_len * 8);

	while (i < page_descriptor.status_array_len) {
		if (page_descriptor.status_array_copy[i] == 0) {
			i++;
		} else {
			page_id = i * 64 + asm_bsf64(page_descriptor.status_array_copy[i]);
			if (page_descriptor.cont_num_array[page_id] >= page_num) {
				return page_id;
			}
			/* If the number of continuous pages can't meet the number of request
			 * clear the mask bits, continue to find */
			heap_clear_bits(page_descriptor.status_array_copy, page_id, page_id + page_descriptor.cont_num_array[page_id]);
		}
	}

	return page_descriptor.page_num;
}

static void *page_alloc_internal(uint32_t page_num)
{
	uint32_t j = 0;
	uint32_t page_id = 0;
	uint32_t *buf = NULL;
	void *ptr = NULL;

	lock_acquire_write(&heap_lock);

	/* find the suitable page */
	page_id = find_avail_page(page_num);
	local_print("HEAP: find available page id: %d\n", page_id);

	if (page_id < page_descriptor.page_num) {
		/* validity check */
		D(VMM_ASSERT((page_id + page_descriptor.cont_num_array[page_id]) <= page_descriptor.page_num));

		if (page_descriptor.cont_num_array[page_id] > page_num ) {
			buf = (uint32_t *)PAGE_ID_TO_POINTER(page_descriptor.heap_base,
					(page_id + page_descriptor.cont_num_array[page_id] - 1));
			/* update continuous page count in the last available page */
			*buf = page_descriptor.cont_num_array[page_id] - page_num;
		}

		/* mark page as in_use, record allocated block number */
		BITARRAY_CLR((uint64_t *)page_descriptor.status_array, page_id);
		page_descriptor.cont_num_array[page_id] = page_num;

		local_print("HEAP: start available page id: %d, page num: %d\n",
					page_id, page_num);

		/* mark next num-1 pages as in_use */
		for (j = page_id + 1; j < (page_id + page_num); j++) {
			BITARRAY_CLR((uint64_t *)page_descriptor.status_array, j);
			page_descriptor.cont_num_array[j] = 0;
		}

		ptr = PAGE_ID_TO_POINTER(page_descriptor.heap_base, page_id);
	}

	lock_release(&heap_lock);

	return ptr;
}

void *page_alloc(IN uint32_t page_num)
{
	void *ptr = NULL;

	VMM_ASSERT_EX(page_num, "HEAP: try to allocate zero page\n");

	ptr = page_alloc_internal(page_num);

	if (ptr == NULL) {
		print_panic("HEAP: fail to allocate %d pages\n", page_num);
		vmm_heap_show();
		VMM_DEADLOOP();
	}

	return ptr;
}

void page_free(IN void *ptr)
{
	uint64_t page_addr = (uint64_t)(uint64_t)ptr;
	uint32_t *buf = NULL;
	uint32_t id_from;     /* first page to free */
	uint32_t id_to;       /* page next to last to free */
	uint32_t pages_to_free;         /* number of pages, nead to be freed */
	uint32_t i;

	lock_acquire_write(&heap_lock);

	VMM_ASSERT_EX(CHECK_ADDRESS_IN_RANGE(page_addr, page_descriptor.heap_base,
		page_descriptor.page_num * PAGE_4K_SIZE),
		"HEAP: Buffer 0x%llx is out of heap space\n", page_addr);

	VMM_ASSERT_EX(((page_addr & PAGE_4K_MASK) == 0),
		"HEAP: page address 0x%llX isn't 4K page aligned\n", page_addr);

	/* find the start page id according to the given memory address */
	id_from = HEAP_POINTER_TO_ID(page_descriptor.heap_base, page_addr, PAGE_4K_SHIFT);

	if (1 == BITARRAY_GET(&page_descriptor.status_array[0], id_from) ||
		0 == page_descriptor.cont_num_array[id_from]) {
		print_panic("HEAP: Page %d is not in use\n",
			id_from);
		VMM_DEADLOOP();
		return;
	}

	/* get the number of pages that need to be freed */
	pages_to_free = page_descriptor.cont_num_array[id_from];
	D(VMM_ASSERT(pages_to_free));
	/* calculate the end page id */
	id_to = id_from + pages_to_free;
	/* mark the freed page is available */
	heap_set_bits((uint64_t *)page_descriptor.status_array, id_from, id_to);

	/* check if the next to the last released page is free */
	/* and if so merge both regions */
	if (id_to < page_descriptor.page_num &&
		1 == BITARRAY_GET(&page_descriptor.status_array[0], id_to) &&
		(id_to + page_descriptor.cont_num_array[id_to]) <= page_descriptor.page_num) {
				/* merge the following available pages */
		pages_to_free += page_descriptor.cont_num_array[id_to];
		/* NOTE: id_to is not changed here
		* because there's no need to update the status/size of the next available pages */
	}

	/* move backward, to merge all available pages, trying to prevent fragmentation */
	if (id_from > 0 &&
		1 == BITARRAY_GET(&page_descriptor.status_array[0], (id_from - 1)) &&
		0 != page_descriptor.cont_num_array[id_from - 1]) {
		buf = (uint32_t *)PAGE_ID_TO_POINTER(page_descriptor.heap_base, (id_from - 1));
		/* merge the previous available pages*/
		pages_to_free += *buf;
		/* recalculate the start page id */
		id_from -= *buf;
	}

	print_trace("HEAP: free page num %d from %d to %d\n",
		pages_to_free, id_from, id_to);

	/* recalculate the size of continuous available pages */
	for (i = id_from; i < id_to; ++i) {
		page_descriptor.cont_num_array[i] = pages_to_free - (i - id_from);
	}

	/* add continuous page count in the last page. It will be used for acceleration in free */
	/* pages_to_free includes the number of all merged pages */
	buf  = (uint32_t *)PAGE_ID_TO_POINTER(page_descriptor.heap_base, (id_from + pages_to_free - 1));
	*buf = pages_to_free;

	lock_release(&heap_lock);
}

#define POOL_BLOCK_NUM_IN_NODE 238
#define POOL_STATUS_NUM_IN_NODE 4
#define POOL_NODE_HEAD_SIZE 288
#define POOL_MAX_ALLOCATED_MEM 3808   //POOL_BLOCK_NUM_IN_NODE * 16
#define POOL_BLOCK_SIZE_SHIFT 4
#define POOL_SIZE_OF_BLOCK (1u << POOL_BLOCK_SIZE_SHIFT)

/*pool_node_t is one page (sizeof(pool_node_t) == 0x1000)*/
typedef struct pool_node_t{
	struct pool_node_t *prev;
	struct pool_node_t *next;
	uint64_t status_array[POOL_STATUS_NUM_IN_NODE];  //block status bitmap 1:avail , 0:used
	uint8_t cont_num_array[POOL_BLOCK_NUM_IN_NODE];   //the number of continuous blocks
	uint8_t avail_blocks; //total available blocks, it's not continuous
	uint8_t pad[1];
	uint128_t block[POOL_BLOCK_NUM_IN_NODE];
}pool_node_t;

static pool_node_t *pool_head;
static vmm_lock_t pool_lock = {0};

static void vmm_pool_show(void)
{
	UNUSED uint32_t i = 0, n;
	pool_node_t *node;

	print_info("Pool Show: \n");
	print_info("----------------\n");

	node = pool_head;
	while (node != NULL) {
		print_info("Node %d unused %d at %p\n", ++i, node->avail_blocks, node);
		for (n = 0; n < POOL_STATUS_NUM_IN_NODE; n++) {
			print_info("status[%d]=0x%llx\n", n, node->status_array[n]);
		}
		node = node->next;
	}
	print_info("----------------\n");
}

void vmm_pool_initialize(void)
{
	lock_init(&pool_lock, "pool_lock");
}

static pool_node_t *pool_get_new_node(void)
{
	pool_node_t *node;
	uint8_t i;

	node = (pool_node_t *)page_alloc_internal(1);

	if (node == NULL) {
		print_panic("POOL: allocated node is NULL\n");
		return NULL;
	}

	node->status_array[0] = 0xffffffffffffffffULL;
	node->status_array[1] = 0xffffffffffffffffULL;
	node->status_array[2] = 0xffffffffffffffffULL;
	/* there're 238 available blocks in a node */
	node->status_array[3] = 0x00003fffffffffffULL;

	node->avail_blocks = POOL_BLOCK_NUM_IN_NODE;
	for (i = 0; i < POOL_BLOCK_NUM_IN_NODE; ++i) {
		node->cont_num_array[i] = (POOL_BLOCK_NUM_IN_NODE - i);
	}
	/* there's no need to add continuous block count in the last available block */
	/*node->block[237].uint64[0] = 238;*/

	local_print("POOL: allocated node: 0x%llX\n", node);
	return node;
}

static uint32_t find_avail_block(pool_node_t *node, uint32_t block_num)
{
	uint32_t i = 0;
	uint32_t block_id = 0;
	uint64_t status_array_copy[POOL_STATUS_NUM_IN_NODE];

	memcpy(status_array_copy, node->status_array, sizeof(node->status_array));

	while (i < POOL_STATUS_NUM_IN_NODE) {
		if (status_array_copy[i] == 0) {
			i++;
		} else {
			block_id = i * 64 + asm_bsf64(status_array_copy[i]);
			if (node->cont_num_array[block_id] >= block_num) {
				return block_id;
			}
			/* If the number of continuous blocks can't meet the number of request
			 * clear the mask bits, continue to find */
			heap_clear_bits(status_array_copy, block_id, block_id + node->cont_num_array[block_id]);
		}
	}

	return POOL_BLOCK_NUM_IN_NODE;
}

static void *pool_alloc_internal(pool_node_t *node, uint8_t block_num)
{
	uint8_t j = 0;
	uint8_t block_id = 0;
	uint32_t *buf = NULL;
	void *ptr = NULL;

	/* find the suitable block */
	block_id = find_avail_block(node, block_num);
	local_print("POOL: find available block id: %d\n", block_id);

	if (block_id < POOL_BLOCK_NUM_IN_NODE) {
		/* validity check */
		VMM_ASSERT_EX(((block_id + node->cont_num_array[block_id]) <= POOL_BLOCK_NUM_IN_NODE),
			"POOL: the continuous block count(%u) in block(%u) is invalid\n",
			node->cont_num_array[block_id], block_id);

		/* update the total number of available blocks in this node */
		node->avail_blocks -= block_num;
		if (node->cont_num_array[block_id] > block_num ) {
			/* update continuous block count in the last available block */
			buf = (uint32_t *)&node->block[block_id + node->cont_num_array[block_id] - 1];
			*buf = node->cont_num_array[block_id] - block_num;
		}

		/* mark block as in_use, record allocated block number */
		BITARRAY_CLR((uint64_t *)&node->status_array, block_id);
		node->cont_num_array[block_id] = block_num;

		local_print("POOL: start available block id: %d, block num: %d from node 0x%llX\n",
					block_id, block_num, node);

		/* mark next num-1 blocks as in_use */
		for (j = block_id + 1; j < (block_id + block_num); j++) {
			BITARRAY_CLR((uint64_t *)&node->status_array, j);
			node->cont_num_array[j] = 0;
		}

		ptr = (void *)&node->block[block_id];
	}

	return ptr;
}

/* the memory allocated is 16 bytes aligned */
static void *pool_alloc(IN uint32_t size)
{
	pool_node_t *cur_node;
	pool_node_t *new_node;
	uint8_t block_num = 0;
	void *ptr = NULL;

	VMM_ASSERT_EX(size, "POOL: try to allocate NULL\n");

	D(if ((size & 0x7) != 0) print_warn("POOL: allocation size is not a multiple of 8 bytes\n"));

	lock_acquire_write(&pool_lock);

	/* calculate hom many block should be allocated */
	block_num = (size + ((POOL_SIZE_OF_BLOCK) - 1)) >> POOL_BLOCK_SIZE_SHIFT;

	/* check if pool is null, and create the first node */
	if (pool_head == NULL) {
		pool_head = pool_get_new_node();
		if (pool_head == NULL) {
			goto error;
		}
		pool_head->next = NULL;
		pool_head->prev = NULL;
	}

	cur_node = pool_head;
	while (1) {
		if (block_num <= cur_node->avail_blocks) {
			ptr = pool_alloc_internal(cur_node, block_num);
			if (ptr != NULL) {
				break;
			}
		}
		/* if all the nodes have been allocated in the pool, create a new node */
		if (cur_node->next == NULL) {
			new_node = pool_get_new_node();
			if (new_node == NULL) {
			 	goto error;
			}
			new_node->next = NULL;
			new_node->prev = cur_node;
			cur_node->next = new_node;
		}
		cur_node = cur_node->next;
	}

	lock_release(&pool_lock);

	return ptr;

error:
	print_panic("POOL: fail to allocate %d bytes memory\n", size);
	vmm_pool_show();
	vmm_heap_show();
	VMM_DEADLOOP();
	return NULL;
}

static void pool_free_node(pool_node_t * node)
{
	if (node->next != NULL) {
		node->next->prev = node->prev;
	}

	if (node->prev != NULL) {
			node->prev->next = node->next;
	} else {
		pool_head = node->next;
	}

	page_free((void *)node);
	print_trace("POOL: free page: 0x%llX\n", node);
}

#ifdef DEBUG
static boolean_t pool_check_node(pool_node_t *list, pool_node_t *node)
{
	while (list != NULL) {
		if (list == node) {
			return TRUE;
		}
		list = list->next;
	}
	return FALSE;
}
#endif

static void pool_free(void *ptr)
{
	uint64_t data_addr = (uint64_t)ptr;
	uint64_t page_addr = ALIGN_B(data_addr, PAGE_4K_SIZE);
	pool_node_t *node = (pool_node_t *)page_addr;
	uint32_t *buf = NULL;
	uint32_t id_from;     /* first block to free */
	uint32_t id_to;       /* block next to last to free */
	uint32_t blocks_to_free;    /* block number, need to be freed */
	uint32_t i;

	lock_acquire_write(&pool_lock);

	/* Check whether the node that need to free blocks is in the pool list */
	D(VMM_ASSERT(pool_check_node(pool_head, node)));

	VMM_ASSERT_EX(CHECK_ADDRESS_IN_RANGE(data_addr, (page_addr + POOL_NODE_HEAD_SIZE),
			POOL_MAX_ALLOCATED_MEM), "POOL: free data is out of pool range\n");

	/* find the start block id according to the given memory address */
	id_from = HEAP_POINTER_TO_ID(page_addr + POOL_NODE_HEAD_SIZE, data_addr, POOL_BLOCK_SIZE_SHIFT);

	if (1 == BITARRAY_GET(&node->status_array[0], id_from) ||
		0 == node->cont_num_array[id_from]) {
		print_panic("POOL: block %d is not in use\n",
		id_from);
		VMM_DEADLOOP();
		return;
	}

	/* get the number of blocks that need to be freed */
	blocks_to_free = node->cont_num_array[id_from];
	D(VMM_ASSERT(blocks_to_free));
	/* update the total number of available blocks in this node */
	node->avail_blocks += blocks_to_free;
	/* if all blocks need to be freed in this node , free node directly */
	if (node->avail_blocks == POOL_BLOCK_NUM_IN_NODE) {
		pool_free_node(node);
		lock_release(&pool_lock);
		return ;
	}

	/* calculate the end block id */
	id_to = id_from + blocks_to_free;
	/* mark the freed block is available */
	heap_set_bits((uint64_t *)&node->status_array, id_from, id_to);

	/* check if the next to the last freed block is available */
	/* and if so merge both regions */
	if (id_to < POOL_BLOCK_NUM_IN_NODE &&
		1 == BITARRAY_GET(&node->status_array[0], id_to) &&
		(id_to + node->cont_num_array[id_to]) <=
		POOL_BLOCK_NUM_IN_NODE) {
		/* merge the following available blocks */
		blocks_to_free += node->cont_num_array[id_to];
		/* NOTE: id_to is not changed here
		 * because there's no need to update the status/size of the next available blocks */
	}

	/* move backward, to merge all previous available blocks, trying to prevent fragmentation */
	if (id_from > 0 &&
		1 == BITARRAY_GET(&node->status_array[0], (id_from - 1)) &&
		0 != node->cont_num_array[id_from - 1]) {
		buf = (uint32_t *)&node->block[id_from - 1];
		/* merge the previous available blocks*/
		blocks_to_free += *buf;
		/* recalculate the start block id */
		id_from -= *buf;
	}

	print_trace("POOL: free block num %d from %d to %d\n",
		blocks_to_free, id_from, id_to);

	/* recalculate the size of continuous available blocks */
	for (i = id_from; i < id_to; ++i) {
		node->cont_num_array[i] = blocks_to_free - (i - id_from);
	}

	/* add continuous block count in the last block. It will be used for acceleration in free */
	/* blocks_to_free includes the number of all merged blocks */
	buf = (uint32_t *)&node->block[id_from + blocks_to_free - 1];
	*buf  = blocks_to_free;

	lock_release(&pool_lock);
}

void *mem_alloc(IN uint32_t size)
{
	void *ptr;

	if (size <= POOL_MAX_ALLOCATED_MEM) {
		ptr = pool_alloc(size);
	} else {
		size = (uint32_t)ALIGN_F(size, PAGE_4K_SIZE);
		ptr = page_alloc((uint32_t)(size >> PAGE_4K_SHIFT));
	}

	return ptr;
}

void mem_free(IN void *ptr)
{
	uint64_t addr;

	addr = (uint64_t)ptr;
	if ((addr & 0xfff) == 0) {
		page_free(ptr);
	} else {
		pool_free(ptr);
	}
}
