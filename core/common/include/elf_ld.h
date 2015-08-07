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

#ifndef _ELF_LD_H_
#define _ELF_LD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "mon_defs.h"
#include "image_access_gen.h"
#include "image_loader.h"

/* Legal values for machine_type below */

#define EM_386       3                  /* Intel 80386 */
#define EM_X86_64   62                  /* AMD x86-64 architecture */

typedef struct {
	boolean_t	copy_section_headers;   /* input */
	boolean_t	copy_symbol_tables;     /* input */
	uint64_t	start_addr;             /* output */
	uint64_t	end_addr;               /* output */
	uint64_t	entry_addr;             /* output */
	uint64_t	sections_addr;          /* output (0 means there are no sections) */
	int64_t		relocation_offset;      /* output */
	int		machine_type;           /* output */
	void		*sh_got;                /* ptr to GOT section header */
	void		*sh_rela_dyn;           /* ptr to Dynamic Relo section header */
	void		*sh_dynsym;             /* ptr to GOT Dynamic Symbol Table section header */
} elf_load_info_t;


#ifdef __cplusplus
}
#endif
#endif    /* _ELF_LD_H_ */
