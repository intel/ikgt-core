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

# Assumption - hw_cpu_id() uses RAX only and returns host cpu id in ax
.extern hw_cpu_id


# pointer to the array of pointers to the GUEST_CPU_SAVE_AREA_PREFIX
.extern g_guest_regs_save_area

#
# Load pointer to the active GUEST_CPU_SAVE_AREA_PREFIX into rbx
# No other registers are modified
#
.func load_save_area_into_rbx
load_save_area_into_rbx:

        # save RAX temporary
        push %rax

        # calculate host cpu id and put it into the rax (ax)
        call hw_cpu_id

        # put pointer to the array of GUEST_CPU_SAVE_AREA_PREFIX* to RBX
        movq    g_guest_regs_save_area (%rip), %rbx

        # put pointer to our GUEST_CPU_SAVE_AREA_PREFIX struct to RBX
        movq    (%rbx, %rax, 8), %rbx

        # restore RAX
        pop %rax
        ret



#****************************************************************************
#*
#* This functions are part of the guest_cpu_t class.
#* They are called by assembler-lever VmExit/VmResume functions
#* to save all registers that are not saved in VMCS but may be used immediately
#* by C-language MON code.
#*
#* The following registers are NOT saved here
#*
#*   RIP            part of VMCS
#*   RSP            part of VMCS
#*   RFLAGS         part of VMCS
#*   segment regs   part of VMCS
#*   control regs   saved in C-code later
#*   debug regs     saved in C-code later
#*   FP/MMX regs    saved in C-code later
#*
#* Assumptions:
#*   No free registers except of RSP/RFLAGS
#*   FS contains host CPU id (should be calculated)
#*
#****************************************************************************

#
# Assumption - no free registers on entry, all are saved on exit
#
.globl gcpu_save_registers
gcpu_save_registers:
        # save RAX and RBX temporary on a stack
        push    %rbx

        # put pointer to our GUEST_CPU_SAVE_AREA_PREFIX struct to RBX
        call    load_save_area_into_rbx

        # now save rax and rbx first
        movq    %rax, GR_REG_OFFSET(IA32_REG_RAX) (%rbx)

        pop     %rax    # this is rbx
        movq    %rax, GR_REG_OFFSET(IA32_REG_RBX) (%rbx)

        # now save all other GP registers except of RIP,RSP,RFLAGS
        movq    %rcx, GR_REG_OFFSET(IA32_REG_RCX) (%rbx)

        movq    %rdx, GR_REG_OFFSET(IA32_REG_RDX) (%rbx)
        movq    %rdi, GR_REG_OFFSET(IA32_REG_RDI) (%rbx)
        movq    %rsi, GR_REG_OFFSET(IA32_REG_RSI) (%rbx)
        movq    %rbp, GR_REG_OFFSET(IA32_REG_RBP) (%rbx)
        # skip RSP
        movq    %r8,  GR_REG_OFFSET(IA32_REG_R8)  (%rbx)
        movq    %r9,  GR_REG_OFFSET(IA32_REG_R9)  (%rbx)
        movq    %r10, GR_REG_OFFSET(IA32_REG_R10) (%rbx)
        movq    %r11, GR_REG_OFFSET(IA32_REG_R11) (%rbx)
        movq    %r12, GR_REG_OFFSET(IA32_REG_R12) (%rbx)
        movq    %r13, GR_REG_OFFSET(IA32_REG_R13) (%rbx)
        movq    %r14, GR_REG_OFFSET(IA32_REG_R14) (%rbx)
        movq    %r15, GR_REG_OFFSET(IA32_REG_R15) (%rbx)
        # skip RIP
        # skip RFLAGS

        # now save XMM registers
        movdqa  %xmm0, XMM_REG_OFFSET(0) (%rbx)
        movdqa  %xmm1, XMM_REG_OFFSET(1) (%rbx)
        movdqa  %xmm2, XMM_REG_OFFSET(2) (%rbx)
        movdqa  %xmm3, XMM_REG_OFFSET(3) (%rbx)
        movdqa  %xmm4, XMM_REG_OFFSET(4) (%rbx)
        movdqa  %xmm5, XMM_REG_OFFSET(5) (%rbx)
        movdqa  %xmm6, XMM_REG_OFFSET(6) (%rbx)
        movdqa  %xmm7, XMM_REG_OFFSET(7) (%rbx)
        movdqa  %xmm8, XMM_REG_OFFSET(8) (%rbx)
        movdqa  %xmm9, XMM_REG_OFFSET(9) (%rbx)
        movdqa  %xmm10, XMM_REG_OFFSET(10) (%rbx)
        movdqa  %xmm11, XMM_REG_OFFSET(11) (%rbx)
        movdqa  %xmm12, XMM_REG_OFFSET(12) (%rbx)
        movdqa  %xmm13, XMM_REG_OFFSET(13) (%rbx)
        movdqa  %xmm14, XMM_REG_OFFSET(14) (%rbx)
        movdqa  %xmm15, XMM_REG_OFFSET(15) (%rbx)

        # done
        ret


#
# Assumption - all free registers on entry, no free registers on exit
#
.globl gcpu_restore_registers
gcpu_restore_registers:

        # put pointer to our GUEST_CPU_SAVE_AREA_PREFIX struct to RBX
        call load_save_area_into_rbx

        # restore all XMM first
        movdqa  XMM_REG_OFFSET(0) (%rbx), %xmm0
        movdqa  XMM_REG_OFFSET(1) (%rbx), %xmm1
        movdqa  XMM_REG_OFFSET(2) (%rbx), %xmm2
        movdqa  XMM_REG_OFFSET(3) (%rbx), %xmm3
        movdqa  XMM_REG_OFFSET(4) (%rbx), %xmm4
        movdqa  XMM_REG_OFFSET(5) (%rbx), %xmm5
        movdqa  XMM_REG_OFFSET(6) (%rbx), %xmm6
        movdqa  XMM_REG_OFFSET(7) (%rbx), %xmm7
        movdqa  XMM_REG_OFFSET(8) (%rbx), %xmm8
        movdqa  XMM_REG_OFFSET(9) (%rbx), %xmm9
        movdqa  XMM_REG_OFFSET(10) (%rbx), %xmm10
        movdqa  XMM_REG_OFFSET(11) (%rbx), %xmm11
        movdqa  XMM_REG_OFFSET(12) (%rbx), %xmm12
        movdqa  XMM_REG_OFFSET(13) (%rbx), %xmm13
        movdqa  XMM_REG_OFFSET(14) (%rbx), %xmm14
        movdqa  XMM_REG_OFFSET(15) (%rbx), %xmm15


        # restore all GP except of RBX

        # now save all other GP registers except of RIP,RSP,RFLAGS
        movq    GR_REG_OFFSET(IA32_REG_RAX) (%rbx), %rax
        # RBX restore later
        movq    GR_REG_OFFSET(IA32_REG_RCX) (%rbx), %rcx
        movq    GR_REG_OFFSET(IA32_REG_RDX) (%rbx), %rdx
        movq    GR_REG_OFFSET(IA32_REG_RDI) (%rbx), %rdi
        movq    GR_REG_OFFSET(IA32_REG_RSI) (%rbx), %rsi
        movq    GR_REG_OFFSET(IA32_REG_RBP) (%rbx), %rbp
        # skip RSP
        movq    GR_REG_OFFSET(IA32_REG_R8) (%rbx), %r8
        movq    GR_REG_OFFSET(IA32_REG_R9) (%rbx), %r9
        movq    GR_REG_OFFSET(IA32_REG_R10) (%rbx), %r10
        movq    GR_REG_OFFSET(IA32_REG_R11) (%rbx), %r11
        movq    GR_REG_OFFSET(IA32_REG_R12) (%rbx), %r12
        movq    GR_REG_OFFSET(IA32_REG_R13) (%rbx), %r13
        movq    GR_REG_OFFSET(IA32_REG_R14) (%rbx), %r14
        movq    GR_REG_OFFSET(IA32_REG_R15) (%rbx), %r15
        # skip RIP
        # skip RFLAGS

        # restore RBX
        mov  GR_REG_OFFSET(IA32_REG_RBX) (%rbx), %rbx

        # done
        ret

