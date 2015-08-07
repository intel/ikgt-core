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
#include "trial_exec.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(TRIAL_EXEC_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(TRIAL_EXEC_C, __condition)

/* max phys. CPUs supported */
static trial_data_t *trial_data[MON_MAX_CPU_SUPPORTED];

void trial_execution_push(trial_data_t *p_trial_data, setjmp_buffer_t *p_env)
{
	cpu_id_t cpu_id = hw_cpu_id();

	MON_ASSERT(cpu_id < NELEMENTS(trial_data));

	p_trial_data->saved_env = p_env;
	p_trial_data->error_code = 0;
	p_trial_data->fault_vector = 0;
	p_trial_data->prev = trial_data[cpu_id];
	trial_data[cpu_id] = p_trial_data;
}

trial_data_t *trial_execution_pop(void)
{
	trial_data_t *p_last_trial;
	cpu_id_t cpu_id = hw_cpu_id();

	MON_ASSERT(cpu_id < NELEMENTS(trial_data));

	if (NULL != trial_data[cpu_id]) {
		p_last_trial = trial_data[cpu_id];
		trial_data[cpu_id] = trial_data[cpu_id]->prev;
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"Error. Attempt to Pop Empty Trial Stack\n");
		p_last_trial = NULL;
	}

	return p_last_trial;
}

trial_data_t *trial_execution_get_last(void)
{
	cpu_id_t cpu_id = hw_cpu_id();

	MON_ASSERT(cpu_id < NELEMENTS(trial_data));
	return trial_data[cpu_id];
}
