/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

.extern cpu_num
.extern efi_hand_over_entry
.align 8
.text

.global ap_start
.code64
ap_start:
	/*
	 * According to UEFI calling convention(MS_ABI), the registers
	 * RBX, RBP, RDI, RSI, RSP, R12, R13, R14 and R15 are considered
	 * non-volatile and must be saved and restored by a function that
	 * uses them.
	 */
	push %rbx
	push %rbp
	push %rdi
	push %rsi
	push %rsp
	push %r12
	push %r13
	push %r14
	push %r15
	pushfq

	cli

	/* calculate cpu_id */
	mov $1, %rax
	lock xadd %rax, cpu_num(%rip)

	/*
	 * Prepare parameters for c_entry():
	 *     RDI: cpu_id
	 *     RSI: old_rsp
	 */
	mov %rax, %rdi
	mov %rsp, %rsi

	call *efi_hand_over_entry(%rip)

	popfq
	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %rsp
	pop %rsi
	pop %rdi
	pop %rbp
	pop %rbx
	ret

/* Never hit here */
stop:
	jmp .
