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

#include "elf32_ld.h"
#include "elf64_ld.h"
#include "elf_ld.h"
#include "evmm_desc.h"

#include "lib/image_loader.h"
#include "lib/print.h"
#include "lib/util.h"

#define local_print(...)
//#define local_print(...) printf(__VA_ARGS__)

static boolean_t
elf_get_segment_info(const void *p_image,
		     uint16_t segment_no, elf_segment_info_t *p_info)
{
	if (!elf_header_is_valid((const elf64_ehdr_t *)p_image))
		return FALSE;
	if (is_elf32((const elf32_ehdr_t *)p_image)) {
		return elf32_get_segment_info((const elf32_ehdr_t *)p_image, segment_no,
			p_info);
	} else if (is_elf64((const elf64_ehdr_t *)p_image)) {
		return elf64_get_segment_info((const elf64_ehdr_t *)p_image, segment_no,
			p_info);
	} else {
		return FALSE;
	}
}

boolean_t get_image_section(void *image_base, uint16_t index, image_section_info_t *info)
{
	elf_segment_info_t segment_info;

	if (!elf_get_segment_info(image_base, index, &segment_info))
		return FALSE;

	/* the segment type is not PT_LOAD, SKIPED */
	if (0 == segment_info.attribute) {
		info->valid = FALSE;
		return TRUE;
	}

	/* found loaded segment */
	info->valid = TRUE;
	info->executable = 0 !=
		(segment_info.attribute & ELF_ATTR_EXECUTABLE);
	info->writable = 0 !=
		(segment_info.attribute &
		 ELF_ATTR_WRITABLE);
	info->readable = 0 !=
		(segment_info.attribute &
		 ELF_ATTR_READABLE);
	info->start = (void *)ALIGN_B(segment_info.address, PAGE_4K_SIZE);
	info->size = segment_info.size +
		(uint32_t)(segment_info.address - info->start);
	local_print(
			"section=%d  flags=0x%llX  start=0x%llX  size=0x%llX\n",
			info->index - 1, segment_info.attribute,
			segment_info.address, segment_info.size);

	return TRUE;
}

void *image_offset(module_file_info_t *file_info,
				uint64_t src_offset, uint64_t bytes_to_read)
{
	if ((src_offset + bytes_to_read) > file_info->loadtime_size) {
		return NULL; /* read no more than size */
	}
	if ((src_offset + bytes_to_read) <= src_offset) {
		return NULL; /* overflow or bytes_to_read == 0 */
	}

	return (void *)(file_info->loadtime_addr+ src_offset);
}

boolean_t image_copy(void *dest, module_file_info_t *file_info,
				uint64_t src_offset, uint64_t bytes_to_copy)
{
	void *src;
	src = image_offset(file_info, src_offset, bytes_to_copy);
	if (!src) {
		return FALSE;
	}
	if (((uint64_t)dest < file_info->runtime_addr) ||
		(((uint64_t)dest + bytes_to_copy) >
		 (file_info->runtime_addr + file_info->runtime_image_size))) {
		return FALSE;
	}
	memcpy(dest, src, bytes_to_copy);
	return TRUE;
}

/*------------------------- Exported Interface --------------------------*/

/*----------------------------------------------------------------------
 *
 * relocate image in memory
 *
 * Input:
 * module_file_info_t* file_info
 * uint64_t* p_entry - address of the uint64_t that will be filled
 * with the address of image entry point if
 * all is ok
 *
 * Output:
 * Return value - FALSE on any error
 *---------------------------------------------------------------------- */
boolean_t relocate_elf_image(module_file_info_t *file_info, uint64_t *p_entry)
{
	uint8_t *p_buffer;

	p_buffer = (uint8_t *)image_offset(file_info, 0,
			sizeof(elf64_ehdr_t));
	if (!p_buffer){
		local_print("failed to read file's header\n");
		return FALSE;
	}
	if (!elf_header_is_valid((elf64_ehdr_t *)p_buffer)) {
		local_print("not an elf binary\n");
		return FALSE;
	}

	if (is_elf64((elf64_ehdr_t *)p_buffer)) {
		return elf64_load_executable(file_info, p_entry);
	} else if (is_elf32((elf32_ehdr_t *)p_buffer)) {
		return elf32_load_executable(file_info, p_entry);
	} else {
		local_print("not an elf32 or elf64 binary\n");
		return FALSE;
	}
}
