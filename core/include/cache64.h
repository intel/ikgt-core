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

#ifndef _CACHE64_H_
#define _CACHE64_H_

typedef struct cache64_struct_t *cache64_object_t;

typedef void (*func_cache64_field_process_t) (uint32_t entry_no, void *arg);

#define CACHE_ALL_ENTRIES   ((uint32_t)-1)

#define CACHE_DIRTY_FLAG 1
#define CACHE_VALID_FLAG 2

cache64_object_t cache64_create(uint32_t num_of_entries);
void cache64_write(cache64_object_t cache, uint64_t value, uint32_t entry_no);
/* return TRUE if entry is valid */
boolean_t cache64_read(cache64_object_t cache,
		       uint64_t *p_value,
		       uint32_t entry_no);
/* return cache flags */
uint32_t cache64_read_raw(cache64_object_t cache,
			  uint64_t *p_value,
			  uint32_t entry_no);
/* clean valid bits */
void cache64_invalidate(cache64_object_t cache, uint32_t entry_no);
/* clean dirty bits */
void cache64_flush_dirty(cache64_object_t cache,
			 uint32_t entry_no,
			 func_cache64_field_process_t function,
			 void *arg);
void cache64_flush_to_memory(cache64_object_t cache,
			     void *p_dest,
			     uint32_t max_bytes);
/* return TRUE if any field is dirty valid */
boolean_t cache64_is_dirty(cache64_object_t cache);
void cache64_destroy(cache64_object_t cache);

#endif  /* _CACHE64_H_ */
