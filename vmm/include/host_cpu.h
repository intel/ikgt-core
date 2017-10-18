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

#ifndef _HOST_CPU_H_
#define _HOST_CPU_H_

#include "vmm_base.h"
#include "vmm_objects.h"
#include "gdt.h"
#include "vmm_asm.h"
#include "dbg.h"

extern uint16_t host_cpu_num;
static inline uint16_t host_cpu_id(void)
{
	uint16_t cpu_id;

	cpu_id = calculate_cpu_id(asm_str());

	VMM_ASSERT_EX(cpu_id < host_cpu_num, "cpuid(%d) is larger"
		" than host_cpu_num(%d)\n", cpu_id, host_cpu_num);

	return cpu_id;
}

void host_cpu_vmx_on(void);

uint32_t host_cpu_get_pending_nmi(void);
void host_cpu_inc_pending_nmi(void);
void host_cpu_dec_pending_nmi(uint32_t num);
void host_cpu_clear_pending_nmi();

#endif   /* _HOST_CPU_H_ */
