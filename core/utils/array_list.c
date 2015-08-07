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

#include "list.h"

#define ARRAY_LIST_HEADER_SIZE(alignment)   \
	(uint32_t)(ALIGN_FORWARD(sizeof(array_list_t) - \
			   sizeof(((array_list_t *)0)->array), \
			   (address_t)alignment))

#define ARRAY_LIST_ELEMENT_HEADER_SIZE          \
	((uint32_t)sizeof(array_list_element_t) - \
	 (uint32_t)sizeof(((array_list_element_t *)0)->data))

#define ARRAY_LIST_ELEMENT_SIZE(element_size, alignment)  \
	(uint32_t)(ALIGN_FORWARD(ARRAY_LIST_ELEMENT_HEADER_SIZE + \
			   element_size, (address_t)alignment))

#define ARRAY_LIST_ELEMENT_BY_INDEX(alist, i) \
	(array_list_element_t *)(alist->array + \
				 ARRAY_LIST_ELEMENT_SIZE(alist->element_size, \
					 alist->alignment) * i)

#define ARRAY_LIST_DATA_TO_ELEMENT(data) \
	(array_list_element_t *)((char *)data - \
				 (char *)((array_list_element_t *)0)->data)

#define ARRAY_LIST_PADDING_SIZE(address, alignment) \
	(uint32_t)(((char *)ALIGN_FORWARD(address, \
			    (address_t)alignment)) - address)

typedef struct array_list_element_t {
	list_element_t	list;     /* free/used list */
	char		data[ARCH_ADDRESS_WIDTH];
} array_list_element_t;

typedef struct array_list_t {
	uint32_t	element_size;
	uint32_t	max_num_of_elements;
	uint32_t	alignment;
	uint32_t	memory_size;
	uint32_t	header_padding_size;
	uint32_t	id;
	list_element_t	free_list;
	list_element_t	used_list;
	uint32_t	num_of_used_elements;
	char		array[4];
} array_list_t;

uint32_t array_list_memory_size(char *buffer, uint32_t element_size,
				uint32_t num_of_elements, uint32_t alignment)
{
	return (uint32_t)(ARRAY_LIST_PADDING_SIZE(buffer, alignment) +
			  ARRAY_LIST_HEADER_SIZE(alignment) +
			  ARRAY_LIST_ELEMENT_SIZE(element_size,
				  alignment) * num_of_elements);
}

uint32_t array_list_size(array_list_t *alist)
{
	return alist->num_of_used_elements;
}

array_list_handle_t array_list_init(char *buffer,
				    uint32_t buffer_size,
				    uint32_t element_size,
				    uint32_t num_of_elements,
				    uint32_t alignment)
{
	static uint16_t list_id = 1;
	uint32_t required_buffer_size =
		array_list_memory_size(buffer, element_size, num_of_elements,
			alignment);
	array_list_t *alist;
	uint32_t i;
	array_list_element_t *entry = NULL;
	list_element_t *free_list = NULL;

	if (required_buffer_size > buffer_size) {
		return NULL;
	}

	alist =
		(array_list_t *)(buffer +
				 ARRAY_LIST_PADDING_SIZE(buffer, alignment));
	alist->id = list_id++;
	alist->element_size = element_size;
	alist->max_num_of_elements = num_of_elements;
	alist->alignment = alignment;
	alist->memory_size = buffer_size;
	alist->header_padding_size = ARRAY_LIST_PADDING_SIZE(buffer, alignment);
	alist->num_of_used_elements = 0;

	list_init(&alist->free_list);
	list_init(&alist->used_list);

	free_list = &alist->free_list;

	for (i = 0; i < num_of_elements; i++) {
		entry = ARRAY_LIST_ELEMENT_BY_INDEX(alist, i);
		list_add(free_list, &entry->list);
		free_list = free_list->next;
	}

	return alist;
}

boolean_t array_list_add(array_list_handle_t alist, void *data)
{
	list_element_t *free_element = NULL;
	array_list_element_t *free_list_entry = NULL;

	if (alist == NULL || list_is_empty(&alist->free_list) || data == NULL) {
		return FALSE;
	}

	free_element = alist->free_list.next;
	list_remove(free_element);
	list_add(alist->used_list.prev, free_element);
	alist->num_of_used_elements++;

	free_list_entry = LIST_ENTRY(free_element, array_list_element_t, list);

	mon_memcpy(free_list_entry->data, data, alist->element_size);

	return TRUE;
}

boolean_t array_list_remove(array_list_handle_t alist, void *data)
{
	array_list_element_t *element;

	if (alist == NULL || list_is_empty(&alist->used_list) || data == NULL) {
		return FALSE;
	}

	element = ARRAY_LIST_DATA_TO_ELEMENT(data);
	list_remove(&element->list);
	list_add(alist->free_list.prev, &element->list);
	alist->num_of_used_elements--;

	return TRUE;
}

char *array_list_first(array_list_handle_t alist, array_list_iterator_t *iter)
{
	array_list_element_t *element;
	char *data;

	if (alist == NULL || list_is_empty(&alist->used_list)) {
		return NULL;
	}

	element = LIST_ENTRY(alist->used_list.next, array_list_element_t, list);
	data = element->data;

	if (iter != NULL) {
		iter->alist = alist;
		iter->element = element;
	}

	return data;
}
