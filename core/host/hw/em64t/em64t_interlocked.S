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
#     em64t_interlocked.s
#
#   Abstract:
#
#*****************************************************************************

.text

#------------------------------------------------------------------------------
# Registers for passing parameters:
#       %rdi, %rsi, %rdx, %rcx, %r8, %r9
# The rest  parameters are in stack
# Preserved regs
#       %rbp,%rbx,%r12,%r15
# System calls via syscall
# Kernel destroys %r11, %rcx
# The number of syscall is in %rax
#------------------------------------------------------------------------------


.macro monitor_instr
        .ascii    "\x0F\x01\xC8"
.endm

.macro mwait_instr
        .ascii    "\x0F\x01\xC9"
.endm

#------------------------------------------------------------------------------
#
# Execute assembler 'pause' instruction
#
#void ASM_FUNCTION hw_pause( void );
#------------------------------------------------------------------------------
.globl hw_pause
hw_pause:
    pause
    ret


#------------------------------------------------------------------------------
#
# Execute assembler 'monitor' instruction
#
# void ASM_FUNCTION hw_monitor( void* addr, uint32_t extension, uint32_t hint );
#------------------------------------------------------------------------------
.globl hw_monitor
hw_monitor:
    # on entry
    #   %rdi RCX contains addr
    #   %rsi RDX contains extension
    #   %rdx R8  contains hint
    mov         %rdi, %rax
    mov         %rsi, %rcx
    # %rdx already contains hint
    monitor_instr
    ret


#------------------------------------------------------------------------------
#
# Execute assembler 'mwait' instruction
#
# void ASM_FUNCTION hw_mwait( uint32_t extension, uint32_t hint );
#------------------------------------------------------------------------------
.globl hw_mwait
hw_mwait:
    # on entry
    #   %rdi RCX contains extension
    #   %rsi RDX  contains hint
    mov         %rdi, %rcx
    mov         %rsi, %rax
    mwait_instr
    ret


