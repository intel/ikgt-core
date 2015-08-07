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

#include "mon_globals.h"
#include "mon_version_struct.h"
#include "mon_dbg.h"
#include "libc.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(MON_GLOBALS_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(MON_GLOBALS_C, __condition)

/***************************************************************************
*
* Just instaniation of global variables
*
***************************************************************************/

mon_state_t g_mon_state = MON_STATE_UNINITIALIZED;

#if defined DEBUG
/* This is done to remove out the strings from the release build */
#ifdef MON_VERSION_STRING
const char *g_mon_version_string =
	MON_VERSION_START MON_VERSION_STRING MON_VERSION_END;
#else
const char *g_mon_version_string = NULL;
#endif
#else
const char *g_mon_version_string = NULL;
#endif

void mon_version_print(void)
{
	uint32_t global_string_length = 0;
	uint32_t header_len, trailer_len;
	uint32_t cur;

	/*
	 * Version string is surrounded with MON_VERSION_START and MON_VERSION_END
	 * MON_VERSION_END must be followed with NULL
	 */

	if (NULL == g_mon_version_string) {
		return;
	}

	header_len = (uint32_t)mon_strlen(MON_VERSION_START);
	trailer_len = (uint32_t)mon_strlen(MON_VERSION_END);

	MON_ASSERT((0 != header_len) && (0 != trailer_len))

	global_string_length = (uint32_t)mon_strlen(g_mon_version_string);

	if (global_string_length <= (header_len + trailer_len)) {
		/* nothing between header and trailer */
		return;
	}

	/* check that header and trailer match */
	for (cur = 0; cur < header_len; ++cur) {
		if (g_mon_version_string[cur] != MON_VERSION_START[cur]) {
			/* header does not match */
			return;
		}
	}

	for (cur = 0; cur < trailer_len; ++cur) {
		if (g_mon_version_string[global_string_length - trailer_len +
					 cur] !=
		    MON_VERSION_END[cur]) {
			/* trailer does not match */
			return;
		}
	}

	/* if we are here - version string is ok. Print it. */
	MON_LOG(mask_anonymous, level_trace,
		"\n----------------------------------------------------------\n");

	MON_LOG(mask_anonymous, level_trace, "%*s\n",
		global_string_length - header_len - trailer_len,
		g_mon_version_string + header_len);

	MON_LOG(mask_anonymous,
		level_trace,
		"------------------------------------------------------------\n\n");
}
