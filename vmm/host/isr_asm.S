/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

.text
.extern isr_c_handler

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
	## at this point stack contains
	##..................................
	## [       SS         ]
	## [       RSP        ]
	## [      RFLAGS      ]
	## [       CS         ]
	## [       RIP        ] <= here RSP should point prior iret
	## [     errcode      ] <= -1 if Exception/Interrupt without error code
	## [    vector ID     ] <= RSP

	## save context and prepare parameter for C-function
	push	%rdi		# save RDI which used for argument passing
	mov	%rsp, %rdi
	add	$8, %rdi	# now RDI points to the location of vector ID
	push	%rax
	push	%rsi
	push	%rdx
	push	%rcx
	push	%r8
	push	%r9
	push	%r10
	push	%r11

	call	isr_c_handler	# call into isr_c_handler

	pop	%r11
	pop	%r10
	pop	%r9
	pop	%r8
	pop	%rcx
	pop	%rdx
	pop	%rsi
	pop	%rax
	pop	%rdi

	add	$16, %rsp	# adjust RSP to point RETURN RIP
	iretq
# end of hw_isr_c_wrapper()

.macro isr_entry_with_error_code vector_id:req
	.align 16
	pushq $(\vector_id)
	jmp hw_isr_c_wrapper
	.align 16
.endm

#PUSH -1 to stack before vector_id to align with entry has error code
.macro isr_entry_without_error_code vector_id:req
	.align 16
	pushq $-1
	pushq $(\vector_id)
	jmp hw_isr_c_wrapper
	.align 16
.endm

#------------------------------------------------------------------------------#
#  Build ISR entry stubs for 256 vectors (IDT entries)
#  Pack 1 stub into every 16 byte block
#------------------------------------------------------------------------------#
	.align 16
.globl isr_entries_start
isr_entries_start:

	# Exception: #DE, #DB, NMI, #BP, #OF, #BR, #UD, #NM
	vector=0
	.rept 8
	isr_entry_without_error_code vector_id=vector
	vector=vector+1
	.endr

	# Exception: #DF
	isr_entry_with_error_code vector_id=8

	# Exception: Coprocessor Segment Overrun
	isr_entry_without_error_code vector_id=9

	# Exception: #TS, #NP, #SS, #GP, #PF
	isr_entry_with_error_code vector_id=10
	isr_entry_with_error_code vector_id=11
	isr_entry_with_error_code vector_id=12
	isr_entry_with_error_code vector_id=13
	isr_entry_with_error_code vector_id=14

	# Exception:  Reserved, #PF
	isr_entry_without_error_code vector_id=15
	isr_entry_without_error_code vector_id=16

	# Exception: #AC
	isr_entry_with_error_code vector_id=17

	# Exception: #MC(18), #XM(19), #VE(20)
	# Reserved:  No.(21 to 31)
	# User Defined Interrupts: No.(32 to 255)
	vector=18
	.rept (256 - 18)
	isr_entry_without_error_code vector_id=vector
	vector=vector+1
	.endr

	.align 16
#ifdef DEBUG
.globl isr_entries_end
isr_entries_end:
#endif
# end of isr_entries_start
