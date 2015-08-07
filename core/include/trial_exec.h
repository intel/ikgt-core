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

#ifndef _TRIAL_EXEC_H_
#define _TRIAL_EXEC_H_

#include "hw_setjmp.h"

typedef struct trial_data_t {
	struct trial_data_t	*prev;
	setjmp_buffer_t		*saved_env;
	uint32_t		error_code;     /* output */
	vector_id_t		fault_vector;   /* output */
	uint8_t			pad[3];
} trial_data_t;

void trial_execution_push(trial_data_t *p_trial_data, setjmp_buffer_t *p_env);
trial_data_t *trial_execution_pop(void);
trial_data_t *trial_execution_get_last(void);

/* TRY/CATCH/END_TRY macros together build the construction for trial(safe)
 * execution. They must be used together and only in that order.
 * Note: Use __p_catch_data carefully, because is declared before
 * TRY/CATCH/END_TRY, but it points to buffer, which is invalid out
 * of TRY... construction. */
/*------------------------------------------------------------------------- */
#define TRY {                                                                  \
		trial_data_t __trial_data;                                     \
		setjmp_buffer_t __env;                                         \
		if (0 == setjmp(&__env)) {                                     \
			trial_execution_push(&__trial_data, &__env);
/*------------------------------------------------------------------------- */
#define CATCH(__p_catch_data)                                                  \
	trial_execution_pop();                                                 \
	}                                                                      \
	else {                                                                 \
		__p_catch_data = trial_execution_pop();
/*------------------------------------------------------------------------- */
#define END_TRY                                                                \
	}                                                                          \
	}
/*------------------------------------------------------------------------- */

#endif   /* _TRIAL_EXEC_H_ */
