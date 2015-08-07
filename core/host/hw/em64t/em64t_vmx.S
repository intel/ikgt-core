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

#*****************************************************************************
#* TODO: add profiling functions here to sync the code with asm after C
#* functions for profiling is avaliable.
#*****************************************************************************

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


.extern gcpu_save_registers
.extern gcpu_restore_registers
.extern extrn vmexit_common_handler
.extern extrn vmentry_failure_function


#------------------------------------------------------------------------------
#  Function:    Restore space on the stack for calling C-function
#
# Acording to System V ABI, the first 6 parameters are passed by registers
# so there is no need to allocate spaces in stack for the function call with
# less 6 parameters. And the following call instruction will push another 8
# bytes so add/sub 8 bytes to make the stack alignment in 16 bytes.
#------------------------------------------------------------------------------
.macro RESTORE_C_STACK
        add     $8, %rsp
.endm

#------------------------------------------------------------------------------
#  Function:    Allocates space on the stack for calling C-function
#
# Acording to System V ABI, the first 6 parameters are passed by registers
# so there is no need to allocate spaces in stack for the function call with
# less 6 parameters. And the following call instruction will push another 8
# bytes so add/sub 8 bytes to make the stack alignment in 16 bytes.
#------------------------------------------------------------------------------
.macro ALLOCATE_C_STACK
        sub     $8, %rsp
.endm

.macro VMCALL
        .ascii "\x0F\x01\xC1"
.endm

.macro VMLAUNCH
        .ascii "\x0F\x01\xC2"
.endm

.macro VMRESUME
        .ascii "\x0F\x01\xC3"
.endm


#------------------------------------------------------------------------------
#  Function:    Called upon VMEXIT. Saves GP registers, allocates stack
#               for C-function and calls it.
#
#  Arguments:   none
#------------------------------------------------------------------------------
.globl vmexit_func
vmexit_func:

        call    gcpu_save_registers
        ALLOCATE_C_STACK
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
        call    gcpu_restore_registers
        VMRESUME                        # Resume execution of Guest Virtual Machine
        jmp     handle_error

do_launch:
        call    gcpu_restore_registers
        VMLAUNCH                        # Launch execution of Guest Virtual Machine

handle_error:
        pushfq                          # use RFLAGS as argument if VMRESUME failed
        pop     %rdi                    # save RFLAGS in RDI as the 1st argument
        ALLOCATE_C_STACK                # for for vmentry_failure_function
        call    vmentry_failure_function
        RESTORE_C_STACK
        pop     %rdi                    # restore RDI. stack is expected to be the same as in entry point
        jmp     vmentry_func            # retry


#------------------------------------------------------------------------------
#  Function:    hw_vmcall
#
#  args passed as
#       %rdi,
#       %rsi,
#       %rdx,
#       %rcx
#
#  MON expects the following:
#      vmcall_id in RCX
#      arg1      in RDX
#      arg2      in RDI
#      arg3      in RSI
#
#  So to change the order we use below tables:
#       before call
#       %rdi -> rcx
#       %rsi -> rdx
#       %rdx -> rdi
#       %rcx -> rsi
#       after call
#       rcx -> %rdi
#       rdx -> %rsi
#       rdi -> %rdx
#       rsi -> %rcx
#
#  return value in RAX
#------------------------------------------------------------------------------
MON_NATIVE_VMCALL_SIGNATURE = 0X24694D40	### $iM@
.globl hw_vmcall
hw_vmcall:
        # push in the order args appear in call
        push    %rdi
        push    %rsi
        push    %rdx
        push    %rcx

        # pop in the order MON expects for
        pop     %rsi
        pop     %rdi
        pop     %rdx
        pop     %rcx

        mov     $MON_NATIVE_VMCALL_SIGNATURE, %rax

        VMCALL

        # push in the order args appear in MON
        push    %rcx
        push    %rdx
        push    %rdi
        push    %rsi

        # pop in the order GCC expects for
        pop     %rcx
        pop     %rdx
        pop     %rsi
        pop     %rdi

        ret


