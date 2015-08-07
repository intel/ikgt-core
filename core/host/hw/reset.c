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
#include "hw_utils.h"
#include "common_libc.h"
#include "mon_dbg.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(RESET_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(RESET_C, __condition)

#define RESET_CONTROL_REGISTER_IO_PORT         0xCF9

typedef enum {
	SYSTEM_RESET_BIT = 1,  /* 0 = cpu_reset generates an INIT(Soft Reset),
				* 1 = cpu_reset generates platform reset (Hard
				* Reset) */
	CPU_RESET_BIT = 2,     /* 0->1 transition generates the reset type
				* specified by system_reset */
	FULL_RESET_BIT = 3
} reset_control_register_bits_t;

#define SET_SYSTEM_RESET(v)  BIT_SET(v, SYSTEM_RESET_BIT)
#define CLR_SYSTEM_RESET(v)  BIT_CLR(v, SYSTEM_RESET_BIT)
#define GET_SYSTEM_RESET(v)  BIT_GET(v, SYSTEM_RESET_BIT)

#define SET_CPU_RESET(v)  BIT_SET(v, CPU_RESET_BIT)
#define CLR_CPU_RESET(v)  BIT_CLR(v, CPU_RESET_BIT)
#define GET_CPU_RESET(v)  BIT_GET(v, CPU_RESET_BIT)

#define SET_FULL_RESET(v)  BIT_SET(v, FULL_RESET_BIT)
#define CLR_FULL_RESET(v)  BIT_CLR(v, FULL_RESET_BIT)
#define GET_FULL_RESET(v)  BIT_GET(v, FULL_RESET_BIT)

void hw_reset_platform(void)
{
	uint8_t reset_control_register;

	/*
	 * Write the ICH register required to perform a platform reset
	 * (Cold Reset)
	 */
	reset_control_register = hw_read_port_8(RESET_CONTROL_REGISTER_IO_PORT);

	SET_CPU_RESET(reset_control_register);
	SET_SYSTEM_RESET(reset_control_register);

	hw_write_port_8(RESET_CONTROL_REGISTER_IO_PORT, reset_control_register);

	/*
	 * Never returns
	 */
	MON_DEADLOOP();
}
