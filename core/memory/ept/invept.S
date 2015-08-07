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
# The rest  parameters are in stack
# Preserved regs
#      	%rbp,%rbx,%r12,%r15
# System calls via syscall
# Kernel destroys %r11, %rcx
# The number of syscall is in %rax
#------------------------------------------------------------------------------


.macro INVEPT
        .ascii    "\x66\x48\x0f\x38\x80\x08"
.endm

.macro INVVPID
        .ascii    "\x66\x48\x0f\x38\x81\x08"
.endm

#------------------------------------------------------------------------------
#  void_t
#  mon_asm_invept (
#    invept_arg_t   *arg,		%rdi
#    uint32_t       modifier	%rsi
#    uint64_t       *rflags	%rdx
#    )
#------------------------------------------------------------------------------

.globl mon_asm_invept
mon_asm_invept:
        mov %rdi, %rax
        mov %rsi, %rcx
        INVEPT
        pushfq
        pop (%rdx)
        ret

#------------------------------------------------------------------------------
#  void_t
#  mon_asm_invvpid (
#    invept_arg_t   *arg,		%rdi
#    uint32_t       modifier	%rsi
#    uint64_t       *rflags	%rdx
#    )
#------------------------------------------------------------------------------

.globl mon_asm_invvpid
mon_asm_invvpid:
        mov %rdi, %rax
        mov %rsi, %rcx
        INVVPID
        pushfq
        pop (%rdx)
        ret


