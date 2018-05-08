################################################################################
# Copyright (c) 2017 Intel Corporation
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

.file   "stage0_entry.s"

#include "stage0_asm.h"
#include "file_pack_asm.h"

.text

.extern stage0_main

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
	/* tos_ldr_size: EVMM_PKG_BIN_SIZE + STAGE1_IMG_SIZE +
	 *               4KB(reserved for evmm_desc_t) */
	.long  EVMM_PKG_BIN_SIZE + STAGE1_IMG_SIZE + 0x1000
	/* reserved */
	.long 0
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
	pushfq
	pushq %rbx
	pushq %rbp
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

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

	/* prepare parameters when calling into stage0_main() */
	/* Note: rdi is passed from bootloader */
	leaq start(%rip), %rsi /* rsi saved load base of stage0_entry.S */
	movq %rsp, %rdx        /* rdx saved the new rsp */

	callq stage0_main

	/* restore the old rsp */
	leaq old_rsp(%rip), %rcx
	movq (%rcx), %rsp

	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbp
	popq %rbx
	popfq

	retq

old_rsp:
	.quad 0

/* stack for stage0 */
.align 4
bspstack_start:
	.fill STAGE0_STACK_SIZE, 1, 0xCC
bspstack:

#ifdef STACK_PROTECTOR
.align 8
stack_canary:
	.fill 0x28, 1, 0  /* GCC hardcodes the stack cookie offset as 0x28 on x86-64 */
	.quad 0
#endif
