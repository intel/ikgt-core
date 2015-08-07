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

#ifndef _MON_DEFS_H_
#define _MON_DEFS_H_

#include "common_types.h"

#define MON_MAX_CPU_SUPPORTED MAX_CPUS
#define MON_MAX_GUESTS_SUPPORTED 4

typedef enum {
	NO_ACCESS = 0,
	WRITE_ACCESS = 1,
	READ_ACCESS = 2,
	READ_WRITE_ACCESS = WRITE_ACCESS | READ_ACCESS,
} rw_access_t;

/* convert 32bit pointer to uint64_t */
#define PTR32_TO_UINT64(p32) ((uint64_t)(uint32_t)(p32))

#define MON_GUID_DATA4_SIZE 8
typedef struct {
	uint32_t	data1;
	uint16_t	data2;
	uint16_t	data3;
	uint8_t		data4[MON_GUID_DATA4_SIZE];
} mon_guid_t;

typedef struct {
	uint16_t	year;
	uint8_t		month;
	uint8_t		day;
	uint8_t		hour;
	uint8_t		minute;
	uint8_t		second;
	uint8_t		pad1;
	uint32_t	nanosecond;
	int16_t		time_zone;
	uint8_t		daylight;
	uint8_t		pad2;
} mon_time_t;

typedef address_t size_t;

/*
 *  compile time alignment
 */

#define ALIGN_N(__type, __var, __alignment) \
	__type __attribute__((aligned(__alignment))) __var
#define NAKED_N(__type) (__attribute__ ((naked)) __type)

#define ALIGN16(__type, __var) ALIGN_N(__type, __var, 16)
#define ALIGN8(__type, __var)  ALIGN_N(__type, __var, 8)

#define UINT64_ALL_ONES ((uint64_t)-1)
#define UINT32_ALL_ONES ((uint32_t)-1)
#define UINT16_ALL_ONES ((uint16_t)-1)
#define UINT8_ALL_ONES  ((uint8_t)-1)
#define SIZE_T_ALL_ONES  ((size_t)-1)
#define INVALID_PHYSICAL_ADDRESS ((uint64_t)-1)

#define NELEMENTS(__array) (int32_t)(sizeof(__array) / sizeof(__array[0]))
#define OFFSET_OF(__struct, __member) ((size_t)&(((__struct *)0)->__member))
#define ALIGN_BACKWARD(__address, __bytes) \
	((address_t)(__address) & ~((__bytes) - 1))
#define ALIGN_FORWARD(__address, __bytes) \
	ALIGN_BACKWARD(__address + __bytes - 1, __bytes)
#define ADDRESSES_ON_THE_SAME_PAGE(__addr1, __addr2) \
	(ALIGN_BACKWARD(__addr1, PAGE_4KB_SIZE) == \
	 ALIGN_BACKWARD(__addr2, PAGE_4KB_SIZE))
#define IS_NEGATIVE(__n) (((int)(__n)) < 0)
#define SIGN_EXTENSION(__n) \
	(IS_NEGATIVE(__n) ? (((uint64_t)0xffffffff) << \
		32) | __n : (uint64_t)__n)
#define IS_POW_OF_2(__n)    (((__n) & ((__n) - 1)) == 0)
#define IS_ALIGN(__address, __bytes) \
	(ALIGN_BACKWARD(__address, __bytes) == __address)

extern uint32_t align_forward_to_power_of_2(uint64_t number);
#define ALIGN_FORWARD_TO_POW_OF_2(__n) (align_forward_to_power_of_2(__n))

#define BITMAP_SET(__word, __mask) ((__word) |= (__mask))
#define BITMAP_CLR(__word, __mask) ((__word) &= ~(__mask))
#define BITMAP_GET(__word, __mask) ((__word) & (__mask))
#define BITMAP_ASSIGN(__word, __mask, __value) {                               \
		BITMAP_CLR(__word, __mask);                                                \
		__word |= BITMAP_GET(__value, __mask);                                     \
}

#define BITMAP_SET64(__word, __mask) ((__word) |= (uint64_t)(__mask))
#define BITMAP_CLR64(__word, __mask) ((__word) &= ~(uint64_t)(__mask))
#define BITMAP_GET64(__word, __mask) ((__word) & (uint64_t)(__mask))
#define BITMAP_ASSIGN64(__word, __mask, __value) {                             \
		BITMAP_CLR64(__word, __mask);                                              \
		__word |= BITMAP_GET64(__value, __mask);                                   \
}

#define BIT_VALUE(__bitno) (1 << (__bitno))
#define BIT_SET(__word, __bitno) BITMAP_SET(__word, 1 << (__bitno))
#define BIT_CLR(__word, __bitno) BITMAP_CLR(__word, 1 << (__bitno))
#define BIT_GET(__word, __bitno) (((__word) >> (__bitno)) & 1)

#define BIT_VALUE64(__bitno) ((uint64_t)1 << (__bitno))
#define BIT_SET64(__word, __bitno) BITMAP_SET(__word, (uint64_t)1 << (__bitno))
#define BIT_CLR64(__word, __bitno) BITMAP_CLR(__word, (uint64_t)1 << (__bitno))
#define BIT_GET64(__word, __bitno) BIT_GET(__word, __bitno)

#define BITARRAY_SIZE_IN_BYTES(__size_in_bits) (((__size_in_bits) + 7) / 8)
#define BITARRAY(__name, __size_in_bits)   \
	uint8_t __name[BITARRAY_SIZE_IN_BYTES(__size_in_bits)]
#define BITARRAY_BYTE(__bitno)    ((__bitno) >> 3)
#define BITARRAY_MASK(__bitno)    (1 << ((__bitno) & 7))
#define BITARRAY_SET(__bitarray, __bitno)  \
	(__bitarray[BITARRAY_BYTE(__bitno)] |= BITARRAY_MASK(__bitno))
#define BITARRAY_CLR(__bitarray, __bitno)  \
	(__bitarray[BITARRAY_BYTE(__bitno)] &= ~BITARRAY_MASK(__bitno))
#define BITARRAY_GET(__bitarray, __bitno)  \
	(__bitarray[BITARRAY_BYTE(__bitno)] & BITARRAY_MASK(__bitno))

#define BITMAP_ARRAY64_BYTE(__bitno)    ((__bitno) >> 6)
#define BITMAP_ARRAY64_CLR(__bitarray64, __bitno)                    \
	BITMAP_CLR64(__bitarray64[BITMAP_ARRAY64_BYTE(__bitno)],         \
	(uint64_t)1 << (__bitno % 64))
#define BITMAP_ARRAY64_SET(__bitarray64, __bitno)                    \
	BITMAP_SET64(__bitarray64[BITMAP_ARRAY64_BYTE(__bitno)],         \
	(uint64_t)1 << (__bitno % 64))
#define BITMAP_ARRAY64_GET(__bitarray64, __bitno)                    \
	BITMAP_GET64(__bitarray64[BITMAP_ARRAY64_BYTE(__bitno)],         \
	(uint64_t)1 << (__bitno % 64))

/*------------------------------------------------------*
* PURPOSE  : Set bit vectors for all the bits which are
*            lower than threshold.  Clear all upper
*            bits as ZERO.
* ARGUMENTS: array_64  - an uint64_t array, pointer to
*                        array_64[0],array_64[1],...
*            len       - the length of array_64
*            threshold - if bit vector position is lower
*                        than threshold, then set it;
*                        if bit vector position is higher
*                        than threshold, then clear it;
*------------------------------------------------------*/
#define  BITMAP_ARRAY_ASSIGN(array_64, len, threshold)        \
	{                                                     \
		uint32_t idx;                                 \
		uint32_t quot = (threshold) / 64,             \
			rem = (threshold) % 64;               \
		for (idx = 0; idx < (len); idx++) {           \
			if (idx < quot) {                                       \
				(array_64)[idx] = UINT64_ALL_ONES;         \
			}                                                  \
			else if (idx > quot) {                             \
				(array_64)[idx] = 0;                       \
			}                                                  \
			else {                                             \
				(array_64)[idx] =                          \
					(uint64_t)((uint64_t)1 << rem) - 1;\
			}                                                  \
		}                                                          \
	}

/*------------------------------------------------------*
* PURPOSE  : Check if all bits of bitmap array are reset
* ARGUMENTS: array_64  - an uint64_t array, pointer to
*                        array_64[0],array_64[1],...
*            len       - the length of array_64
*	      ret -	  1 if all bits are cleared,
*                        0 otherwise
*------------------------------------------------------*/
#define  BITMAP_ARRAY64_CHECKBITS_ALLZERO(array_64, len, ret) \
	{                                                     \
		uint32_t idx;                                 \
		(ret) = 1;                                    \
		for (idx = 0; idx < (len); idx++) {           \
			if ((array_64)[idx] != 0) {           \
				(ret) = 0;                    \
				break;                        \
			}                                     \
		}                                             \
	}

/*------------------------------------------------------*
* PURPOSE  : Check if all bits of bitmap array are set
* ARGUMENTS: array_64  - an uint64_t array, pointer to
*                        array_64[0],array_64[1],...
*            len       - the length of array_64
*	      ret -	  1 if all bits are set,
*                        0 otherwise
*------------------------------------------------------*/
#define  BITMAP_ARRAY64_CHECKBITS_ALLONE(array_64, len, ret)      \
	{                                                         \
		uint32_t idx;                                     \
		(ret) = 1;                                        \
		for (idx = 0; idx < (len); idx++) {               \
			if ((array_64)[idx] != UINT64_ALL_ONES) { \
				(ret) = 0;                        \
				break;                            \
			}                                         \
		}                                                 \
	}

/*------------------------------------------------------*
* PURPOSE  : Returns index of the highest bit set in the
*            bitmap array
* ARGUMENTS: array_64  - an uint64_t array, pointer to
*                        array_64[0],array_64[1],...
*            len       - the length of array_64
*            bit_index - index of the highest bit set,
*                        set to -1 if no bit is set in
*                        the bitmap array
*------------------------------------------------------*/
#define BITMAP_ARRAY64_HIGHESTINDEX(array_64, len, bit_index)                 \
	{                                                                     \
		int32_t i;                                                    \
		uint32_t index;                                               \
		(bit_index) = -1;                                             \
		for (i = ((len) - 1); i >= 0; i--) {                          \
			if (hw_scan_bit_backward64((uint32_t *)&index,        \
				    (array_64)[i])) {                         \
				(bit_index) =                                 \
					(index + (i * sizeof(uint64_t) * 8)); \
				break;                                        \
			}                                                     \
		}                                                             \
	}

/*
 *  enumerate all bits in BITARRAY
 *
 *  Call user-given function for each bit set.
 *  User-given function receives bit_number and arbitrary data, passed to
 *  enumerator */
typedef void (*func_bitarray_enum_t) (uint32_t bit_number,
				      void *data_for_this_func);
extern void bitarray_enumerate_bits(uint8_t *bitarray,
				    uint32_t bitarray_size_in_bits,
				    func_bitarray_enum_t cb,
				    void *cb_data);

#define BITARRAY_ENUMERATE(__name, __size_in_bits, __cb_func, __data_for_cb) \
	bitarray_enumerate_bits(__name, __size_in_bits, __cb_func, \
	__data_for_cb)

#define GET_BYTE(__word, __no) (((__word) >> ((__no) * 8)) & 0xFF)
#define GET_2BYTE(__word, __no)(((__word) >> ((__no) * 16)) & 0xFFFF)
#define GET_4BYTE(__word, __no)(((__word) >> ((__no) * 32)) & 0xFFFFFFFF)

#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif

#define KILOBYTE            *1024
#define KILOBYTES           KILOBYTE

#define MEGABYTE            *1024 KILOBYTES
#define MEGABYTES           MEGABYTE

#define GIGABYTE            *1024 MEGABYTES
#define GIGABYTES           GIGABYTE

#ifndef MAX
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b)   ((b) > (a) ? (a) : (b))
#endif

/*
 *
 *  Pages
 *
 */

/*
 *  The following definitions and macros will be used to perform alignment
 */
#define PAGE_4KB_SIZE       (4 KILOBYTES)
#define PAGE_4KB_MASK       (PAGE_4KB_SIZE - 1)
#define PAGE_ALIGN_4K(x)    ALIGN_FORWARD(x, PAGE_4KB_SIZE)
/*
 *  returns TRUE if the structure occupies the single page
 */
#define IS_ON_THE_SAME_4K_PAGE(address, size)                               \
	(ALIGN_BACKWARD(address, PAGE_4KB_SIZE) ==                          \
	ALIGN_BACKWARD(address + PAGE_4KB_SIZE))

/* Returns number of pages (4KB) required to accomdate x bytes */
#define PAGE_ROUNDUP(x)  (PAGE_ALIGN_4K(x) / PAGE_4KB_SIZE)

#define PAGE_2MB_SIZE       (2 MEGABYTES)
#define PAGE_2MB_MASK       (PAGE_2MB_SIZE - 1)
#define PAGE_ALIGN_2M(x)    ALIGN_FORWARD(x, PAGE_2MB_SIZE)
/*
 *  returns TRUE if the structure occupies the single page
 */
#define IS_ON_THE_SAME_2M_PAGE(address, size)                               \
	(ALIGN_BACKWARD(address, PAGE_2MB_SIZE) ==                          \
	ALIGN_BACKWARD(address + PAGE_2MB_SIZE))

#define PAGE_4MB_SIZE       (4 MEGABYTES)
#define PAGE_4MB_MASK       (PAGE_4MB_SIZE - 1)
#define PAGE_ALIGN_4M(x)    ALIGN_FORWARD(x, PAGE_4MB_SIZE)
/*
 *  returns TRUE if the structure occupies the single page
 */
#define IS_ON_THE_SAME_4M_PAGE(address, size)                               \
	(ALIGN_BACKWARD(address, PAGE_4MB_SIZE) ==                          \
	ALIGN_BACKWARD(address + PAGE_4MB_SIZE))

#define PAGE_1GB_SIZE       (1 GIGABYTES)
#define PAGE_1GB_MASK       (PAGE_1GB_SIZE - 1)
#define PAGE_ALIGN_1G(x)    ALIGN_FORWARD(x, PAGE_1GB_SIZE)

#define ADDRESS_TO_FN(addr)     ((address_t)(addr) >> 12)
#define FN_TO_ADDRESS(fn)       ((address_t)(fn) << 12)

#define MEMORY_REGIONS_NON_OVERLAPPED(__addr1, __size1, __addr2, __size2) \
	(((__addr1) + (__size1)) < (__addr2) || ((__addr2) + (__size2)) < \
	 (__addr1))

#define MEMORY_REGIONS_OVERLAPPED(__addr1, __size1, __addr2, __size2) \
	(!MEMORY_REGIONS_NON_OVERLAPPED(__addr1, __size1, __addr2, __size2))

/*
 *
 *  Calling conventions
 *
 */

#ifdef DEBUG
#define USED_IN_DEBUG_ONLY
#else
#define USED_IN_DEBUG_ONLY  UNUSED
#endif

#ifdef DEBUG
#define INLINE       static
#else
#define INLINE       static inline
#endif    /* ! DEBUG */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)   (sizeof(a) / sizeof(a[0]))
#endif

/*
 *
 *  varags
 *
 */

/*
 *  find size of parameter aligned on the native integer size
 */
#define _MON_INT_SIZE_OF(n)  \
	((sizeof(n) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))

#ifndef va_start

#define va_list         __builtin_va_list
#define va_start(ap, v)  __builtin_va_start((ap), v)
#define va_arg(ap, t)    __builtin_va_arg(ap, t)
#define va_end(ap)      __builtin_va_end

#endif

#define MON_UP_BREAKPOINT()                                 \
	{                                                   \
		volatile int __never_change = 1;            \
		while (__never_change)                      \
			;                                   \
	}

/*
 * The size of ap startup code and gtd table
 * must be less than AP_STARTUP_CODE_SIZE.
 * Update this value when not enough.
 */
#define AP_STARTUP_CODE_SIZE 0xD0
#endif    /* _MON_DEFS_H_ */
