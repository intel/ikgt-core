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

#------------------------------------------------------------------------------
# System V AMD64 ABI for passing parameters and preserved registers:
# Registers for passing parameters:
#       %rdi, %rsi, %rdx, %rcx, %r8, %r9
# Preserved registers:
#       %rbp, %rbx, %rdi, %rsi, %rsp, %r12-r15
#------------------------------------------------------------------------------

.extern mon_memset
.extern mon_memcpy
.extern mon_strlen

.text


#------------------------------------------------------------------------------
#  force compiler intrinsics to use our code
#------------------------------------------------------------------------------
.globl memset
memset:
    jmp mon_memset


.globl memcpy
memcpy:
    jmp mon_memcpy

.globl strlen
strlen:
    jmp mon_strlen

.globl strcpy
strcpy:
    jmp mon_strcpy

.globl strncpy
strncpy:
    jmp mon_strcpy_s

#****************************************************************************
#*
#* Lock exchange qword
#* void_t
#* mon_lock_xchg_qword (
#*                      uint64_t *dst, ; rdi
#*                      uint64_t *src  ; rsi
#*                     )
#****************************************************************************
.global mon_lock_xchg_qword
mon_lock_xchg_qword:
    push %r8

    movq (%rsi), %r8 # copy src to r8
    lock xchgq %r8, (%rdi)

    pop %r8
    ret

#****************************************************************************
#*
#* Lock exchange byte
#* void_t
#* mon_lock_xchg_byte (
#*                     uint8_t *dst, ; rdi
#*                     uint8_t *src  ; rsi
#*                    )
#****************************************************************************
.global mon_lock_xchg_byte
mon_lock_xchg_byte:
    push %rbx

    movb (%rsi), %bl # copy src to bl
    lock xchgb %bl, (%rdi)

    pop %rbx
    ret

