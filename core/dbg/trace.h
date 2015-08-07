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

#ifndef TRACE_H
#define TRACE_H

#include "mon_defs.h"

#define MAX_TRACE_BUFFERS       1
#define MAX_STRING_LENGTH       128
#define MAX_RECORDS_IN_BUFFER   2048

typedef struct {
	uint64_t	tsc;
	uint64_t	exit_reason;
	uint64_t	guest_eip;
	char		string[MAX_STRING_LENGTH];
} trace_record_data_t;

boolean_t trace_init(uint32_t max_num_guests, uint32_t max_num_guest_cpus);

boolean_t trace_add_record(IN uint32_t vm_index,
			   IN uint32_t cpu_index,
			   IN uint32_t buffer_index,
			   IN trace_record_data_t *data);

boolean_t trace_remove_oldest_record(OUT uint32_t *vm_index,
				     OUT uint32_t *cpu_index,
				     OUT uint32_t *buffer_index,
				     OUT uint32_t *record_index,
				     OUT trace_record_data_t *data);

boolean_t trace_lock(void);

boolean_t trace_unlock(void);

void trace_set_recyclable(boolean_t recyclable);

#endif   /* TRACE_H */
