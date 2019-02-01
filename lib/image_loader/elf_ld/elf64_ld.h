/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _ELF64_LD_H_
#define _ELF64_LD_H_

#include "elf_ld.h"
#include "evmm_desc.h"

/*
 * ELF header.
 */

typedef struct {
	unsigned char	e_ident[EI_NIDENT];     /* File identification. */
	uint16_t	e_type;                 /* File type. */
	uint16_t	e_machine;              /* Machine architecture. */
	uint32_t	e_version;              /* ELF format version. */
	uint64_t	e_entry;                /* Entry point. */
	uint64_t	e_phoff;                /* Program header file offset. */
	uint64_t	e_shoff;                /* Section header file offset. */
	uint32_t	e_flags;                /* Architecture-specific flags. */
	uint16_t	e_ehsize;               /* Size of ELF header in bytes. */
	uint16_t	e_phentsize;            /* Size of program header entry. */
	uint16_t	e_phnum;                /* Number of program header entries. */
	uint16_t	e_shentsize;            /* Size of section header entry. */
	uint16_t	e_shnum;                /* Number of section header entries. */
	uint16_t	e_shstrndx;             /* Section name strings section. */
} elf64_ehdr_t;

/*
 * Section header.
 */

typedef struct {
	uint32_t	sh_name;                /* Section name (index into the
						   section header string table). */
	uint32_t	sh_type;                /* Section type. */
	uint64_t	sh_flags;               /* Section flags. */
	uint64_t	sh_addr;                /* Address in memory image. */
	uint64_t	sh_offset;              /* Offset in file. */
	uint64_t	sh_size;                /* Size in bytes. */
	uint32_t	sh_link;                /* Index of a related section. */
	uint32_t	sh_info;                /* Depends on section type. */
	uint64_t	sh_addralign;           /* Alignment in bytes. */
	uint64_t	sh_entsize;             /* Size of each entry in section. */
} elf64_shdr_t;

/*
 * Program header.
 */

typedef struct {
	uint32_t	p_type;                 /* Entry type. */
	uint32_t	p_flags;                /* Access permission flags. */
	uint64_t	p_offset;               /* File offset of contents. */
	uint64_t	p_vaddr;                /* Virtual address in memory image. */
	uint64_t	p_paddr;                /* Physical address (not used). */
	uint64_t	p_filesz;               /* Size of contents in file. */
	uint64_t	p_memsz;                /* Size of contents in memory. */
	uint64_t	p_align;                /* Alignment in memory and file. */
} elf64_phdr_t;

/*
 * Dynamic structure.  The ".dynamic" section contains an array of them.
 */

typedef struct {
	uint64_t d_tag;                   /* Entry type. */
	union {
		uint64_t	d_val;          /* Integer value. */
		uint64_t	d_ptr;          /* Address value. */
	} d_un;
} elf64_dyn_t;

/*
 * Relocation entries.
 */

/* Relocations that don't need an addend field. */
typedef struct {
	uint64_t	r_offset;               /* Location to be relocated. */
	uint64_t	r_info;                 /* Relocation type and symbol index. */
} elf64_rel_t;

/* Relocations that need an addend field. */
typedef struct {
	uint64_t	r_offset;               /* Location to be relocated. */
	uint64_t	r_info;                 /* Relocation type and symbol index. */
	uint64_t	r_addend;               /* Addend. */
} elf64_rela_t;

/*
 * Symbol table entries.
 */

typedef struct {
	uint32_t	st_name;        /* String table index of name. */
	unsigned char	st_info;        /* Type and binding information. */
	unsigned char	st_other;       /* reserved (not used). */
	uint16_t	st_shndx;       /* Section index of symbol. */
	uint64_t	st_value;       /* Symbol value. */
	uint64_t	st_size;        /* Size of associated object. */
} elf64_sym_t;
boolean_t elf64_get_segment_info(const elf64_ehdr_t *ehdr,
				uint16_t segment_no, elf_segment_info_t *p_info);
boolean_t elf64_load_executable(module_file_info_t *p_info, uint64_t *p_entry);


#endif                          /* _ELF64_LD_H_ */
