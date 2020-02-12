/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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
    *(.bss .bss.* .gnu.linkonce.b.* COMMON)

    /* stage0 runtime section must be put last */
    *(.stage0_runtime)
  } =0x90909090

  . = .;

  /DISCARD/ :
  {
    /*
     * The .eh_frame and .eh_framehdr sections contain the exception frames.
     * There is no need to link such sections in our entry. Detail reference:
     * http://refspecs.linux-foundation.org/LSB_4.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
     */
    *(.eh_frame)
    *(.eh_framehdr)
  }
}
