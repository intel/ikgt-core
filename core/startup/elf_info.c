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

#include "file_codes.h"
#define MON_DEADLOOP()                  MON_DEADLOOP_LOG(ELF_INFO_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(ELF_INFO_C, __condition)
#include "elf_env.h"
#include "elf_ld.h"
#include "elf32.h"
#include "elf64.h"
#include "elf_info.h"

static int elf32_get_segment_info(elf32_ehdr_t *ehdr,
				  int16_t segment_no,
				  elf_segment_info_t *p_info);
static int elf64_get_segment_info(elf64_ehdr_t *ehdr,
				  int16_t segment_no,
				  elf_segment_info_t *p_info);

/*
 *  FUNCTION  : elf_is_elf
 *  PURPOSE   : Minimal check for ELF image
 *  ARGUMENTS : p_buffer - pointer to the start of the binary
 *  RETURNS   : TRUE if valid
 */
int elf_is_elf(const char *p_buffer)
{
	/* Check the magic numbers */
	return ELFMAG0 == p_buffer[EI_MAG0] &&
	       ELFMAG1 == p_buffer[EI_MAG1] &&
	       ELFMAG2 == p_buffer[EI_MAG2] &&
	       ELFMAG3 == p_buffer[EI_MAG3];
}

/*
 *  FUNCTION  : elf32_header_is_valid
 *  PURPOSE   : Check if it is valid x32-ELF image
 *  ARGUMENTS : p_buffer - pointer to the start of the binary
 *  RETURNS   : TRUE if valid
 */
int elf32_header_is_valid(const void *p_buffer)
{
	elf32_ehdr_t *ehdr = (elf32_ehdr_t *)p_buffer;

	/* Check the magic numbers */
	return ELFCLASS32 == ehdr->e_ident[EI_CLASS] &&
	       ELFMAG0 == ehdr->e_ident[EI_MAG0] &&
	       ELFMAG1 == ehdr->e_ident[EI_MAG1] &&
	       ELFMAG2 == ehdr->e_ident[EI_MAG2] &&
	       ELFMAG3 == ehdr->e_ident[EI_MAG3] &&
	       ELFDATA2LSB == ehdr->e_ident[EI_DATA] &&
	       EV_CURRENT == ehdr->e_ident[EI_VERSION] &&
	       EV_CURRENT == ehdr->e_version &&
	       EM_386 == ehdr->e_machine;
}

/*
 *  FUNCTION  : elf64_header_is_valid
 *  PURPOSE   : Check if it is valid x64-ELF image
 *  ARGUMENTS : p_buffer - pointer to the start of the binary
 *  RETURNS   : TRUE if valid
 */
int elf64_header_is_valid(const void *p_buffer)
{
	elf64_ehdr_t *ehdr = (elf64_ehdr_t *)p_buffer;

	/* Check the magic numbers */
	return ELFCLASS64 == ehdr->e_ident[EI_CLASS] &&
	       ELFMAG0 == ehdr->e_ident[EI_MAG0] &&
	       ELFMAG1 == ehdr->e_ident[EI_MAG1] &&
	       ELFMAG2 == ehdr->e_ident[EI_MAG2] &&
	       ELFMAG3 == ehdr->e_ident[EI_MAG3] &&
	       ELFDATA2LSB == ehdr->e_ident[EI_DATA] &&
	       EV_CURRENT == ehdr->e_version &&
	       EM_X86_64 == ehdr->e_machine;
}

int elf_get_segment_info(const void *p_image, int16_t segment_no,
			 elf_segment_info_t *p_info)
{
	if (TRUE == elf32_header_is_valid(p_image)) {
		return elf32_get_segment_info((elf32_ehdr_t *)p_image,
			segment_no,
			p_info);
	} else if (TRUE == elf64_header_is_valid(p_image)) {
		return elf64_get_segment_info((elf64_ehdr_t *)p_image,
			segment_no,
			p_info);
	} else {
		return FALSE;
	}
}

int elf32_get_segment_info(elf32_ehdr_t *ehdr, int16_t segment_no,
			   elf_segment_info_t *p_info)
{
	if (segment_no < ehdr->e_phnum) {
		uint8_t *phdrtab = (uint8_t *)ehdr + ehdr->e_phoff;
		elf32_phdr_t *phdr = (elf32_phdr_t *)GET_PHDR(ehdr,
			phdrtab,
			segment_no);

		p_info->address = (void *)(address_t)phdr->p_paddr;
		p_info->size = phdr->p_memsz;

		if (PT_LOAD == phdr->p_type) {
			p_info->attribute =
				phdr->p_flags &
				(ELF_ATTR_EXECUTABLE | ELF_ATTR_WRITABLE |
				 ELF_ATTR_READABLE);
		} else {
			p_info->attribute = 0;
		}
		return TRUE;
	}
	return FALSE;
}

int elf64_get_segment_info(elf64_ehdr_t *ehdr, int16_t segment_no,
			   elf_segment_info_t *p_info)
{
	if (segment_no < ehdr->e_phnum) {
		uint8_t *phdrtab = (uint8_t *)ehdr + ehdr->e_phoff;
		elf64_phdr_t *phdr = (elf64_phdr_t *)GET_PHDR(ehdr,
			phdrtab,
			segment_no);

		p_info->address = (void *)(size_t)phdr->p_paddr;
		p_info->size = (uint32_t)phdr->p_memsz;
		if (PT_LOAD == phdr->p_type) {
			p_info->attribute =
				phdr->p_flags &
				(ELF_ATTR_EXECUTABLE | ELF_ATTR_WRITABLE |
				 ELF_ATTR_READABLE);
		} else {
			p_info->attribute = 0;
		}
		return TRUE;
	}
	return FALSE;
}
