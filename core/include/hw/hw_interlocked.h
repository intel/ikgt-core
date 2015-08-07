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

#ifndef _HW_INTERLOCKED_H_
#define _HW_INTERLOCKED_H_

#include "mon_defs.h"

/*
 * Various interlocked routines
 */

/*-------------------------------------------------------------------------
 * returns previous value
 * Assigns new value if previous value == expected_value
 * If previous value != expected_value do not change it
 *
 * Compare returned value with expected to discover.
 *
 * int32_t ASM_FUNCTION
 * hw_interlocked_compare_exchange(
 *     volatile int32_t* p_number,
 *     int32_t expected_value,
 *     int32_t new_value
 * );
 *------------------------------------------------------------------------- */
int32_t gcc_interlocked_compare_exchange(volatile int32_t *destination,
					 int32_t exchange,
					 int32_t comperand);

#define hw_interlocked_compare_exchange(p_number, expected_value, new_value) \
	((int32_t)gcc_interlocked_compare_exchange(                          \
	(volatile int32_t *)(p_number),                                      \
	(int32_t)(new_value),                                                \
	(int32_t)(expected_value)))

/*-------------------------------------------------------------------------
 * returns previous value
 * Assigns new value if previous value == expected_value
 * If previous value != expected_value do not change it
 *
 * Compare returned value with expected to discover.
 *
 * int64_t ASM_FUNCTION
 * hw_interlocked_compare_exchange_8(
 *     volatile uint64_t *p_number,
 *     int64_t expected_value,
 *     int64_t new_value
 * );
 *------------------------------------------------------------------------- */
int64_t gcc_interlocked_compare_exchange_8(volatile int64_t *destination,
					   int64_t exchange,
					   int64_t comperand);

#define hw_interlocked_compare_exchange_8(p_number, expected_value, new_value) \
	((int64_t)gcc_interlocked_compare_exchange_8(                          \
	(volatile int64_t *)(p_number),                                        \
	(int64_t)(new_value),                                                  \
	(int64_t)(expected_value)))

/*-------------------------------------------------------------------------
 *
 * Decrement value by 1
 *
 * int32_t ASM_FUNCTION
 * hw_interlocked_decrement(
 *     volatile int32_t *p_counter
 * );
 *------------------------------------------------------------------------- */
int32_t hw_interlocked_decrement(int32_t *minuend);

/*-------------------------------------------------------------------------
 *
 * Decrement value by 1
 *
 * int32_t ASM_FUNCTION
 * hw_interlocked_increment(
 *     volatile int32_t *p_counter
 * );
 *------------------------------------------------------------------------- */
int32_t hw_interlocked_increment(int32_t *addend);
int32_t hw_interlocked_increment64(int64_t *addend);

/*-------------------------------------------------------------------------
 *
 * This function guarantees to return the old value at the time of the addition
 *
 *
 * int32_t ASM_FUNCTION
 * hw_interlocked_add(
 *     volatile int32_t *p_counter,
 *     int32_t addend
 * );
 *------------------------------------------------------------------------- */
int32_t hw_interlocked_add(volatile int32_t *addend, int32_t value);

/*-------------------------------------------------------------------------
 *
 * returns previous value
 *
 * int32_t ASM_FUNCTION
 * hw_interlocked_bit_or(
 *     volatile int32_t* p_bit_set,
 *     int32_t mask
 * );
 *------------------------------------------------------------------------- */
int32_t hw_interlocked_or(volatile int32_t *value, int32_t mask);

/*-------------------------------------------------------------------------
 *
 * returns previous value
 *
 * int32_t ASM_FUNCTION
 * hw_interlocked_bit_and(
 *     volatile int32_t* p_bit_set,
 *     int32_t mask
 * );
 *------------------------------------------------------------------------- */
int32_t hw_interlocked_and(volatile int32_t *value, int32_t mask);

/*-------------------------------------------------------------------------
 *
 * returns previous value
 *
 * int32_t ASM_FUNCTION
 * hw_interlocked_bit_xor(
 * volatile int32_t* p_bit_set,
 * int32_t mask
 * );
 *------------------------------------------------------------------------- */
int32_t hw_interlocked_xor(volatile int32_t *value, int32_t mask);

/*-------------------------------------------------------------------------
 *
 * returns previous value
 *
 * int32_t ASM_FUNCTION
 * hw_interlocked_assign(
 * volatile int32_t* p_number,
 * int32_t new_value
 * );
 *------------------------------------------------------------------------- */
int32_t hw_interlocked_assign(volatile int32_t *target, int32_t new_value);

void hw_store_fence(void);

/*-------------------------------------------------------------------------
 *
 * returns nothing
 *
 * void hw_assign_as_barrier( volatile uint32_t* p_number, uint32_t new_value )
 *
 *------------------------------------------------------------------------- */
#define hw_assign_as_barrier(p_number, new_value) \
	do { \
		hw_store_fence(); \
		*(p_number) = (new_value); \
	} while (0)

/*-------------------------------------------------------------------------
 *
 * Execute assembler 'pause' instruction
 *
 *------------------------------------------------------------------------- */
void ASM_FUNCTION hw_pause(void);

/*-------------------------------------------------------------------------
 *
 * Execute assembler 'monitor' instruction
 *
 *------------------------------------------------------------------------- */
void ASM_FUNCTION hw_monitor(void *addr, uint32_t extension, uint32_t hint);

/*-------------------------------------------------------------------------
 *
 * Execute assembler 'mwait' instruction
 *
 *------------------------------------------------------------------------- */
void ASM_FUNCTION hw_mwait(uint32_t extension, uint32_t hint);

#endif    /* _HW_INTERLOCKED_H_ */
