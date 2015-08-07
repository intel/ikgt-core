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

#include "gas_defs.h"

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

.extern g_exception_gpr
.extern exception_class
.extern isr_c_handler

#define VECTOR_19 19
#define FAULT_CLASS 2

#------------------------------------------------------------------------------
#  hw_isr_c_wrapper()
#
#  Saves on the stack IA32e registers, which are not saved across function calls.
#  Prepares stack with the structure on the stack and calls isr_c_handler() with
#  the address of this structure as a single arguments to the callee function.
#  Upon returning from isr_c_handler(), restores saved registers.
# Regs to save:
#       %rax, %rdi, %rsi, %rdx, %rcx, %r8, %r9, %r10, %r11
#------------------------------------------------------------------------------
.globl hw_isr_c_wrapper
hw_isr_c_wrapper:
        # If an exception fault is detected, save the GRPs
        # for the assertion debug buffer
        push    %rax            # offset 0x08
        push    %rbx            # offset 0x00

        mov     0x10(%rsp), %rbx  # vector number
        # all exception faults have vector number up to 19
        cmpq    $VECTOR_19, %rbx
        jg      continue

        # check the exception type
        lea     exception_class(%rip), %rax
        movzbl  (%rax, %rbx), %ebx
        cmpl    $FAULT_CLASS, %ebx
        jne     continue

        # save GPRs
        mov     8(%rsp), %rax   # this is rax
        lea     g_exception_gpr(%rip), %rbx
        mov     %rax, GR_REG_OFFSET(IA32_REG_RAX)(%rbx)
        mov     (%rsp), %rax    # this is rbx
        mov     %rax, GR_REG_OFFSET(IA32_REG_RBX)(%rbx)

        # now save all other GP registers except RIP, RSP, RFLAGS
        mov     %rcx, GR_REG_OFFSET(IA32_REG_RCX)(%rbx)
        mov     %rdx, GR_REG_OFFSET(IA32_REG_RDX)(%rbx)
        mov     %rdi, GR_REG_OFFSET(IA32_REG_RDI)(%rbx)
        mov     %rsi, GR_REG_OFFSET(IA32_REG_RSI)(%rbx)
        mov     %rbp, GR_REG_OFFSET(IA32_REG_RBP)(%rbx)
        mov     %r8, GR_REG_OFFSET(IA32_REG_R8)(%rbx)
        mov     %r9, GR_REG_OFFSET(IA32_REG_R9)(%rbx)
        mov     %r10, GR_REG_OFFSET(IA32_REG_R10)(%rbx)
        mov     %r11, GR_REG_OFFSET(IA32_REG_R11)(%rbx)
        mov     %r12, GR_REG_OFFSET(IA32_REG_R12)(%rbx)
        mov     %r13, GR_REG_OFFSET(IA32_REG_R13)(%rbx)
        mov     %r14, GR_REG_OFFSET(IA32_REG_R14)(%rbx)
        mov     %r15, GR_REG_OFFSET(IA32_REG_R15)(%rbx)

continue:
        pop     %rbx
        pop     %rax

        ## save context and prepare stack for C-function
        ## at this point stack contains
        ##..................................
        ## [       SS         ]
        ## [       RSP        ]
        ## [      RFLAGS      ]
        ## [       CS         ]
        ## [       RIP        ] <= here RSP should point prior iret
        ## [[   errcode      ]]    optionally
        ## [    vector ID     ] <= RSP
        push    %rdi            # save RDI which used for argument passing
        mov     %rsp, %rdi
        add     $8, %rdi        # now RDI points to the location of vector ID
        push    %rax
        push    %rsi
        push    %rdx
        push    %rcx
        push    %r8
        push    %r9
        push    %r10
        push    %r11
        push    %r15            # used for saving unaligned stack
        mov     %rsp, %r15      # save RSP prior alignment
        and     $0xFFFFFFFFFFFFFFF0, %rsp  # align on 16 bytes boundary
        sub     $8, %rsp        # prepare stack for C-function
                                # and keep align on 16 bytes
        call    isr_c_handler   # call push another 8 bytes on stack
        mov     %r15, %rsp      # restore unaligned RSP
        pop     %r15
        pop     %r11
        pop     %r10
        pop     %r9
        pop     %r8
        pop     %rcx
        pop     %rdx
        pop     %rsi
        pop     %rax
        pop     %rdi
        pop     %rsp            # isr_c_handler replaces vector ID with pointer to the
                                # RIP. Just pop the pointer to the RIP into RSP.
        iretq
# end of hw_isr_c_wrapper()


#------------------------------------------------------------------------------#
#  Functions below serve as ISR handlers for 256 vectors (IDT entries)
#  They push hardcoded interrupt vector on stack and jump to hw_isr_c_wrapper()
#------------------------------------------------------------------------------#

.globl isr_entry_00
isr_entry_00:
        pushq  $0x00
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_01
isr_entry_01:
        pushq  $0x01
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_02
isr_entry_02:
        pushq  $0x02
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_03
isr_entry_03:
        pushq  $0x03
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_04
isr_entry_04:
        pushq  $0x04
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_05
isr_entry_05:
        pushq  $0x05
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_06
isr_entry_06:
        pushq  $0x06
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_07
isr_entry_07:
        pushq  $0x07
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_08
isr_entry_08:
        pushq  $0x08
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_09
isr_entry_09:
        pushq  $0x09
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_0a
isr_entry_0a:
        pushq  $0x0a
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_0b
isr_entry_0b:
        pushq  $0x0b
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_0c
isr_entry_0c:
        pushq  $0x0c
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_0d
isr_entry_0d:
        pushq  $0x0d
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_0e
isr_entry_0e:
        pushq  $0x0e
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_0f
isr_entry_0f:
        pushq  $0x0f
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_10
isr_entry_10:
        pushq  $0x10
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_11
isr_entry_11:
        pushq  $0x11
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_12
isr_entry_12:
        pushq  $0x12
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_13
isr_entry_13:
        pushq  $0x13
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_14
isr_entry_14:
        pushq  $0x14
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_15
isr_entry_15:
        pushq  $0x15
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_16
isr_entry_16:
        pushq  $0x16
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_17
isr_entry_17:
        pushq  $0x17
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_18
isr_entry_18:
        pushq  $0x18
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_19
isr_entry_19:
        pushq  $0x19
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_1a
isr_entry_1a:
        pushq  $0x1a
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_1b
isr_entry_1b:
        pushq  $0x1b
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_1c
isr_entry_1c:
        pushq  $0x1c
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_1d
isr_entry_1d:
        pushq  $0x1d
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_1e
isr_entry_1e:
        pushq  $0x1e
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_1f
isr_entry_1f:
        pushq  $0x1f
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_20
isr_entry_20:
        pushq  $0x20
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_21
isr_entry_21:
        pushq  $0x21
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_22
isr_entry_22:
        pushq  $0x22
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_23
isr_entry_23:
        pushq  $0x23
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_24
isr_entry_24:
        pushq  $0x24
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_25
isr_entry_25:
        pushq  $0x25
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_26
isr_entry_26:
        pushq  $0x26
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_27
isr_entry_27:
        pushq  $0x27
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_28
isr_entry_28:
        pushq  $0x28
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_29
isr_entry_29:
        pushq  $0x29
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_2a
isr_entry_2a:
        pushq  $0x2a
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_2b
isr_entry_2b:
        pushq  $0x2b
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_2c
isr_entry_2c:
        pushq  $0x2c
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_2d
isr_entry_2d:
        pushq  $0x2d
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_2e
isr_entry_2e:
        pushq  $0x2e
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_2f
isr_entry_2f:
        pushq  $0x2f
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_30
isr_entry_30:
        pushq  $0x30
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_31
isr_entry_31:
        pushq  $0x31
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_32
isr_entry_32:
        pushq  $0x32
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_33
isr_entry_33:
        pushq  $0x33
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_34
isr_entry_34:
        pushq  $0x34
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_35
isr_entry_35:
        pushq  $0x35
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_36
isr_entry_36:
        pushq  $0x36
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_37
isr_entry_37:
        pushq  $0x37
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_38
isr_entry_38:
        pushq  $0x38
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_39
isr_entry_39:
        pushq  $0x39
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_3a
isr_entry_3a:
        pushq  $0x3a
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_3b
isr_entry_3b:
        pushq  $0x3b
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_3c
isr_entry_3c:
        pushq  $0x3c
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_3d
isr_entry_3d:
        pushq  $0x3d
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_3e
isr_entry_3e:
        pushq  $0x3e
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_3f
isr_entry_3f:
        pushq  $0x3f
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_40
isr_entry_40:
        pushq  $0x40
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_41
isr_entry_41:
        pushq  $0x41
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_42
isr_entry_42:
        pushq  $0x42
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_43
isr_entry_43:
        pushq  $0x43
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_44
isr_entry_44:
        pushq  $0x44
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_45
isr_entry_45:
        pushq  $0x45
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_46
isr_entry_46:
        pushq  $0x46
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_47
isr_entry_47:
        pushq  $0x47
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_48
isr_entry_48:
        pushq  $0x48
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_49
isr_entry_49:
        pushq  $0x49
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_4a
isr_entry_4a:
        pushq  $0x4a
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_4b
isr_entry_4b:
        pushq  $0x4b
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_4c
isr_entry_4c:
        pushq  $0x4c
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_4d
isr_entry_4d:
        pushq  $0x4d
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_4e
isr_entry_4e:
        pushq  $0x4e
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_4f
isr_entry_4f:
        pushq  $0x4f
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_50
isr_entry_50:
        pushq  $0x50
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_51
isr_entry_51:
        pushq  $0x51
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_52
isr_entry_52:
        pushq  $0x52
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_53
isr_entry_53:
        pushq  $0x53
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_54
isr_entry_54:
        pushq  $0x54
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_55
isr_entry_55:
        pushq  $0x55
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_56
isr_entry_56:
        pushq  $0x56
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_57
isr_entry_57:
        pushq  $0x57
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_58
isr_entry_58:
        pushq  $0x58
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_59
isr_entry_59:
        pushq  $0x59
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_5a
isr_entry_5a:
        pushq  $0x5a
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_5b
isr_entry_5b:
        pushq  $0x5b
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_5c
isr_entry_5c:
        pushq  $0x5c
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_5d
isr_entry_5d:
        pushq  $0x5d
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_5e
isr_entry_5e:
        pushq  $0x5e
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_5f
isr_entry_5f:
        pushq  $0x5f
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_60
isr_entry_60:
        pushq  $0x60
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_61
isr_entry_61:
        pushq  $0x61
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_62
isr_entry_62:
        pushq  $0x62
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_63
isr_entry_63:
        pushq  $0x63
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_64
isr_entry_64:
        pushq  $0x64
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_65
isr_entry_65:
        pushq  $0x65
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_66
isr_entry_66:
        pushq  $0x66
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_67
isr_entry_67:
        pushq  $0x67
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_68
isr_entry_68:
        pushq  $0x68
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_69
isr_entry_69:
        pushq  $0x69
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_6a
isr_entry_6a:
        pushq  $0x6a
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_6b
isr_entry_6b:
        pushq  $0x6b
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_6c
isr_entry_6c:
        pushq  $0x6c
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_6d
isr_entry_6d:
        pushq  $0x6d
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_6e
isr_entry_6e:
        pushq  $0x6e
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_6f
isr_entry_6f:
        pushq  $0x6f
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_70
isr_entry_70:
        pushq  $0x70
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_71
isr_entry_71:
        pushq  $0x71
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_72
isr_entry_72:
        pushq  $0x72
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_73
isr_entry_73:
        pushq  $0x73
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_74
isr_entry_74:
        pushq  $0x74
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_75
isr_entry_75:
        pushq  $0x75
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_76
isr_entry_76:
        pushq  $0x76
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_77
isr_entry_77:
        pushq  $0x77
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_78
isr_entry_78:
        pushq  $0x78
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_79
isr_entry_79:
        pushq  $0x79
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_7a
isr_entry_7a:
        pushq  $0x7a
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_7b
isr_entry_7b:
        pushq  $0x7b
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_7c
isr_entry_7c:
        pushq  $0x7c
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_7d
isr_entry_7d:
        pushq  $0x7d
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_7e
isr_entry_7e:
        pushq  $0x7e
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_7f
isr_entry_7f:
        pushq  $0x7f
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_80
isr_entry_80:
        pushq  $0x80
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_81
isr_entry_81:
        pushq  $0x81
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_82
isr_entry_82:
        pushq  $0x82
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_83
isr_entry_83:
        pushq  $0x83
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_84
isr_entry_84:
        pushq  $0x84
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_85
isr_entry_85:
        pushq  $0x85
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_86
isr_entry_86:
        pushq  $0x86
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_87
isr_entry_87:
        pushq  $0x87
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_88
isr_entry_88:
        pushq  $0x88
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_89
isr_entry_89:
        pushq  $0x89
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_8a
isr_entry_8a:
        pushq  $0x8a
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_8b
isr_entry_8b:
        pushq  $0x8b
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_8c
isr_entry_8c:
        pushq  $0x8c
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_8d
isr_entry_8d:
        pushq  $0x8d
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_8e
isr_entry_8e:
        pushq  $0x8e
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_8f
isr_entry_8f:
        pushq  $0x8f
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_90
isr_entry_90:
        pushq  $0x90
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_91
isr_entry_91:
        pushq  $0x91
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_92
isr_entry_92:
        pushq  $0x92
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_93
isr_entry_93:
        pushq  $0x93
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_94
isr_entry_94:
        pushq  $0x94
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_95
isr_entry_95:
        pushq  $0x95
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_96
isr_entry_96:
        pushq  $0x96
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_97
isr_entry_97:
        pushq  $0x97
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_98
isr_entry_98:
        pushq  $0x98
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_99
isr_entry_99:
        pushq  $0x99
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_9a
isr_entry_9a:
        pushq  $0x9a
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_9b
isr_entry_9b:
        pushq  $0x9b
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_9c
isr_entry_9c:
        pushq  $0x9c
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_9d
isr_entry_9d:
        pushq  $0x9d
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_9e
isr_entry_9e:
        pushq  $0x9e
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_9f
isr_entry_9f:
        pushq  $0x9f
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_a0
isr_entry_a0:
        pushq  $0xa0
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_a1
isr_entry_a1:
        pushq  $0xa1
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_a2
isr_entry_a2:
        pushq  $0xa2
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_a3
isr_entry_a3:
        pushq  $0xa3
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_a4
isr_entry_a4:
        pushq  $0xa4
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_a5
isr_entry_a5:
        pushq  $0xa5
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_a6
isr_entry_a6:
        pushq  $0xa6
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_a7
isr_entry_a7:
        pushq  $0xa7
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_a8
isr_entry_a8:
        pushq  $0xa8
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_a9
isr_entry_a9:
        pushq  $0xa9
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_aa
isr_entry_aa:
        pushq  $0xaa
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ab
isr_entry_ab:
        pushq  $0xab
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ac
isr_entry_ac:
        pushq  $0xac
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ad
isr_entry_ad:
        pushq  $0xad
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ae
isr_entry_ae:
        pushq  $0xae
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_af
isr_entry_af:
        pushq  $0xaf
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_b0
isr_entry_b0:
        pushq  $0xb0
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_b1
isr_entry_b1:
        pushq  $0xb1
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_b2
isr_entry_b2:
        pushq  $0xb2
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_b3
isr_entry_b3:
        pushq  $0xb3
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_b4
isr_entry_b4:
        pushq  $0xb4
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_b5
isr_entry_b5:
        pushq  $0xb5
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_b6
isr_entry_b6:
        pushq  $0xb6
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_b7
isr_entry_b7:
        pushq  $0xb7
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_b8
isr_entry_b8:
        pushq  $0xb8
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_b9
isr_entry_b9:
        pushq  $0xb9
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ba
isr_entry_ba:
        pushq  $0xba
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_bb
isr_entry_bb:
        pushq  $0xbb
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_bc
isr_entry_bc:
        pushq  $0xbc
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_bd
isr_entry_bd:
        pushq  $0xbd
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_be
isr_entry_be:
        pushq  $0xbe
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_bf
isr_entry_bf:
        pushq  $0xbf
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_c0
isr_entry_c0:
        pushq  $0xc0
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_c1
isr_entry_c1:
        pushq  $0xc1
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_c2
isr_entry_c2:
        pushq  $0xc2
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_c3
isr_entry_c3:
        pushq  $0xc3
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_c4
isr_entry_c4:
        pushq  $0xc4
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_c5
isr_entry_c5:
        pushq  $0xc5
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_c6
isr_entry_c6:
        pushq  $0xc6
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_c7
isr_entry_c7:
        pushq  $0xc7
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_c8
isr_entry_c8:
        pushq  $0xc8
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_c9
isr_entry_c9:
        pushq  $0xc9
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ca
isr_entry_ca:
        pushq  $0xca
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_cb
isr_entry_cb:
        pushq  $0xcb
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_cc
isr_entry_cc:
        pushq  $0xcc
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_cd
isr_entry_cd:
        pushq  $0xcd
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ce
isr_entry_ce:
        pushq  $0xce
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_cf
isr_entry_cf:
        pushq  $0xcf
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_d0
isr_entry_d0:
        pushq  $0xd0
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_d1
isr_entry_d1:
        pushq  $0xd1
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_d2
isr_entry_d2:
        pushq  $0xd2
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_d3
isr_entry_d3:
        pushq  $0xd3
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_d4
isr_entry_d4:
        pushq  $0xd4
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_d5
isr_entry_d5:
        pushq  $0xd5
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_d6
isr_entry_d6:
        pushq  $0xd6
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_d7
isr_entry_d7:
        pushq  $0xd7
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_d8
isr_entry_d8:
        pushq  $0xd8
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_d9
isr_entry_d9:
        pushq  $0xd9
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_da
isr_entry_da:
        pushq  $0xda
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_db
isr_entry_db:
        pushq  $0xdb
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_dc
isr_entry_dc:
        pushq  $0xdc
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_dd
isr_entry_dd:
        pushq  $0xdd
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_de
isr_entry_de:
        pushq  $0xde
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_df
isr_entry_df:
        pushq  $0xdf
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_e0
isr_entry_e0:
        pushq  $0xe0
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_e1
isr_entry_e1:
        pushq  $0xe1
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_e2
isr_entry_e2:
        pushq  $0xe2
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_e3
isr_entry_e3:
        pushq  $0xe3
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_e4
isr_entry_e4:
        pushq  $0xe4
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_e5
isr_entry_e5:
        pushq  $0xe5
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_e6
isr_entry_e6:
        pushq  $0xe6
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_e7
isr_entry_e7:
        pushq  $0xe7
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_e8
isr_entry_e8:
        pushq  $0xe8
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_e9
isr_entry_e9:
        pushq  $0xe9
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ea
isr_entry_ea:
        pushq  $0xea
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_eb
isr_entry_eb:
        pushq  $0xeb
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ec
isr_entry_ec:
        pushq  $0xec
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ed
isr_entry_ed:
        pushq  $0xed
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ee
isr_entry_ee:
        pushq  $0xee
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ef
isr_entry_ef:
        pushq  $0xef
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_f0
isr_entry_f0:
        pushq  $0xf0
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_f1
isr_entry_f1:
        pushq  $0xf1
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_f2
isr_entry_f2:
        pushq  $0xf2
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_f3
isr_entry_f3:
        pushq  $0xf3
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_f4
isr_entry_f4:
        pushq  $0xf4
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_f5
isr_entry_f5:
        pushq  $0xf5
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_f6
isr_entry_f6:
        pushq  $0xf6
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_f7
isr_entry_f7:
        pushq  $0xf7
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_f8
isr_entry_f8:
        pushq  $0xf8
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_f9
isr_entry_f9:
        pushq  $0xf9
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_fa
isr_entry_fa:
        pushq  $0xfa
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_fb
isr_entry_fb:
        pushq  $0xfb
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_fc
isr_entry_fc:
        pushq  $0xfc
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_fd
isr_entry_fd:
        pushq  $0xfd
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_fe
isr_entry_fe:
        pushq  $0xfe
        jmp hw_isr_c_wrapper
#-- eof

.globl isr_entry_ff
isr_entry_ff:
        pushq  $0xff
        jmp hw_isr_c_wrapper
#-- eof

