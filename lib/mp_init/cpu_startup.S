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
.extern hand_over_entry
.align 8
.text

.global cpu_startup32
/* Jump from realmode
 * Parameter patched in machine code:
 *     edi: cpu_startup_data
 *     esi: cpu_startup64
 */
.code32
cpu_startup32:
	/* setup ds/ss */
	mov $0x18, %ax
	mov %ax, %ds
	mov %ax, %ss

	/* calculate cpu_id and store in edx
	 * If need_wakeup_bsp in setup_sipi_page() is TRUE, the cpu_id starts from 0,
	 * otherwise it starts from 1. */
	mov $1, %edx
	lock xaddl %edx, 20(%edi)

	/* if cpu_id  > MAX_CPU_NUM, stop it;
	 * if cpu_id == MAX_CPU_NUM, hand_over_entry() should print error message. */
	cmpl $MAX_CPU_NUM, %edx
	jg stop

	/* setup gdt/esp */
	lgdt (%edi)

	mov %edi, %eax
	add $40, %eax
	shl $2, %edx
	add %edx, %eax
	mov (%eax), %esp

	/* prepare [CS:EIP] for jumping to 64bit mode */
	xor %eax, %eax
	movw 10(%edi), %ax
	push %eax
	mov %esi, %eax
	push %eax

	/* save cpu_id to rsi */
	shr $2, %edx
	mov %edx, %esi

	/* setup cr3 */
	mov 16(%edi), %ebx
	mov %ebx, %cr3

	/* set CR4.PAE=1 */
	mov %cr4, %eax
	bts $5, %eax
	mov %eax, %cr4

	/* set EFER.LME=1 && EFER.NXE=1 */
	mov $0xC0000080, %ecx
	rdmsr
	bts $8, %eax
	bts $11, %eax
	wrmsr

	/* set CR0.PG=1 */
	mov %cr0, %eax
	bts $31, %eax
	mov %eax, %cr0

	/* jmp to 64bit mode */
	lret

stop:
	jmp .

.global cpu_startup64
.code64
cpu_startup64:
	/* setup 64-bit DS/SS */
	xor %rax, %rax
	movw 12(%edi), %ax
	movw %ax, %ds
	movw 14(%edi), %ax
	movw %ax, %ss

#ifdef STACK_PROTECTOR
	/* configure the fs base for stack protect */
	movl $0xC0000100, %ecx
	mov %edi, %ebp
	add $32, %ebp
	movl (%ebp), %eax
	movl 4(%ebp), %edx
	wrmsr
#endif

	/* take cpu_id as parameter to C function */
	mov %esi, %edi

	/* clear second parameter since no need for legacy startup */
	xor %rsi, %rsi

	/* call into C function */
	call *hand_over_entry(%rip)
