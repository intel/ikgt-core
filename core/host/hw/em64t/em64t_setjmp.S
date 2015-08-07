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
#          em64t_setjmp.S
#
#   Abstract:
#
#*****************************************************************************

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
#  int cdecl
#  globl setjmp()
#  Arguments:   RDI: Buffer to save the context
#  Returns  :   0 - if first time, retcode otherwise
#------------------------------------------------------------------------------
.globl setjmp
setjmp:
	movq %rbx,(%rdi)
	movq %rbp,8(%rdi)
	movq %r12,16(%rdi)
	movq %r13,24(%rdi)
	movq %r14,32(%rdi)
	movq %r15,40(%rdi)
	pop  %rax			# Return address, and adjust the stack
	movq %rsp,48(%rdi)	# Post-return %rsp!
	push %rax			# Make the call/return stack happy
	movq %rax,56(%rdi)  # Return address
	xorl %eax,%eax		# Return value
	ret
# end of setjmp()


#------------------------------------------------------------------------------
#  void cdecl
#  globl longjmp()
#  Arguments:   RDI: Buffer to save the context
#			:   RSI: ret code for hw_setjmp
#  Returns  :   0 - if first time, retcode otherwise
#------------------------------------------------------------------------------
.globl longjmp
longjmp:
	movq %rsi,%rax			# Return value (int)
	movq (%rdi),%rbx
	movq 8(%rdi),%rbp
	movq 16(%rdi),%r12
	movq 24(%rdi),%r13
	movq 32(%rdi),%r14
	movq 40(%rdi),%r15
	movq 48(%rdi),%rsp
	jmp *56(%rdi)
# end of longjmp()


.globl hw_exception_post_handler
hw_exception_post_handler:
	mov	   $1, %rsi		# err code for longjmp
	movq %rsp, %rdi		# address of setjmp_buffer_t
	jmp  longjmp
# end of hw_exception_post_handler()


### EOF ###

