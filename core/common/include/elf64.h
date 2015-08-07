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

#ifndef _SYS_ELF64_H_
#define _SYS_ELF64_H_ 1

#include "elf_defs.h"

/*
 * ELF definitions common to all 64-bit architectures.
 */
/* change the definition below to make it work with Intel xmon */
typedef uint64_t elf64_addr_t;
typedef uint16_t elf64_half_t;
typedef uint64_t elf64_off_t;
typedef int32_t elf64_sword_t;
typedef int64_t elf64_sxword_t;
typedef uint32_t elf64_word_t;
typedef uint64_t elf64_lword_t;
typedef uint64_t elf64_xword_t;


/*
 * Types of dynamic symbol hash table bucket and chain elements.
 *
 * This is inconsistent among 64 bit architectures, so a machine dependent
 * typedef is required.
 */

typedef elf64_word_t elf64_hashelt_t;

/* Non-standard class-dependent datatype used for abstraction. */
typedef elf64_xword_t elf64_size_t;
typedef elf64_sxword_t elf64_ssize_t;

/*
 * ELF header.
 */

typedef struct {
	unsigned char	e_ident[EI_NIDENT];     /* File identification. */
	elf64_half_t	e_type;                 /* File type. */
	elf64_half_t	e_machine;              /* Machine architecture. */
	elf64_word_t	e_version;              /* ELF format version. */
	elf64_addr_t	e_entry;                /* Entry point. */
	elf64_off_t	e_phoff;                /* Program header file offset. */
	elf64_off_t	e_shoff;                /* Section header file offset. */
	elf64_word_t	e_flags;                /* Architecture-specific flags. */
	elf64_half_t	e_ehsize;               /* Size of ELF header in bytes. */
	elf64_half_t	e_phentsize;            /* Size of program header entry. */
	elf64_half_t	e_phnum;                /* Number of program header entries. */
	elf64_half_t	e_shentsize;            /* Size of section header entry. */
	elf64_half_t	e_shnum;                /* Number of section header entries. */
	elf64_half_t	e_shstrndx;             /* Section name strings section. */
} elf64_ehdr_t;

/*
 * Section header.
 */

typedef struct {
	elf64_word_t	sh_name;                /* Section name (index into the
						   section header string table). */
	elf64_word_t	sh_type;                /* Section type. */
	elf64_xword_t	sh_flags;               /* Section flags. */
	elf64_addr_t	sh_addr;                /* Address in memory image. */
	elf64_off_t	sh_offset;              /* Offset in file. */
	elf64_xword_t	sh_size;                /* Size in bytes. */
	elf64_word_t	sh_link;                /* Index of a related section. */
	elf64_word_t	sh_info;                /* Depends on section type. */
	elf64_xword_t	sh_addralign;           /* Alignment in bytes. */
	elf64_xword_t	sh_entsize;             /* Size of each entry in section. */
} elf64_shdr_t;

/*
 * Program header.
 */

typedef struct {
	elf64_word_t	p_type;                 /* Entry type. */
	elf64_word_t	p_flags;                /* Access permission flags. */
	elf64_off_t	p_offset;               /* File offset of contents. */
	elf64_addr_t	p_vaddr;                /* Virtual address in memory image. */
	elf64_addr_t	p_paddr;                /* Physical address (not used). */
	elf64_xword_t	p_filesz;               /* Size of contents in file. */
	elf64_xword_t	p_memsz;                /* Size of contents in memory. */
	elf64_xword_t	p_align;                /* Alignment in memory and file. */
} elf64_phdr_t;

/*
 * Dynamic structure.  The ".dynamic" section contains an array of them.
 */

typedef struct {
	elf64_sxword_t d_tag;                   /* Entry type. */
	union {
		elf64_xword_t	d_val;          /* Integer value. */
		elf64_addr_t	d_ptr;          /* Address value. */
	} d_un;
} elf64_dyn_t;

/*
 * Relocation entries.
 */

/* Relocations that don't need an addend field. */
typedef struct {
	elf64_addr_t	r_offset;               /* Location to be relocated. */
	elf64_xword_t	r_info;                 /* Relocation type and symbol index. */
} elf64_rel_t;

/* Relocations that need an addend field. */
typedef struct {
	elf64_addr_t	r_offset;               /* Location to be relocated. */
	elf64_xword_t	r_info;                 /* Relocation type and symbol index. */
	elf64_sxword_t	r_addend;               /* Addend. */
} elf64_rela_t;

/* Macros for accessing the fields of r_info. */
#define ELF64_R_SYM(info)       ((info) >> 32)
#define ELF64_R_TYPE(info)      ((info) & 0xffffffffL)

/* Macro for constructing r_info from field values. */
#define ELF64_R_INFO(sym, type) (((sym) << 32) + ((type) & 0xffffffffL))

#define ELF64_R_TYPE_DATA(info) (((elf64_xword_t)(info) << 32) >> 40)
#define ELF64_R_TYPE_ID(info)   (((elf64_xword_t)(info) << 56) >> 56)
#define ELF64_R_TYPE_INFO(data, type)   \
	(((elf64_xword_t)(data) << 8) + (elf64_xword_t)(type))

/*
 *      Note entry header
 */
typedef elf_note_t elf64_nhdr_t;

/*
 *      Move entry
 */
typedef struct {
	elf64_lword_t	m_value;                /* symbol value */
	elf64_xword_t	m_info;                 /* size + index */
	elf64_xword_t	m_poffset;              /* symbol offset */
	elf64_half_t	m_repeat;               /* repeat count */
	elf64_half_t	m_stride;               /* stride info */
} elf64_move_t;

#define ELF64_M_SYM(info)       ((info) >> 8)
#define ELF64_M_SIZE(info)      ((unsigned char)(info))
#define ELF64_M_INFO(sym, size) (((sym) << 8) + (unsigned char)(size))

/*
 *      Hardware/Software capabilities entry
 */
typedef struct {
	elf64_xword_t c_tag;              /* how to interpret value */
	union {
		elf64_xword_t	c_val;
		elf64_addr_t	c_ptr;
	} c_un;
} elf64_cap_t;

/*
 * Symbol table entries.
 */

typedef struct {
	elf64_word_t	st_name;        /* String table index of name. */
	unsigned char	st_info;        /* Type and binding information. */
	unsigned char	st_other;       /* reserved (not used). */
	elf64_half_t	st_shndx;       /* Section index of symbol. */
	elf64_addr_t	st_value;       /* Symbol value. */
	elf64_xword_t	st_size;        /* Size of associated object. */
} elf64_sym_t;

/* Macros for accessing the fields of st_info. */
#define ELF64_ST_BIND(info)             ((info) >> 4)
#define ELF64_ST_TYPE(info)             ((info) & 0xf)

/* Macro for constructing st_info from field values. */
#define ELF64_ST_INFO(bind, type)       (((bind) << 4) + ((type) & 0xf))

/* Macro for accessing the fields of st_other. */
#define ELF64_ST_VISIBILITY(oth)        ((oth) & 0x3)

/* Structures used by Sun & GNU-style symbol versioning. */
typedef struct {
	elf64_half_t	vd_version;
	elf64_half_t	vd_flags;
	elf64_half_t	vd_ndx;
	elf64_half_t	vd_cnt;
	elf64_word_t	vd_hash;
	elf64_word_t	vd_aux;
	elf64_word_t	vd_next;
} elf64_verdef_t;

typedef struct {
	elf64_word_t	vda_name;
	elf64_word_t	vda_next;
} elf64_verdaux_t;

typedef struct {
	elf64_half_t	vn_version;
	elf64_half_t	vn_cnt;
	elf64_word_t	vn_file;
	elf64_word_t	vn_aux;
	elf64_word_t	vn_next;
} elf64_verneed_t;

typedef struct {
	elf64_word_t	vna_hash;
	elf64_half_t	vna_flags;
	elf64_half_t	vna_other;
	elf64_word_t	vna_name;
	elf64_word_t	vna_next;
} elf64_vernaux_t;

typedef elf64_half_t elf64_versym_t;

typedef struct {
	elf64_half_t	si_boundto;             /* direct bindings - symbol bound to */
	elf64_half_t	si_flags;               /* per symbol flags */
} elf64_sysinfo_t;

#endif /* !_SYS_ELF64_H_ */
