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

#ifndef _MON_GLOBALS_H_
#define _MON_GLOBALS_H_

#include "mon_defs.h"

/***************************************************************************
*
* Set of global variables
*
***************************************************************************/

/* MON running state */
typedef enum {
	MON_STATE_UNINITIALIZED = 0,
	/* initial boot state - only BSP is active and is in MON */
	MON_STATE_BOOT,
	/* BSP waits for APs to finish initialization */
	MON_STATE_WAIT_FOR_APS,
	/* All CPUs finished init and are in normal running state or in
	 * Wait-For-SIPI state on behalf of guest */
	MON_STATE_RUN
} mon_state_t;

extern mon_state_t g_mon_state;

INLINE mon_state_t mon_get_state(void)
{
	return g_mon_state;
}

INLINE void mon_set_state(mon_state_t new_state)
{
	g_mon_state = new_state;
}

void mon_version_print(void);

extern cpu_id_t g_num_of_cpus;

#endif   /* _MON_GLOBALS_H_ */
