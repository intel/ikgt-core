/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
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

#ifndef _ELF_DEFS_H_
#define _ELF_DEFS_H_

typedef unsigned int u_int32_t;
#include "elf_common.h"

/* currently unsupported flags...  this is a kind of version number */
#define MULTIBOOT_UNSUPPORTED       0x0000FFF8
/* Magic value identifying the multiboot_header */
#define MULTIBOOT_MAGIC         0x1BADB002
/* Must pass video information to OS */
#define MULTIBOOT_VIDEO_MODE        0x00000004
/* This flag indicates the use of the address fields in the header */
#define MULTIBOOT_AOUT_KLUDGE       0x00010000

/* Macros to facilitate locations of program segment headers and section
 * headers */
#define GET_PHDR(__ehdr, __phdrtab, __i) \
	((__phdrtab) + (__i) * (__ehdr)->e_phentsize)
#define GET_SHDR(__ehdr, __shdrtab, __i) \
	((__shdrtab) + (__i) * (__ehdr)->e_shentsize)

#define GOT_SECTION_NAME        ".got"
#define DYNRELA_SECTION_NAME    ".rela.dyn"
#define DYNSYM_SECTION_NAME     ".dynsym"

#endif
