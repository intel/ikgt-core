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

.file   "mon_acpi_resume_after_s3_c_main_gnu.s"


.text

#------------------------------------------------------------------------------
# Registers for passing parameters:
#       %rdi, %rsi, %rdx, %rcx, %r8, %r9
# The rest  parameters are in stack
# Preserved regs
#          %rbp,%rbx,%r12,%r15
# System calls via syscall
# Kernel destroys %r11, %rcx
# The number of syscall is in %rax
#------------------------------------------------------------------------------

#void mon_acpi_resume_after_s3_c_main(uint32_t , uint64_t , uint64_t , uint64_t )
.extern mon_acpi_resume_after_s3_c_main

#------------------------------------------------------------------------------
#  void_t
#  mon_acpi_resume_after_s3_c_main_gnu (
#    )
#
#  This function used as entry point to uMON. It assumes that loader is in PE format,
#  so it displace 4 arguments passed in registers RCX, RDX, R8, R9
#  (Microsoft-like arguments passing), to registers RDI, RSI, RDX, RCX,
#   i.e. GNU arguments passing convention
#------------------------------------------------------------------------------

.globl mon_acpi_resume_after_s3_c_main_gnu
mon_acpi_resume_after_s3_c_main_gnu:
        mov %rcx, %rdi
        mov %rdx, %rsi
        mov %r8,  %rdx
        mov %r9,  %rcx
        call mon_acpi_resume_after_s3_c_main
#        jmp .           # should never reach this point
        ret
