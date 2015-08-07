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

#ifndef _MON_ADDONS_H_
#define _MON_ADDONS_H_

#include "mon_defs.h"
#include "mon_startup.h"

/*************************************************************************
 *
 * List of all known addons
 *
 ************************************************************************* */

extern void init_ept_addon(uint32_t num_of_cpus);
void init_guest_create_addon(void);
extern void gdb_stub_addon_initialize(uint32_t max_num_of_guest_cpus,
				      const mon_debug_port_params_t *p_params);

#endif                          /* _MON_ADDONS_H_ */
