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

#ifndef _GAS_DEFS_H_
#define _GAS_DEFS_H_

#define ARG1_U8         %di
#define ARG1_U16        %di
#define ARG1_U32        %edi
#define ARG1_U64        %rdi


#define ARG2_U8         %si
#define ARG2_U16        %si
#define ARG2_U32        %esi
#define ARG2_U64        %rsi

#define ARG3_U32        %edx
#define ARG3_U64        %rdx


#define ARG4_U64        %rcx
#define ARG5_U64        %r8
#define ARG6_U64        %r9


/* -- structure definition for mon_gp_registers_t */
#define IA32_REG_RAX    0
#define IA32_REG_RBX    1
#define IA32_REG_RCX    2
#define IA32_REG_RDX    3
#define IA32_REG_RDI    4
#define IA32_REG_RSI    5
#define IA32_REG_RBP    6
#define IA32_REG_RSP    7
#define IA32_REG_R8     8
#define IA32_REG_R9     9
#define IA32_REG_R10    10
#define IA32_REG_R11    11
#define IA32_REG_R12    12
#define IA32_REG_R13    13
#define IA32_REG_R14    14
#define IA32_REG_R15    15
#define IA32_REG_RIP    16
#define IA32_REG_RFLAGS 17
#define IA32_REG_COUNT  18

/* __gp_reg_id : IA32_REG_RAX..IA32_REG_RFLAGS */
#define GR_REG_OFFSET(__gp_reg_id) (__gp_reg_id * 8)

/* __xmm_reg_id : 0..15 */
#define XMM_REG_OFFSET(__xmm_reg_id)      \
	(GR_REG_OFFSET(IA32_REG_COUNT) + __xmm_reg_id * 16)

#endif /* _GAS_DEFS_H_ */
