/*******************************************************************************
* Copyright (c) 2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

/* in stage0, there is only the .text section.
 * we need function printf(see lib/print/print.c) to print message,
 * but strings are put into the .rodata and there are some
 * static global variables in print.c, must merge .rodata
 * and .bss into the .text.
 * That is, we don't need merge read-only section into text segment
 * and we don't need other sections. */

OUTPUT_FORMAT("elf64-x86-64","elf64-x86-64","elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

ENTRY(start);
SECTIONS
{
  .text           :
  {
    *(.text.unlikely .text.*_unlikely .text.unlikely.*)
    *(.text.exit .text.exit.*)
    *(.text.startup .text.startup.*)
    *(.text.hot .text.hot.*)
    *(.text .stub .text.* .gnu.linkonce.t.*)
    /* .gnu.warning sections are handled specially by elf32.em.  */
    *(.gnu.warning)

    /* merge .rodata into .text */
    *(.rodata .rodata.* .gnu.linkonce.r.*)

    /* merge .bss into .text */
    *(.bss .bss.* .gnu.linkonce.b.*)
  } =0x90909090
}
