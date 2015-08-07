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
#     em64t_utils.S
#
#   Abstract:
#
#*****************************************************************************

#include "gas_defs.h"


## structure definition for hw_write_to_smi_port()
        .struct 0
P_RAX:
        .struct P_RAX + 8
P_RBX:
        .struct P_RBX + 8
P_RCX:
        .struct P_RCX + 8
P_RDX:
        .struct P_RDX + 8
P_RSI:
        .struct P_RSI + 8
P_RDI:
        .struct P_RDI + 8
RET_ADDR:
        .struct RET_ADDR + 8
P_RFLAGS:


## structure definition for hw_cpuid()
        .struct 0
M_RAX:
        .struct M_RAX + 8
M_RBX:
        .struct M_RBX + 8
M_RCX:
        .struct M_RCX + 8
M_RDX:


.text



#------------------------------------------------------------------------------
# Registers for passing parameters:
#       %rdi, %rsi, %rdx, %rcx, %r8, %r9
# The rest  parameters are in stack
# Callee saved regs
#       %rbp,%rbx,%r12-%r15, %rsp
# System calls via syscall
# Kernel destroys %r11, %rcx
# The number of syscall is in %rax
#------------------------------------------------------------------------------


#------------------------------------------------------------------------------
#  void cdecl
#  hw_lgdt (
#          void * gdtr
#  )
#
#  Load GDTR (from buffer pointed by RCX)
#------------------------------------------------------------------------------
.globl hw_lgdt
hw_lgdt:
        lgdt    (ARG1_U64)
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_sgdt (
#          void * gdtr
#  )
#
#  Store GDTR (to buffer pointed by RCX)
#------------------------------------------------------------------------------
.globl hw_sgdt
hw_sgdt:
        sgdt    (ARG1_U64)
        ret


#------------------------------------------------------------------------------
#  uint16_t cdecl
#  hw_read_cs (
#          void
#  )
#
#  Read Command Segment Selector
#
#  Stack offsets on entry:
#
#  ax register will contain result
#------------------------------------------------------------------------------
.globl hw_read_cs
hw_read_cs:
        xor     %rax, %rax
        mov     %cs, %ax
        ret


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
        shl     $32, %rax
        lea     CONT_WITH_NEW_CS (%rip), %rdx
        add     %rdx, %rax
        push    %rax
        lret                    # brings IP to CONT_WITH_NEW_CS
CONT_WITH_NEW_CS:
        ret


#------------------------------------------------------------------------------
#  uint16_t cdecl
#  hw_read_ds (
#          void
#  )
#
#  Read Data Segment Selector
#
#  Stack offsets on entry:
#
#  ax register will contain result
#------------------------------------------------------------------------------
.globl hw_read_ds
hw_read_ds:
        xor     %rax, %rax
        movw    %ds, %ax
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_write_ds (
#          uint16_t
#  )
#
#  Write to Data Segment Selector
#
#------------------------------------------------------------------------------
.globl hw_write_ds
hw_write_ds:
        mov     ARG1_U16, %ds
        ret


#------------------------------------------------------------------------------
#  uint16_t cdecl
#  hw_read_es (
#          void
#  )
#
#  Read ES Segment Selector
#
#  Stack offsets on entry:
#
#  ax register will contain result
#------------------------------------------------------------------------------
.globl hw_read_es
hw_read_es:
        xor     %rax, %rax
        mov     %es, %ax
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_write_es (
#          uint16_t
#  )
#
#  Write to ES Segment Selector
#
#------------------------------------------------------------------------------
.globl hw_write_es
hw_write_es:
        mov     ARG1_U16, %es
        ret


#------------------------------------------------------------------------------
#  uint16_t cdecl
#  hw_read_ss (
#          void
#  )
#
#  Read Stack Segment Selector
#
#  ax register will contain result
#------------------------------------------------------------------------------
.globl hw_read_ss
hw_read_ss:
        xor     %rax, %rax
        mov     %ss, %ax
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_write_ss (
#          uint16_t
#  )
#
#  Write to Stack Segment Selector
#
#------------------------------------------------------------------------------
.globl hw_write_ss
hw_write_ss:
        mov     ARG1_U16, %ss
        ret


#------------------------------------------------------------------------------
#  uint16_t cdecl
#  hw_read_fs (
#          void
#  )
#
#  Read FS
#
#  ax register will contain result
#------------------------------------------------------------------------------
.globl hw_read_fs
hw_read_fs:
        xor     %rax, %rax
        mov     %fs, %ax
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_write_fs (
#          uint16_t
#  )
#
#  Write to FS
#
#------------------------------------------------------------------------------
.globl hw_write_fs
hw_write_fs:
        mov     ARG1_U16, %fs
        ret


#------------------------------------------------------------------------------
#  uint16_t cdecl
#  hw_read_gs (
#          void
#  )
#
#  Read GS
#
#  ax register will contain result
#------------------------------------------------------------------------------
.globl hw_read_gs
hw_read_gs:
        xor     %rax, %rax
        mov     %gs, %ax
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_write_gs (
#          uint16_t
#  )
#
#  Write to GS
#
#------------------------------------------------------------------------------
.globl hw_write_gs
hw_write_gs:
        mov     ARG1_U16, %gs
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_set_stack_pointer (
#          hva_t new_stack_pointer,
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
        sub     $32, %rsp               # allocate home space for 4 input params
        call    *ARG2_U64
        jmp     .
        ret


#------------------------------------------------------------------------------
#  uint64_t cdecl
#  hw_read_rsp (void)
#
#
#------------------------------------------------------------------------------
.globl hw_read_rsp
hw_read_rsp:
        mov     %rsp, %rax
        add     $8, %rax
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_write_to_smi_port(
#               uint64_t * p_rax,     // rdi
#               uint64_t * p_rbx,     // rsi
#               uint64_t * p_rcx,     // rdx
#               uint64_t * p_rdx,     // rcx
#               uint64_t * p_rsi,     // r8
#               uint64_t * p_rdi,     // r9
#               uint64_t * p_rflags   // on the stack
#               )
#
#  Fill HW regs from emulator context before writing to the port,
#  and fill emulator context registers from HW after the write.
#  Also preserve callee saved HW registers, and restore them at return
#       %rbp,%rbx,%r12-%r15, %rsp
#------------------------------------------------------------------------------

.globl hw_write_to_smi_port
hw_write_to_smi_port:

        ## Push args onto stack, in backward order, thus filling structure.
        ## Since P_RFLAGS is already on the stack, skip it
        push    ARG6_U64        # p_rdi
        push    ARG5_U64        # p_rsi
        push    ARG4_U64        # p_rdx
        push    ARG3_U64        # p_rcx
        push    ARG2_U64        # p_rbx
        push    ARG1_U64        # p_rax

        ## save callee saved registers. We assume rsp does not changed during SMI
        push    %rbp
        mov     %rsp, %rbp      # setup stack frame pointer
        add     $8, %rbp        # set rbp to point to .struct
        push    %rbx            # save callee saved HW registers
        push    %r12
        push    %r13
        push    %r14
        push    %r15

        ## copy emulator registers into CPU
        movq    P_RAX (%rbp), %r15
        movq    (%r15), %rax
        movq    P_RBX (%rbp), %r15
        movq    (%r15), %rbx
        movq    P_RCX (%rbp), %r15
        movq    (%r15), %rcx
        movq    P_RDX (%rbp), %r15
        movq    (%r15), %rdx
        movq    P_RSI (%rbp), %r15
        movq    (%r15), %rsi
        movq    P_RDI (%rbp), %r15
        movq    (%r15), %rdi
        movq    P_RFLAGS (%rbp), %r15
        pushq   (%r15)
        popfq

        ## we assume that sp will not change after SMI
        push    %rbp
        outb    %al, %dx
        pop     %rbp

        ## fill emulator registers from CPU
        movq    P_RAX (%rbp), %r15
        movq    %rax, (%r15)
        movq    P_RBX (%rbp), %r15
        movq    %rbx, (%r15)
        movq    P_RCX (%rbp), %r15
        movq    %rcx, (%r15)
        movq    P_RDX (%rbp), %r15
        movq    %rdx, (%r15)
        movq    P_RSI (%rbp), %r15
        movq    %rsi, (%r15)
        movq    P_RDI (%rbp), %r15
        movq    %rdi, (%r15)
        movq    P_RFLAGS (%rbp), %r15
        pushfq
        popq   (%r15)

        ## restore callee saved registers
        pop     %r15
        pop     %r14
        pop     %r13
        pop     %r12
        pop     %rbx
        pop     %rbp

        add     $48, %rsp       # remove 6 ARGs from the stack
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_enable_interrupts (void)
#
#
#------------------------------------------------------------------------------
.globl hw_enable_interrupts
hw_enable_interrupts:
        sti
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_disable_interrupts (void)
#
#
#------------------------------------------------------------------------------
.globl hw_disable_interrupts
hw_disable_interrupts:
        cli
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_fxsave (void* buffer)
#
#
#------------------------------------------------------------------------------
.globl hw_fxsave
hw_fxsave:
        fxsave (ARG1_U64)
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_fxrestore (void* buffer)
#
#
#------------------------------------------------------------------------------
.globl hw_fxrestore
hw_fxrestore:
        fxrstor (ARG1_U64)
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_write_cr2 (uint64_t value)
#
#
#------------------------------------------------------------------------------
.globl hw_write_cr2
hw_write_cr2:
        mov ARG1_U64, %cr2
        ret


#------------------------------------------------------------------------------
#  uint16_t cdecl
#  hw_cpu_id (
#          void
#  )
#
#  Read TR and calculate cpu_id
#
#  ax register will contain result
#
#  IMPORTANT NOTE: only RAX regsiter may be used here !!!!
#                  This assumption is used in gcpu_regs_save_restore.asm
#------------------------------------------------------------------------------
#### define CPU_LOCATOR_GDT_ENTRY_OFFSET 48
CPU_LOCATOR_GDT_ENTRY_OFFSET = 32
TSS_ENTRY_SIZE_SHIFT         = 4

.globl hw_cpu_id
hw_cpu_id:
        xor     %rax, %rax
        str     %ax
        sub     $CPU_LOCATOR_GDT_ENTRY_OFFSET, %ax
        shr     $TSS_ENTRY_SIZE_SHIFT, %ax
        ret


#------------------------------------------------------------------------------
#  uint16_t cdecl
#  hw_read_tr (
#          void
#  )
#
#  Read Task Register
#
#  ax register will contain result
#------------------------------------------------------------------------------
.globl hw_read_tr
hw_read_tr:
        str     %ax
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_write_tr (
#          uint16_t
#  )
#
#  Write Task Register
#
#------------------------------------------------------------------------------
.globl hw_write_tr
hw_write_tr:
        ltr     ARG1_U16
        ret


#------------------------------------------------------------------------------
#  uint16_t cdecl
#  hw_read_ldtr (
#          void
#  )
#
#  Read LDT Register
#
#  ax register will contain result
#------------------------------------------------------------------------------
.globl hw_read_ldtr
hw_read_ldtr:
        sldt   %ax
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_write_ldtr (
#          uint16_t
#  )
#
#  Write LDT Register
#
#------------------------------------------------------------------------------
.globl hw_write_ldtr
hw_write_ldtr:
        lldt   ARG1_U16
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_cpuid (
#       cpuid_params_t *          ARG1_U64
#  )
#
#  Execute cpuid instruction
#
#------------------------------------------------------------------------------
.globl hw_cpuid
hw_cpuid:
        # store regs b, c, d
        pushq   %rbx
        pushq   %rcx
        pushq   %rdx
        # fill regs for cpuid
        mov     M_RAX (ARG1_U64), %rax
        mov     M_RBX (ARG1_U64), %rbx
        mov     M_RCX (ARG1_U64), %rcx
        mov     M_RDX (ARG1_U64), %rdx
        cpuid
        mov     %rax, M_RAX (ARG1_U64)
        mov     %rbx, M_RBX (ARG1_U64)
        mov     %rcx, M_RCX (ARG1_U64)
        mov     %rdx, M_RDX (ARG1_U64)
        # restore regs b, c, d
        popq    %rdx
        popq    %rcx
        popq    %rbx
        ret


#------------------------------------------------------------------------------
#  void cdecl
#  hw_leave_64bit_mode ()
#  Arguments:   uint32_t compatibility_segment  CX
#               uint16_t port_id                DX
#               uint16_t value                  R8
#               uint32_t cr3_value              R9
#------------------------------------------------------------------------------
#hw_leave_64bit_mode PROC
#
#        jmp $
#
#        shl rcx, 32             ## prepare segment:offset pair for retf by shifting
#                                ## compatibility segment in high address
#        lea rax, compat_code    ## and
#        add rcx, rax            ## placing offset into low address
#        push rcx                ## push ret address onto stack
#        mov  rsi, rdx           ## rdx will be used during EFER access
#        mov  rdi, r8            ## r8 will be unaccessible, so use rsi instead
#        mov  rbx, r9            ## save CR3 in RBX. this function is the last called, so we have not to save rbx
#        retf                    ## jump to compatibility mode
#compat_code:                    ## compatibility mode starts right here
#
#        mov rax, cr0            ## only 32-bit are relevant
#        btc eax, 31             ## disable IA32e paging (64-bits)
#        mov cr0, rax            ##
#
#        ## now in protected mode
#        mov ecx, 0C0000080h     ## EFER MSR register
#        rdmsr                   ## read EFER into EAX
#        btc eax, 8              ## clear EFER.LME
#        wrmsr                   ## write EFER back
#
##        mov cr3, rbx            ## load CR3 for 32-bit mode
##
##        mov rax, cr0            ## use Rxx notation for compiler, only 32-bit are valuable
##        bts eax, 31             ## enable IA32 paging (32-bits)
##        mov cr0, rax            ##
##        jmp @f
#
### now in 32-bit paging mode
#        mov rdx, rsi
#        mov rax, rdi
#        out dx, ax              ## write to PM register
#        ret                     ## should never get here
#hw_leave_64bit_mode ENDP


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

