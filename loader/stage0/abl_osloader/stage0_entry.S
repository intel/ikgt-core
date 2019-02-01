/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

.file   "stage0_entry.s"
#include "stage0_asm.h"
#include "file_pack_asm.h"

.text

.extern stage0_main

.globl start
.org 0 #make below code in beginning of final binary.
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
	.long  start_x32 - start
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

/* Build 0-4G page table */
.macro build_early_page_table
	/* fill PML4E(1 entry, cover 512G) */
	leal pml4(%esi), %ecx
	leal pdpt + 0x7(%esi), %eax
	movl %eax, (%ecx)

	/* detect whether CPU support 1G leaf */
	movl $0x80000001, %eax
	cpuid
	bts $26, %edx
	jnc leaf_2M

leaf_1G:
	leal pdpt(%esi), %ecx
	/* fill PDPTE(4 entries, 1G Page per entry) */
	movl $0x00000083, 0x00(%ecx)
	movl $0x40000083, 0x08(%ecx)
	movl $0x80000083, 0x10(%ecx)
	movl $0xC0000083, 0x18(%ecx)
	jmp 2f

leaf_2M:
	/* fill PDPTE(4 entries, 1 PD per entry) */
	leal pdpt(%esi), %ecx
	leal pd + 0x7(%esi), %eax
	movl $4, %edx
1:
	movl %eax, (%ecx)
	addl $0x1000, %eax
	addl $8, %ecx
	decl %edx
	jnz 1b

	/* fill PDE(512*4 entries, 2MB Page per entry) */
	leal pd(%esi), %ecx
	movl $0x00000083, %eax
	movl $2048, %edx
1:
	movl %eax, (%ecx)
	addl $0x200000, %eax
	addl $8, %ecx
	decl %edx
	jnz 1b

2:
.endm

/* 32bit entry point */
.code32
start_x32:
	/* save esp to edi for stage0_main's first parameter */
	movl 0x4(%esp), %edi

	/* save the orinal env */
	pushal
	pushfl
	pushw %ds

	cli
	/* esi will used to caculate the relative address of flags */
	call 1f
1:  popl %esi
	subl $1b, %esi

	/* save old 32bit gdtr in memory */
	lea  old_gdtr(%esi), %eax
	sgdt (%eax)

	lea old_cs(%esi), %eax
	movw %cs, (%eax)

	/* prepare for entering 64 bit mode */
	/* load new GDT with the 64bit segments using 32bit descriptor */
	leal gdt_x64(%esi), %eax
	movl %eax, gdtr_x64+2(%esi)
	lgdt gdtr_x64(%esi)

	movw $__BOOT_DS, %ax
	movw %ax, %ds
	movw %ax, %ss
	/* es,fs,gs are not set because they will not be used in loader. */
	/* idtr is not set because there's no exception handling in loader.
	 * also the I flag is cleared to make sure there's no interrupt in loader */
	/* tr is not set because there's no privilege change in loader */

	/* enable PAE mode, CR4.PAE */
	movl %cr4, %eax
	btsl $5, %eax
	movl %eax, %cr4

	/* Build early 0-4G pagetable */
	build_early_page_table

	/* Enable the boot page tables */
	leal pml4(%esi), %ecx
	movl %ecx, %cr3

	/* enable Long mode in EFER.LME */
	movl  $0xC0000080, %ecx
	rdmsr
	btsl  $8, %eax
	wrmsr

	/* setup CS:EIP for 64bit mode */
	pushl $__BOOT_CS
	leal start_x64(%esi), %eax
	pushl %eax

	/* set CR0.PG */
	movl %cr0, %eax
	btsl $31, %eax
	movl %eax, %cr0

	/* jump from 32bit compat mode into 64bit mode. */
	lret

.code32
compat_mode:
	/* disable IA32e paging */
	mov %cr0, %eax
	btc $31, %eax
	mov %eax, %cr0

	/* restore cr4 */
	mov %cr4, %eax
	btc $5, %eax
	mov %eax, %cr4


	/* clear MSR bit (8): EFER.LME */
	mov  $0xC0000080, %ecx
	rdmsr
	btc  $8,%eax
	wrmsr

	/* resume the saved register */
	popw %ds
	popfl
	popal

	xorl %eax, %eax

	/* return back to OSloader */
	ret

/*
 * 64bit entry point, used by evmm boot header
 *  Scratch Registers   - rdi, rsi, rdx, rcx, r8, r9, r10, r11
 */
.code64
start_x64:
	pushfq
	pushq %rbp
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

	/* save old stack */
	movq %rsp, old_rsp(%rip)

	/* reset rflags */
	pushq $0
	popfq

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
	movl %edi, %edi			/* rdi saved the address of init register from ABL,
							 * here mov 32 bit value to 64 bit register will clear high 32 bit.
							 */
	leaq start(%rip), %rsi	/* rsi saved the load base of stage0.S */
	movq %rsp, %rdx			/* rdx saved the new rsp */

	call stage0_main

	/* restore old rsp */
	movq old_rsp(%rip), %rsp

	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %rbp
	popfq

	/* load the orignal 32bit gdt */
	lea  old_gdtr(%rip), %rax
	lgdt (%rax)

	/* restore original 32bit CS */
	lea old_cs(%rip), %rax
	xor %rbx, %rbx
	movw (%rax), %bx
	pushq %rbx

	lea compat_mode(%rip), %rax
	pushq %rax

	/* jump from 64bit mode to compat mode */
	lretq

/* stack for stage0 */
.align 4
bspstack_start:
	.fill STAGE0_STACK_SIZE, 1, 0xCC
bspstack:

.balign 8
gdt_x64:
	.quad 	0x0000000000000000	/* NULL descriptor */
	.quad 	0x00af9b000000ffff	/* 0x08 __BOOT_CS */
	.quad 	0x00cf93000000ffff	/* 0x10 __BOOT_DS */
gdt_x64_end:

gdtr_x64:
	.word	gdt_x64_end - gdt_x64 - 1
	.long	0
gdtr_x64_end:

old_rsp:
	.quad 0

old_gdtr:
	.word 0
	.quad 0

old_cs:
	.word 0

/*
 * Space for page tables
 */
.balign 4096
pml4:
	.fill 4096, 1, 0
pdpt:
	.fill 4096, 1, 0
pd:
	.fill 4*4096, 1, 0

#ifdef STACK_PROTECTOR
.align 8
stack_canary:
	.fill 0x28, 1, 0  /* GCC hardcodes the stack cookie offset as 0x28 on x86-64 */
	.quad 0
#endif
