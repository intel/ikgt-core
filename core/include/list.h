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

#ifndef _LIST_H
#define _LIST_H

#include "mon_defs.h"
#include "common_libc.h"

#define LIST_ENTRY(list, entry_type, list_entry_name) \
	((entry_type *)((char *)list - OFFSET_OF(entry_type, list_entry_name)))

typedef struct list_element_t {
	struct list_element_t *next;
	struct list_element_t *prev;
} list_element_t;

INLINE void list_init(list_element_t *entry)
{
	entry->next = entry->prev = entry;
}

INLINE void _list_add(list_element_t *prev, list_element_t *next,
		      list_element_t *new_entry)
{
	prev->next = new_entry;
	new_entry->prev = prev;
	next->prev = new_entry;
	new_entry->next = next;
}

INLINE void list_add(list_element_t *list, list_element_t *new_entry)
{
	_list_add(list, list->next, new_entry);
}

INLINE void list_remove(list_element_t *list)
{
	list->prev->next = list->next;
	list->next->prev = list->prev;
	list->prev = list->next = NULL;
}

INLINE boolean_t list_is_empty(list_element_t *list)
{
	return (boolean_t)(list->next == list);
}

#define LIST_FOR_EACH(list, iter) \
	for (iter = (list)->next; iter != (list); iter = iter->next)

INLINE uint16_t list_size(list_element_t *list)
{
	uint16_t size = 0;
	list_element_t *curr_element = list;

	while (curr_element->next != list) {
		size++;
		curr_element = curr_element->next;
	}

	return size;
}

#define LIST_NEXT(list, entry_type, list_entry_name) \
	(LIST_ENTRY((list)->next, entry_type, list_entry_name))

typedef struct array_list_t *array_list_handle_t;

typedef struct array_list_element_t *array_list_element_handle_t;

typedef struct {
	array_list_handle_t		alist;
	array_list_element_handle_t	element;
} array_list_iterator_t;

/* FUNCTION: array_list_memory_size
 * DESCRIPTION: Calculate memory size required for list.
 * RETURN VALUE: Memory size required for list */
uint32_t array_list_memory_size(char *buffer,
				uint32_t element_size,
				uint32_t num_of_elements,
				uint32_t alignment);

/* FUNCTION: array_list_init
 * DESCRIPTION: Initialize the list.
 * RETURN VALUE: Array list handle to use for list manipulation */
array_list_handle_t array_list_init(char *buffer,
				    uint32_t buffer_size,
				    uint32_t element_size,
				    uint32_t num_of_elements,
				    uint32_t alignment);

/* FUNCTION: array_list_size
 * DESCRIPTION: Number of elements in the list.
 * RETURN VALUE: Number of elements in the list. */
uint32_t array_list_size(array_list_handle_t alist);

/* FUNCTION: array_list_add
 * DESCRIPTION: Add element to the list.
 * RETURN VALUE: TRUE if element was successfully added, FALSE for error. */
boolean_t array_list_add(array_list_handle_t alist, void *data);

/* FUNCTION: array_list_remove
 * DESCRIPTION: Remove element from the list.
 * RETURN VALUE: TRUE if element was successfully removed, FALSE for error. */
boolean_t array_list_remove(array_list_handle_t alist, void *data);

/* FUNCTION: array_list_first
 * DESCRIPTION: Get first element.
 * RETURN VALUE: The first element. */
char *array_list_first(array_list_handle_t alist, array_list_iterator_t *iter);

/* FUNCTION: array_list_next
 * DESCRIPTION: Get next element for iteration.
 * RETURN VALUE: The next element. */
char *array_list_next(array_list_iterator_t *iter);

#endif
