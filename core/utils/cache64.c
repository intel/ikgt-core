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
#include "mon_dbg.h"
#include "common_libc.h"
#include "memory_allocator.h"
#include "cache64.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(CACHE64_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(CACHE64_C, __condition)

typedef struct cache64_struct_t {
	uint32_t	num_of_entries;
	/* bitmap size in bytes */
	uint16_t	bitmap_size;
	uint16_t	flags;
	uint64_t	*table;
	uint8_t		*dirty_bits;
	uint8_t		*valid_bits;
} cache64_struct_t;

/*
 * Helper macros
 */
#define CACHE_FIELD_IS_VALID(__cache, __entry_no)   \
	BITARRAY_GET((__cache)->valid_bits, __entry_no)
#define CACHE_FIELD_SET_VALID(__cache, __entry_no)  \
	BITARRAY_SET((__cache)->valid_bits, __entry_no)
#define CACHE_FIELD_CLR_VALID(__cache, __entry_no)  \
	BITARRAY_CLR((__cache)->valid_bits, __entry_no)

#define CACHE_FIELD_IS_DIRTY(__cache, __entry_no)   \
	BITARRAY_GET((__cache)->dirty_bits, __entry_no)
#define CACHE_FIELD_SET_DIRTY(__cache, __entry_no)  \
	BITARRAY_SET((__cache)->dirty_bits, __entry_no)
#define CACHE_FIELD_CLR_DIRTY(__cache, __entry_no)  \
	BITARRAY_CLR((__cache)->dirty_bits, __entry_no)

#define ENUMERATE_DIRTY_ENTRIES(__cache, __func, __arg)                        \
	BITARRAY_ENUMERATE((__cache)->dirty_bits,                                 \
	(__cache)->num_of_entries,                             \
	__func, __arg)

cache64_object_t cache64_create(uint32_t num_of_entries)
{
	cache64_struct_t *cache;
	uint64_t *table;
	uint8_t *dirty_bits;
	uint8_t *valid_bits;
	uint16_t bitmap_size = (uint16_t)BITARRAY_SIZE_IN_BYTES(num_of_entries);

	cache = mon_malloc(sizeof(cache64_struct_t));
	table = mon_malloc(sizeof(uint64_t) * num_of_entries);
	dirty_bits = mon_malloc(bitmap_size);
	valid_bits = mon_malloc(bitmap_size);

	if (NULL != cache && NULL != table &&
	    NULL != dirty_bits && NULL != valid_bits) {
		/* everything is OK. fill the fields */
		cache->num_of_entries = num_of_entries;
		cache->bitmap_size = bitmap_size;
		cache->flags = 0;
		cache->table = table;
		cache->dirty_bits = dirty_bits;
		cache->valid_bits = valid_bits;

		mon_memset(table, 0, sizeof(*table) * num_of_entries);
		mon_memset(dirty_bits, 0, bitmap_size);
		mon_memset(valid_bits, 0, bitmap_size);
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"[cache64] %s: Allocation failed\n", __FUNCTION__);
		if (NULL != cache) {
			mon_mfree(cache);
		}
		if (NULL != table) {
			mon_mfree(table);
		}
		if (NULL != dirty_bits) {
			mon_mfree(dirty_bits);
		}
		if (NULL != valid_bits) {
			mon_mfree(valid_bits);
		}
		cache = NULL;
	}

	return cache;
}

extern boolean_t vmcs_sw_shadow_disable[];

void cache64_write(cache64_object_t cache, uint64_t value, uint32_t entry_no)
{
	if (vmcs_sw_shadow_disable[hw_cpu_id()]) {
		return;
	}

	MON_ASSERT(cache);
	MON_ASSERT(entry_no < cache->num_of_entries);

	if (entry_no < cache->num_of_entries) {
		if (!(cache->table[entry_no] == value &&
		      CACHE_FIELD_IS_VALID(cache, entry_no))) {
			cache->table[entry_no] = value;
			CACHE_FIELD_SET_DIRTY(cache, entry_no);
			CACHE_FIELD_SET_VALID(cache, entry_no);
			BITMAP_SET(cache->flags, CACHE_DIRTY_FLAG);
		}
	}
}

boolean_t cache64_read(cache64_object_t cache,
		       uint64_t *p_value,
		       uint32_t entry_no)
{
	boolean_t is_valid = FALSE;

	if (vmcs_sw_shadow_disable[hw_cpu_id()]) {
		return FALSE;
	}

	MON_ASSERT(cache);
	MON_ASSERT(entry_no < cache->num_of_entries);
	MON_ASSERT(p_value);

	if (entry_no < cache->num_of_entries) {
		if (CACHE_FIELD_IS_VALID(cache, entry_no)) {
			*p_value = cache->table[entry_no];
			is_valid = TRUE;
		}
	}
	return is_valid;
}

/* clean valid bits */
void cache64_invalidate(cache64_object_t cache, uint32_t entry_no)
{
	MON_ASSERT(cache);

	if (entry_no < cache->num_of_entries) {
		/* invalidate specific entry */
		CACHE_FIELD_CLR_VALID(cache, entry_no);
	} else {
		/* invalidate all entries */
		BITMAP_CLR(cache->flags, CACHE_VALID_FLAG);
		mon_memset(cache->valid_bits, 0, cache->bitmap_size);
		mon_memset(cache->dirty_bits, 0, cache->bitmap_size);
	}
}

/* flush dirty fields using <function> */
void cache64_flush_dirty(cache64_object_t cache, uint32_t entry_no,
			 /* if function == NULL, then just clean dirty bits */
			 func_cache64_field_process_t function,
			 void *arg)
{
	MON_ASSERT(cache);

	if (entry_no < cache->num_of_entries) {
		/* flush specific entry */
		if (CACHE_FIELD_IS_DIRTY(cache, entry_no)) {
			CACHE_FIELD_CLR_DIRTY(cache, entry_no);
			if (NULL != function) {
				function(entry_no, arg);
			}
		}
	} else {
		/* flush all entries */
		BITMAP_CLR(cache->flags, CACHE_DIRTY_FLAG);

		if (NULL != function) {
			ENUMERATE_DIRTY_ENTRIES(cache, function, arg);
		} else {
			mon_memset(cache->dirty_bits, 0, cache->bitmap_size);
		}
	}
}

void cache64_flush_to_memory(cache64_object_t cache, void *p_dest,
			     uint32_t max_bytes)
{
	uint32_t cache_size = sizeof(*cache->table) * cache->num_of_entries;

	MON_ASSERT(cache);
	MON_ASSERT(p_dest);

	if (cache_size > max_bytes) {
		MON_LOG(mask_anonymous,
			level_trace,
			"[cache64] %s: Warning!!! Destination size less then required\n",
			__FUNCTION__);
		cache_size = max_bytes;
	}
	mon_memcpy(p_dest, cache->table, cache_size);
}

boolean_t cache64_is_dirty(cache64_object_t cache)
{
	MON_ASSERT(cache);

	return 0 != BITMAP_GET(cache->flags, CACHE_DIRTY_FLAG);
}

void cache64_destroy(cache64_object_t cache)
{
	MON_ASSERT(cache);

	mon_mfree(cache->table);
	mon_mfree(cache->dirty_bits);
	mon_mfree(cache->valid_bits);
	mon_mfree(cache);
}
