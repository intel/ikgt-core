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

#include "vmm_base.h"
#include "vmm_asm.h"
#include "lib/print.h"
#include "lib/util.h"

void __stack_chk_fail(void)
{
	printf("stack check fail in loader\n");
	__STOP_HERE__
}

#if (defined STACK_PROTECTOR) && (defined DEBUG)
/* when we changed to another compiler, or another version of gcc,
 * the cookie might be placed in another place. this function is
 * used to detect such situation.
 */
boolean_t stack_layout_check(uint64_t stack_cookie)
{
	uint64_t fs_cookie = get_stack_cookie_value();

	if (fs_cookie != stack_cookie)
		return FALSE;

	return TRUE;
}
#endif
