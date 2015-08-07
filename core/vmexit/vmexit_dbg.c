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

#include <mon_defs.h>
#include <guest_cpu.h>
#include <libc.h>
#include <mon_dbg.h>
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_DBG_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMEXIT_DBG_C, __condition)

const char *string = "bla bla";

#define CTRL(__char)    (__char - 'a' + 1)

#define REQUEST_COUNT    8
static char monitor_requested[REQUEST_COUNT] = { 0, 0, 0, 0, 0, 0, 0, 0 };

static char monitor_request_keys[REQUEST_COUNT] = {
	CTRL('q'),
	CTRL('w'),
	CTRL('e'),
	CTRL('r'),
	CTRL('t'),
	CTRL('y'),
	CTRL('u'),
	CTRL('i')
};

int monitor_was_requested(char key)
{
	size_t i;

	for (i = 0; i < NELEMENTS(monitor_request_keys); ++i) {
		if (key == monitor_request_keys[i]) {
			return (int)i;
		}
	}
	return -1;
}

void vmexit_check_keystroke(guest_cpu_handle_t gcpu)
{
	uint8_t key = mon_getc();
	int monitor_cpu;

	switch (key) {
	case 0:
		/* optimization */
		break;

	case 's':
	case 'S':
		MON_LOG(mask_anonymous, level_trace, "%s\n", string);
		break;

	case CTRL('b'):
	case CTRL('d'):
		MON_DEADLOOP();
		break;

	default:
		monitor_cpu = monitor_was_requested(key);
		if (monitor_cpu != -1) {
			monitor_requested[monitor_cpu] = 1;
		}
		if (monitor_requested[hw_cpu_id()] != 0) {
			monitor_requested[hw_cpu_id()] = 0;
			MON_DEADLOOP();
		}

		break;
	}
}
