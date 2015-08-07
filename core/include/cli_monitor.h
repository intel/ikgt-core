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

#ifndef _CLI_MONITOR_H_
#define _CLI_MONITOR_H_

#include "cli_env.h"

#ifdef CLI_INCLUDE

void cli_monitor_init(void);
/* returns TRUE if BREAK_POINT is required */
boolean_t cli_monitor(const char *title, uint32_t access_level);

boolean_t cli_deadloop_helper(const char *assert_condition,
			      const char *func_name,
			      const char *file_name,
			      uint32_t line_num,
			      uint32_t access_level);

/* returns TRUE if MON_BREAKPOINT is required */
void cli_handle_error(const char *assert_condition,
		      const char *func_name,
		      const char *file_name,
		      uint32_t line_num,
		      uint32_t error_level);
#else  /* ! CLI_INCLUDE */


INLINE void cli_monitor_init(void)
{
}

INLINE boolean_t cli_monitor(const char *title, uint32_t access_level)
{
	return FALSE;
}

INLINE boolean_t cli_deadloop_helper(const char *assert_condition,
				     const char *func_name,
				     const char *file_name,
				     uint32_t line_num,
				     uint32_t access_level)
{
	return TRUE;
}

INLINE void cli_handle_error(const char *assert_condition,
			     const char *func_name,
			     const char *file_name,
			     uint32_t line_num,
			     uint32_t error_level)
{
	MON_UP_BREAKPOINT();
}


#endif  /* CLI_INCLUDE */

#endif  /* _CLI_MONITOR_H_ */
