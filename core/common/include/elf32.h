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

#ifndef _SYS_ELF32_H_
#define _SYS_ELF32_H_ 1

#include "elf_defs.h"

/*
 * ELF definitions common to all 32-bit architectures.
 */
/* change the definition below to make it work with Intel xmon */
typedef uint32_t elf32_addr_t;
typedef uint16_t elf32_half_t;
typedef uint32_t elf32_off_t;
typedef int32_t elf32_sword_t;
typedef uint32_t elf32_word_t;
typedef uint64_t elf32_lword_t;

typedef elf32_word_t elf32_hashelt_t;

/* Non-standard class-dependent datatype used for abstraction. */
typedef elf32_word_t elf32_size_t;
typedef elf32_sword_t elf32_ssize_t;

/*
 * ELF header.
 */

typedef struct {
	unsigned char	e_ident[EI_NIDENT];     /* File identification. */
	elf32_half_t	e_type;                 /* File type. */
	elf32_half_t	e_machine;              /* Machine architecture. */
	elf32_word_t	e_version;              /* ELF format version. */
	elf32_addr_t	e_entry;                /* Entry point. */
	elf32_off_t	e_phoff;                /* Program header file offset. */
	elf32_off_t	e_shoff;                /* Section header file offset. */
	elf32_word_t	e_flags;                /* Architecture-specific flags. */
	elf32_half_t	e_ehsize;               /* Size of ELF header in bytes. */
	elf32_half_t	e_phentsize;            /* Size of program header entry. */
	elf32_half_t	e_phnum;                /* Number of program header entries. */
	elf32_half_t	e_shentsize;            /* Size of section header entry. */
	elf32_half_t	e_shnum;                /* Number of section header entries. */
	elf32_half_t	e_shstrndx;             /* Section name strings section. */
} elf32_ehdr_t;

/*
 * Section header.
 */

typedef struct {
	elf32_word_t	sh_name;                /* Section name (index into the
						 * section header string table). */
	elf32_word_t	sh_type;                /* Section type. */
	elf32_word_t	sh_flags;               /* Section flags. */
	elf32_addr_t	sh_addr;                /* Address in memory image. */
	elf32_off_t	sh_offset;              /* Offset in file. */
	elf32_word_t	sh_size;                /* Size in bytes. */
	elf32_word_t	sh_link;                /* Index of a related section. */
	elf32_word_t	sh_info;                /* Depends on section type. */
	elf32_word_t	sh_addralign;           /* Alignment in bytes. */
	elf32_word_t	sh_entsize;             /* Size of each entry in section. */
} elf32_shdr_t;

/*
 * Program header.
 */

typedef struct {
	elf32_word_t	p_type;                 /* Entry type. */
	elf32_off_t	p_offset;               /* File offset of contents. */
	elf32_addr_t	p_vaddr;                /* Virtual address in memory image. */
	elf32_addr_t	p_paddr;                /* Physical address (not used). */
	elf32_word_t	p_filesz;               /* Size of contents in file. */
	elf32_word_t	p_memsz;                /* Size of contents in memory. */
	elf32_word_t	p_flags;                /* Access permission flags. */
	elf32_word_t	p_align;                /* Alignment in memory and file. */
} elf32_phdr_t;

/*
 * Dynamic structure.  The ".dynamic" section contains an array of them.
 */

typedef struct {
	elf32_sword_t d_tag;                    /* Entry type. */
	union {
		elf32_word_t	d_val;          /* Integer value. */
		elf32_addr_t	d_ptr;          /* Address value. */
	} d_un;
} elf32_dyn_t;

/*
 * Relocation entries.
 */

/* Relocations that don't need an addend field. */
typedef struct {
	elf32_addr_t	r_offset;               /* Location to be relocated. */
	elf32_word_t	r_info;                 /* Relocation type and symbol index. */
} elf32_rel_t;

/* Relocations that need an addend field. */
typedef struct {
	elf32_addr_t	r_offset;               /* Location to be relocated. */
	elf32_word_t	r_info;                 /* Relocation type and symbol index. */
	elf32_sword_t	r_addend;               /* Addend. */
} elf32_rela_t;

/* Macros for accessing the fields of r_info. */
#define ELF32_R_SYM(info)       ((info) >> 8)
#define ELF32_R_TYPE(info)      ((unsigned char)(info))

/* Macro for constructing r_info from field values. */
#define ELF32_R_INFO(sym, type) (((sym) << 8) + (unsigned char)(type))

/*
 *      Note entry header
 */
typedef elf_note_t elf32_nhdr_t;

/*
 *      Move entry
 */
typedef struct {
	elf32_lword_t	m_value;                /* symbol value */
	elf32_word_t	m_info;                 /* size + index */
	elf32_word_t	m_poffset;              /* symbol offset */
	elf32_half_t	m_repeat;               /* repeat count */
	elf32_half_t	m_stride;               /* stride info */
} elf32_move_t;

/*
 *      The macros compose and decompose values for Move.r_info
 *
 *      sym = ELF32_M_SYM(M.m_info)
 *      size = ELF32_M_SIZE(M.m_info)
 *      M.m_info = ELF32_M_INFO(sym, size)
 */
#define ELF32_M_SYM(info)       ((info) >> 8)
#define ELF32_M_SIZE(info)      ((unsigned char)(info))
#define ELF32_M_INFO(sym, size) (((sym) << 8) + (unsigned char)(size))

/*
 *      Hardware/Software capabilities entry
 */
typedef struct {
	elf32_word_t c_tag;               /* how to interpret value */
	union {
		elf32_word_t	c_val;
		elf32_addr_t	c_ptr;
	} c_un;
} elf32_cap_t;

/*
 * Symbol table entries.
 */

typedef struct {
	elf32_word_t	st_name;                /* String table index of name. */
	elf32_addr_t	st_value;               /* Symbol value. */
	elf32_word_t	st_size;                /* Size of associated object. */
	unsigned char	st_info;                /* Type and binding information. */
	unsigned char	st_other;               /* reserved (not used). */
	elf32_half_t	st_shndx;               /* Section index of symbol. */
} elf32_sym_t;

/* Macros for accessing the fields of st_info. */
#define ELF32_ST_BIND(info)             ((info) >> 4)
#define ELF32_ST_TYPE(info)             ((info) & 0xf)

/* Macro for constructing st_info from field values. */
#define ELF32_ST_INFO(bind, type)       (((bind) << 4) + ((type) & 0xf))

/* Macro for accessing the fields of st_other. */
#define ELF32_ST_VISIBILITY(oth)        ((oth) & 0x3)

/* Structures used by Sun & GNU symbol versioning. */
typedef struct {
	elf32_half_t	vd_version;
	elf32_half_t	vd_flags;
	elf32_half_t	vd_ndx;
	elf32_half_t	vd_cnt;
	elf32_word_t	vd_hash;
	elf32_word_t	vd_aux;
	elf32_word_t	vd_next;
} elf32_verdef_t;

typedef struct {
	elf32_word_t	vda_name;
	elf32_word_t	vda_next;
} elf32_verdaux_t;

typedef struct {
	elf32_half_t	vn_version;
	elf32_half_t	vn_cnt;
	elf32_word_t	vn_file;
	elf32_word_t	vn_aux;
	elf32_word_t	vn_next;
} elf32_verneed_t;

typedef struct {
	elf32_word_t	vna_hash;
	elf32_half_t	vna_flags;
	elf32_half_t	vna_other;
	elf32_word_t	vna_name;
	elf32_word_t	vna_next;
} elf32_vernaux_t;

typedef elf32_half_t elf32_versym_t;

typedef struct {
	elf32_half_t	si_boundto;             /* direct bindings - symbol bound to */
	elf32_half_t	si_flags;               /* per symbol flags */
} elf32_sysinfo_t;

#endif /* !_SYS_ELF32_H_ */
