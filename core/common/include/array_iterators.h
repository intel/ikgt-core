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

#ifndef _ARRAY_ITERATORS_H_
#define _ARRAY_ITERATORS_H_

#include "mon_defs.h"
#include "mon_dbg.h"

/****************************************************************************
*
* Implementation of array iterators template
*
****************************************************************************/

/*--------------------------------------------------------------------------
 *
 * Generic array iterator
 *
 * Usage:
 *
 * Provider:
 *
 * typedef generic_array_iterator_t;
 *
 *-------------------------------------------------------------------------- */

typedef struct {
	size_t		array_start;    /* pointer to the array start */
	uint32_t	start_idx;      /* count from */
	uint32_t	end_idx;        /* count to */
	uint32_t	addend;         /* count step - MUST be end_idx=start_idx+elem_count*addend */
	uint32_t	cur_idx;        /* current state */
} generic_array_iterator_t;

INLINE size_t generic_array_iterator_next(generic_array_iterator_t *ctx,
					  uint32_t elem_size)
{
	uint32_t ret_idx;

	MON_ASSERT(ctx != NULL);

	if (ctx->cur_idx != ctx->end_idx) {
		ret_idx = ctx->cur_idx;
		ctx->cur_idx += ctx->addend;
		return ctx->array_start + ret_idx * elem_size;
	}

	return 0;
}

INLINE size_t generic_array_iterator_first(uint32_t start_idx,
					   uint32_t elem_count,
					   uint32_t addend,
					   size_t array_start,
					   uint32_t elem_size,
					   generic_array_iterator_t *ctx)
{
	MON_ASSERT(ctx != NULL);

	ctx->array_start = array_start;
	ctx->start_idx = start_idx;
	ctx->end_idx = start_idx + elem_count * addend;
	ctx->addend = addend;
	ctx->cur_idx = start_idx;

	return generic_array_iterator_next(ctx, elem_size);
}

/*---------------------------------------------------------------------------
 *
 * Typeless generic macros
 *
 *--------------------------------------------------------------------------- */
#define GENERIC_ARRAY_ITERATOR_FIRST(elem_type, array_ptr, start_elem_idx,   \
				     number_of_entries, count_step, ctx_ptr) \
	((elem_type *)generic_array_iterator_first(start_elem_idx,            \
	number_of_entries,          \
	count_step,                 \
	(size_t)(array_ptr),        \
	(uint32_t)sizeof(elem_type),\
	ctx_ptr))

#define GENERIC_ARRAY_ITERATOR_NEXT(elem_type, ctx_ptr)   \
	((elem_type *)generic_array_iterator_next(ctx_ptr, \
	(uint32_t)sizeof(elem_type)))

/*---------------------------------------------------------------------------
 *
 * Typeless array iterator 0->end_of_array
 *
 *--------------------------------------------------------------------------- */
#define ARRAY_ITERATOR_FIRST(elem_type, array_ptr, number_of_entries,         \
			     ctx_ptr)                                        \
	GENERIC_ARRAY_ITERATOR_FIRST(elem_type, array_ptr, 0,                 \
	number_of_entries, 1, ctx_ptr)

#define ARRAY_ITERATOR_NEXT(elem_type, ctx_ptr)                              \
	GENERIC_ARRAY_ITERATOR_NEXT(elem_type, ctx_ptr)

/*---------------------------------------------------------------------------
 *
 * Typeless array iterator end_of_array->0
 *
 *--------------------------------------------------------------------------- */
#define ARRAY_REVERSE_ITERATOR_FIRST(elem_type, array_ptr, number_of_entries, \
				     ctx_ptr)                                \
	GENERIC_ARRAY_ITERATOR_FIRST(elem_type, array_ptr,                    \
	number_of_entries - 1, number_of_entries,  \
	-1, ctx_ptr)

#define ARRAY_REVERSE_ITERATOR_NEXT(elem_type, ctx_ptr)                      \
	GENERIC_ARRAY_ITERATOR_NEXT(elem_type, ctx_ptr)

#endif  /* _ARRAY_ITERATORS_H_ */
