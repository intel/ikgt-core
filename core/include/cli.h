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

#ifndef _CLI_H_
#define _CLI_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "cli_env.h"

typedef int (*func_cli_t) (unsigned argc, char *args[]);

#ifdef CLI_INCLUDE

void cli_open_session(const char *reason, uint32_t access_level);
boolean_t cli_close_session(void);
void cli_end_session(boolean_t cli_retvalue);
boolean_t cli_is_session_active(void);
boolean_t cli_is_session_active_on_this_cpu(void);
void cli_print_session_reason(void);
int cli_add_command(func_cli_t function,
		    char *path,
		    char *help,
		    char *usage,
		    uint32_t access_level);
int cli_exec_command(char *path);
void cli_prompt(void);
void cli_init(void);
void cli_emulator_register(guest_id_t guest_id);
uint32_t cli_get_level(void);
boolean_t cli_set_level(uint32_t level);

#else /* ! CLI_INCLUDE */

INLINE void cli_open_session(const char *reason, uint32_t access_level)
{
}
INLINE boolean_t cli_close_session(void)
{
	return FALSE;
}
INLINE void cli_end_session(boolean_t cli_retvalue)
{
}
INLINE boolean_t cli_is_session_active(void)
{
	return FALSE;
}
INLINE boolean_t cli_is_session_active_on_this_cpu(void)
{
	return FALSE;
}
INLINE void cli_print_session_reason(void)
{
}
INLINE int cli_add_command(func_cli_t function, char *path, char *help,
			   char *usage, uint32_t access_level)
{
	return -1;
}
INLINE int cli_exec_command(char *path)
{
	return -1;
}
INLINE void cli_prompt(void)
{
}
INLINE void cli_emulator_register(guest_id_t guest_id)
{
}
INLINE uint32_t cli_get_level(void)
{
	return 0;
}
INLINE boolean_t cli_set_level(uint32_t level)
{
	return FALSE;
}
#endif  /* CLI_INCLUDE */

#if defined(__cplusplus)
}
#endif

#endif  /* _CLI_H_ */
