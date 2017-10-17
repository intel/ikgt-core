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
#ifndef _VMM_BASE_H_
#define _VMM_BASE_H_

#define UNUSED  __attribute__((unused))
#define PACKED  __attribute((packed))
/* PACKED should be applied to below cases
 * 1.the size of the struct is not 32bit aligned. e.g. gdtr64_t.
 * 2.the member of the struct is not aligned with it's size.
 *   e.g. tss64_t (rsp is 64bit but it's offset is 32bit, not 64bit aligned).
 * 3.the struct is not aligned with max size of the member. e.g. acpi_table_rsdp_t.
 */

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef uint32_t boolean_t;
typedef struct {
	uint64_t uint64[2];
} uint128_t;

#define FALSE 0
#define TRUE 1

#define NULL ((void *)0)

#define ALIGN_B(value, align) \
	((uint64_t)(value) & (~((uint64_t)(align) - 1ULL)))
#define ALIGN_F(value, align) \
	ALIGN_B((uint64_t)value + (uint64_t)align - 1, align)

#define KILOBYTE            *1024ULL
#define MEGABYTE            *1024ULL KILOBYTE
#define GIGABYTE            *1024ULL MEGABYTE

#define PAGE_4K_SHIFT 12
#define PAGE_4K_SIZE 		(4 KILOBYTE)
#define PAGE_4K_MASK 		(PAGE_4K_SIZE - 1)
#define PAGE_ALIGN_4K(x)    ALIGN_F(x, PAGE_4K_SIZE)
/* Returns number of pages (4KB) required to accomdate x bytes */
#define PAGE_4K_ROUNDUP(x)  (((x) + PAGE_4K_SIZE - 1) >> PAGE_4K_SHIFT)

#define PAGE_2MB_SIZE       (2 MEGABYTE)
#define PAGE_2MB_MASK       (PAGE_2MB_SIZE - 1)
#define PAGE_ALIGN_2M(x)    ALIGN_F(x, PAGE_2MB_SIZE)

#define PAGE_4MB_SIZE       (4 MEGABYTE)
#define PAGE_4MB_MASK       (PAGE_4MB_SIZE - 1)
#define PAGE_ALIGN_4M(x)    ALIGN_F(x, PAGE_4MB_SIZE)

#define PAGE_1GB_SIZE       (1 GIGABYTE)
#define PAGE_1GB_MASK       (PAGE_1GB_SIZE - 1)
#define PAGE_ALIGN_1G(x)    ALIGN_F(x, PAGE_1GB_SIZE)

#ifndef MAX
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b)   ((b) > (a) ? (a) : (b))
#endif

#define BITARRAY_SET(__bitbase, __bitoffset)  (asm_bts64((__bitbase), (__bitoffset)))
#define BITARRAY_CLR(__bitbase, __bitoffset)  (asm_btr64((__bitbase), (__bitoffset)))
#define BITARRAY_GET(__bitbase, __bitoffset)  \
	(((((uint8_t *)(__bitbase))[(__bitoffset) >> 3]) >> \
			((__bitoffset) & 0x7)) & 0x1)

/* according to IA32 spec, shift left/shift right instructions (SAL/SAR/SHL/SHR)
* treat the "count" as "count % 64" (for 32 bit, it is 32).
* that is, m << n == m << (n%64), m >> n == m >> (n%64)
* that is, 1ULL << 64 == 1ULL, while usually we think it is 0 in our code
*/
#define BIT(n) (((n) >= 64U)?0:1ULL<<(n))
#define MASK64_LOW(n) ((BIT(n)) - 1)
#define MASK64_MID(h, l) ((BIT((h) + 1)) - (BIT(l)))
#define MAKE64(high, low) (((uint64_t)(high))<<32 | (((uint64_t)(low)) & MASK64_LOW(32)))

#define OFFSET_OF(__struct, __member) ((uint64_t)&(((__struct *)0)->__member))

#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif

#ifdef DEBUG
#define D(__xxx) __xxx
#define __STOP_HERE__ \
	while (1) { \
	}
#else
#define D(__xxx)
#define __STOP_HERE__ \
	while (1) { \
		asm_hlt(); \
	}
#endif

static inline boolean_t addr_is_canonical(boolean_t is_64bit, uint64_t addr)
{
	if (is_64bit)
	{
		if ((((addr) >> 47) == 0) || (((addr) >> 47) == 0x1ffffull))
			return TRUE;
	}else{
		if((((addr) >> 32) == 0))
			return TRUE;
	}
	return FALSE;
}

#endif
