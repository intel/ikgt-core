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

/*
 * Trace mechanism
 */

#include "trace.h"
#include "heap.h"
#include "common_libc.h"

#define CYCLIC_INCREMENT(x)     \
	do { (x)++; if ((x) == MAX_RECORDS_IN_BUFFER) { (x) = 0; } } while (0)

#define NON_CYCLIC_INCREMENT(x) \
	do { (x)++; if ((x) == MAX_RECORDS_IN_BUFFER) { (x)--; } } while (0)

#define FOREACH_BUFFER(apply_function, param) \
	do { \
		uint32_t vm_index = 0, cpu_index = 0, buffer_index = 0; \
		for (vm_index = 0; vm_index < trace_state->max_num_guests; \
		     vm_index++) { \
			for (cpu_index = 0; \
			     cpu_index < trace_state->max_num_guest_cpus; \
			     cpu_index++) { \
				for (buffer_index = 0; \
				     buffer_index < MAX_TRACE_BUFFERS; \
				     buffer_index++) { \
					apply_function(vm_index, \
						cpu_index, \
						buffer_index, \
						param); \
				} \
			} \
		} \
	} while (0)

typedef struct {
	boolean_t		valid;
	uint32_t		index;
	trace_record_data_t	data;
	struct trace_buffer_t	*buffer;
} trace_record_t;

typedef struct trace_buffer_t {
	uint32_t	vm_index;
	uint32_t	cpu_index;
	uint32_t	buffer_index;
	uint32_t	next_record_index;
	trace_record_t	records[MAX_RECORDS_IN_BUFFER];
} trace_buffer_t;

typedef struct {
	uint32_t	global_counter;
	boolean_t	locked;
	uint32_t	max_num_guests;
	uint32_t	max_num_guest_cpus;
	trace_buffer_t	buffers[1]; /* pointer to buffers */
} trace_state_t;

static boolean_t trace_initialized = FALSE;
static trace_state_t *trace_state;
static boolean_t trace_recyclable = TRUE;

#define GET_BUFFER(vm_index, cpu_index, buffer_index) \
	(trace_state->buffers + \
	 vm_index * trace_state->max_num_guest_cpus * MAX_TRACE_BUFFERS \
	 + cpu_index * MAX_TRACE_BUFFERS \
	 + buffer_index)

static
void initialize_trace_buffer(uint32_t vm_index, uint32_t cpu_index,
			     uint32_t buffer_index, void *param UNUSED)
{
	trace_buffer_t *buffer;
	uint32_t record_index;

	buffer = GET_BUFFER(vm_index, cpu_index, buffer_index);

	buffer->vm_index = vm_index;
	buffer->cpu_index = cpu_index;
	buffer->buffer_index = buffer_index;

	for (record_index = 0; record_index < MAX_RECORDS_IN_BUFFER;
	     record_index++) {
		buffer->records[record_index].valid = FALSE;
		buffer->records[record_index].buffer = buffer;
	}

	buffer->next_record_index = 0;
}

boolean_t trace_init(uint32_t max_num_guests, uint32_t max_num_guest_cpus)
{
	if (trace_initialized) {
		return FALSE;
	}
	/* trace_state already includes one buffer */
	trace_state = mon_memory_alloc(sizeof(trace_state_t) +
		max_num_guests * max_num_guest_cpus *
		MAX_TRACE_BUFFERS * sizeof(trace_buffer_t)
		- 1);
	if (NULL == trace_state) {
		return FALSE;
	}

	trace_state->global_counter = 0;
	trace_state->locked = FALSE;
	trace_state->max_num_guests = max_num_guests;
	trace_state->max_num_guest_cpus = max_num_guest_cpus;

	FOREACH_BUFFER(initialize_trace_buffer, NULL);

	trace_initialized = TRUE;
	return TRUE;
}

static
void add_record(trace_buffer_t *buffer, trace_record_data_t *data)
{
	trace_record_t *record = &buffer->records[buffer->next_record_index];

	record->valid = TRUE;
	record->index = trace_state->global_counter++;

	record->data.tsc = data->tsc;
	record->data.exit_reason = data->exit_reason;
	record->data.guest_eip = data->guest_eip;
	mon_strcpy_s(record->data.string, MAX_STRING_LENGTH, data->string);

	if (trace_recyclable) {
		CYCLIC_INCREMENT(buffer->next_record_index);
	} else {
		NON_CYCLIC_INCREMENT(buffer->next_record_index);
	}
}

boolean_t trace_add_record(IN uint32_t vm_index,
			   IN uint32_t cpu_index,
			   IN uint32_t buffer_index,
			   IN trace_record_data_t *data)
{
	if (!trace_initialized || trace_state->locked || data == NULL
	    || vm_index >= trace_state->max_num_guests
	    || cpu_index >= trace_state->max_num_guest_cpus
	    || buffer_index >= MAX_TRACE_BUFFERS) {
		return FALSE;
	}

	add_record(GET_BUFFER(vm_index, cpu_index, buffer_index), data);

	return TRUE;
}

static
void remove_record(trace_record_t *record)
{
	record->valid = FALSE;
	CYCLIC_INCREMENT(record->buffer->next_record_index);
}

static
void set_buffer_pointer_to_oldest_record(uint32_t vm_index, uint32_t cpu_index,
					 uint32_t buffer_index,
					 void *param UNUSED)
{
	trace_buffer_t *buffer = GET_BUFFER(vm_index, cpu_index, buffer_index);

	if (!buffer->records[buffer->next_record_index].valid) {
		uint32_t i;

		for (i = 0; i < MAX_RECORDS_IN_BUFFER; i++) {
			if (buffer->records[i].valid) {
				/* found */
				break;
			}
		}
		buffer->next_record_index = (i < MAX_RECORDS_IN_BUFFER) ? i : 0;
	}
}

static
void find_buffer_with_oldest_record(uint32_t vm_index, uint32_t cpu_index,
				    uint32_t buffer_index, void *param)
{
	trace_record_t **oldest_record_ptr = (trace_record_t **)param;
	trace_buffer_t *buffer = GET_BUFFER(vm_index, cpu_index, buffer_index);
	trace_record_t *record = &buffer->records[buffer->next_record_index];

	if (record->valid) {
		if ((*oldest_record_ptr == NULL) ||
		    (record->index < (*oldest_record_ptr)->index)) {
			/* this record is the first record encountered */
			/* and this record is older than the oldest record */
			*oldest_record_ptr = record;
		}
	}
}

static
trace_record_t *find_oldest_record(void)
{
	trace_record_t *oldest_record = NULL;

	/* find the oldest record in each buffer */
	FOREACH_BUFFER(set_buffer_pointer_to_oldest_record, NULL);

	/* find the globally oldest record */
	FOREACH_BUFFER(find_buffer_with_oldest_record, &oldest_record);

	return oldest_record;
}

boolean_t trace_remove_oldest_record(OUT uint32_t *vm_index,
				     OUT uint32_t *cpu_index,
				     OUT uint32_t *buffer_index,
				     OUT uint32_t *record_index,
				     OUT trace_record_data_t *data)
{
	trace_record_t *oldest_record;

	if (!trace_initialized) {
		return FALSE;
	}

	oldest_record = find_oldest_record();
	if (oldest_record == NULL) {
		return FALSE;
	}

	remove_record(oldest_record);

	if (vm_index != NULL) {
		*vm_index = oldest_record->buffer->vm_index;
	}
	if (cpu_index != NULL) {
		*cpu_index = oldest_record->buffer->cpu_index;
	}
	if (buffer_index != NULL) {
		*buffer_index = oldest_record->buffer->buffer_index;
	}
	if (record_index != NULL) {
		*record_index = oldest_record->index;
	}
	if (data != NULL) {
		data->exit_reason = oldest_record->data.exit_reason;
		data->guest_eip = oldest_record->data.guest_eip;
		data->tsc = oldest_record->data.tsc;
		mon_strcpy_s(data->string, MAX_STRING_LENGTH,
			oldest_record->data.string);
	}

	return TRUE;
}

boolean_t trace_lock(void)
{
	if (!trace_initialized || trace_state->locked) {
		return FALSE;
	}
	trace_state->locked = TRUE;
	return TRUE;
}

boolean_t trace_unlock(void)
{
	if (!trace_initialized || !trace_state->locked) {
		return FALSE;
	}
	trace_state->locked = FALSE;
	return TRUE;
}

void trace_set_recyclable(boolean_t recyclable)
{
	trace_recyclable = recyclable;
}
