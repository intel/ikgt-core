################################################################################
# Copyright (c) 2015 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################
#
#   Module Name:
#
#     vmm_util_asm.S
#
#   Abstract:
#
#*****************************************************************************

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

.text

#------------------------------------------------------------------------------
# Registers for passing parameters:
#       %rdi, %rsi, %rdx, %rcx, %r8, %r9
#------------------------------------------------------------------------------

#------------------------------------------------------------------------------
#  void cdecl
#  hw_write_cs (
#          uint16_t
#  )
#
#  Write to Command Segment Selector
#
#------------------------------------------------------------------------------
.globl hw_write_cs
hw_write_cs:
	## push segment selector
	xor     %rax, %rax
	movw    ARG1_U16, %ax
	pushq   %rax
	lea     CONT_WITH_NEW_CS (%rip), %rdx
	pushq   %rdx
	lretq                  # brings IP to CONT_WITH_NEW_CS
CONT_WITH_NEW_CS:
	ret

#------------------------------------------------------------------------------
#  void cdecl
#  hw_set_stack_pointer (
#          uint64_t new_stack_pointer,
#          func_main_continue_t func,
#          void* params
#  )
#
#
#------------------------------------------------------------------------------
.globl hw_set_stack_pointer
hw_set_stack_pointer:
	mov     ARG1_U64, %rsp
	mov     ARG3_U64, ARG1_U64
	call    *ARG2_U64
	jmp     .
	ret

#------------------------------------------------------------------------------
#  void
#  hw_perform_asm_iret()
#------------------------------------------------------------------------------
# Transforms stack from entry to reglar procedure:
#
# [       RIP        ] <= RSP
#
# To stack  to perform iret instruction:
#
# [       SS         ]
# [       RSP        ]
# [      RFLAGS      ]
# [       CS         ]
# [       RIP        ] <= RSP should point prior iret

.globl hw_perform_asm_iret
hw_perform_asm_iret:
	sub     $0x20, %rsp     # prepare space for "interrupt stack"

	push    %rax            # save scratch registers
	push    %rbx
	push    %rcx
	push    %rdx

	add     $0x40, %rsp     # get rsp back to RIP
	pop     %rax            # RIP -> RAX
	mov     %cs,   %rbx     # CS  -> RBX
	mov     %rsp,  %rcx     # good RSP -> RCX
	mov     %ss,   %rdx     # CS  -> RDX

	push    %rdx            # [       SS         ]
	push    %rcx            # [       RSP        ]
	pushfq                  # [      RFLAGS      ]
	push    %rbx            # [       CS         ]
	push    %rax            # [       RIP        ]

	sub     $0x20, %rsp     # restore scratch registers
	pop     %rdx
	pop     %rcx
	pop     %rbx
	pop     %rax            # now RSP is in right position

	iretq                   # perform IRET

### EOF ###

