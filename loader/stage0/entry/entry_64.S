/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

.file   "entry_64.s"
#include "entry_asm.h"
#include "file_pack_asm.h"

.text

.extern entry_main

#---------------------------------------------------------------------
#  void_t start(void)
# This is the first loader componet to run after EFI bootloader and
# tranfer control to the evmm loader.
#---------------------------------------------------------------------

.globl start
.org 0
start:
.align 8

/****** tos image private header ******/
tos_image_header:
	/* 64 bit magic value*/
	.quad  TOS_HEADER_MAGIC
	/* version of this header */
	.long  TOS_HEADER_VERSION
	/* size of header structure */
	.long  tos_image_header_end - tos_image_header
	/* TOS image version and patch level combination */
	.long  TOS_IMAGE_VERSION
	/* entry offset */
	.long  start_x64 - start
	/* tos_ldr_size: EVMM_PKG_BIN_SIZE + STAGE1_IMG_SIZE + EVMM_PAYLOAD_SIZE */
	.long  EVMM_PKG_BIN_SIZE + STAGE1_RT_SIZE + EVMM_PAYLOAD_SIZE
	.byte  TOS_STARTUP_VERSION
	/* reserved */
	.fill 3, 1, 0
tos_image_header_end:

/* layout header for files (stage0.bin,evmm.bin,stage1.bin) mapped in RAM
 *  by search this header, to get the address of these
 *  file mapping memory location. 4 byte aligned.
 *  the evmmPacker must search this header, and update
 *  size for each component/file.
 */
file_mapping_hdr_info:
	/* MAGIC0/1 */
	.long   FILE_OFFSET_MAGIC0
	.long   FILE_OFFSET_MAGIC1
	/* binary size */
	/* stage0 */
	.long   0
	/* stage1 */
	.long   0
	/* evmm */
	.long   0
	/* lk */
	.long   0

/*
 * 64bit entry point, used by evmm boot header
 *  Scratch registers      - rax, rcx, rdx, rdi, rsi, r8-r11
 *  Callee saved registers - rbx, rbp, r12-r15
 *  Parameter transfer     - rdi, rsi, rdx, rcx, r8, r9
 *  Return registers       - rax, rdx
 */
.code64
start_x64:
	/* save the old register */
	pushq %rax
	pushq %rcx
	pushq %rdx
	pushq %rbx
	movq %rsp, %rax
	add $0x24, %rax
	pushq %rax
	pushq %rbp
	pushq %rsi
	pushq %rdi
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	pushfq

	/* reset rflags */
	pushq $0
	popfq

	/* save the old rsp into memory */
	leaq old_rsp(%rip), %rax
	movq %rsp, (%rax)

	/* setup loader's own stack, don't use the old rsp(it might be above 4G) */
	leaq bspstack(%rip), %rax
	movq %rax, %rsp

#ifdef STACK_PROTECTOR
	/* save old fs */
	leaq old_fs(%rip), %rax
	movw %fs, (%rax)

	xorw %ax, %ax
	movw %ax, %fs

	/* configure the stack cookie */
	movl $0xC0000100, %ecx                  /* FS_BASE_MSR */
	leaq stack_canary(%rip), %rbp
	movl %ebp, %eax
	shrq $32, %rbp
	movl %ebp, %edx
	wrmsr

	mov $10, %edx
1:
	rdrand %rax
	jc 2f
	decl %edx
	jnz 1b
	jz 0f
2:
	mov %rax, 0x28 + stack_canary(%rip)

0:
#endif

	/* prepare parameters when calling into entry_main() */
	leaq old_rsp(%rip), %rax
	movq (%rax), %rdi                       /* rdi: init registers */
	leaq file_mapping_hdr_info(%rip), %rsi  /* rsi: file_mapping_hdr_info */
	leaq start(%rip), %rdx                  /* rdx: load base of entry_x64.S */
	leaq stage0_runtime_base(%rip), %rcx    /* rcx: stage0 runtime base */
	leaq stage0_loadtime_base(%rip), %r8    /*  r8: stage0 loadtime base */

	callq entry_main

	/* restore the old rsp */
	leaq old_rsp(%rip), %rcx
	movq (%rcx), %rsp

#ifdef STACK_PROTECTOR
	/* restore old fs */
	leaq old_fs(%rip), %rbx
	movw (%rbx), %cx
	movw %cx, %fs
#endif

	popfq
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rbp
	add $8, %rsp /* skip rsp */
	popq %rbx
	popq %rdx
	popq %rcx
	add $8, %rsp /* skip rax */

	retq

old_rsp:
	.quad 0

old_fs:
	.quad 0

/* stack for stage0 */
.align 16
bspstack_start:
	.fill STAGE0_STACK_SIZE, 1, 0xCC
bspstack:

#ifdef STACK_PROTECTOR
.align 8
stack_canary:
	.fill 0x28, 1, 0  /* GCC hardcodes the stack cookie offset as 0x28 on x86-64 */
	.quad 0
#endif

.section .stage0_runtime
.align 8
stage0_runtime_base:
	.fill STAGE0_RT_SIZE, 1, 0
stage0_loadtime_base:
