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

.text

#------------------------------------------------------------------------------
# Registers for passing parameters:
#       %rdi, %rsi, %rdx, %rcx, %r8, %r9
#------------------------------------------------------------------------------


.extern extrn vmexit_common_handler
.extern extrn vmentry_failure_function

.macro SAVE_GP_REG
	pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %r11
	pushq %r10
	pushq %r9
	pushq %r8
	pushq %rdi
	pushq %rsi
	pushq %rbp
	subq  $8, %rsp	#rsp is save/restore in VMCS
	pushq %rbx
	pushq %rdx
	pushq %rcx
	pushq %rax
.endm

.macro RESTORE_GP_REG
	/* read HOST RSP from VMCS */
	movq $0x6C14, %rax
	vmreadq %rax, %rbx

	/* adjust rsp */
	subq $128, %rbx
	movq %rbx, %rsp

	popq %rax
	popq %rcx
	popq %rdx
	popq %rbx
	addq $8, %rsp	#rsp is save/restore in VMCS
	popq %rbp
	popq %rsi
	popq %rdi
	popq %r8
	popq %r9
	popq %r10
	popq %r11
	popq %r12
	popq %r13
	popq %r14
	popq %r15
.endm

.globl vmexit_func
vmexit_func:

	SAVE_GP_REG
	call    vmexit_common_handler
	jmp     .                       ## should never return
	ret

#------------------------------------------------------------------------------
#  Function:    Called upon VMENTRY.
#
#  Arguments:   RDI = 1 if called first time
#------------------------------------------------------------------------------
.globl vmentry_func
vmentry_func:
	push    %rdi
	cmp     $0, %rdi
	jnz     do_launch
do_resume:
	RESTORE_GP_REG
	vmresume                        # Resume execution of Guest Virtual Machine
	jmp     handle_error

do_launch:
	RESTORE_GP_REG
	vmlaunch                        # Launch execution of Guest Virtual Machine

handle_error:
	pushfq                          # use RFLAGS as argument if VMRESUME failed
	pop     %rdi                    # save RFLAGS in RDI as the 1st argument
	call    vmentry_failure_function
	jmp     .                       ## should never return
	ret

