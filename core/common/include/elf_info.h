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

#ifndef _ELF_INFO_H_
#define _ELF_INFO_H_

typedef enum {
	ELF_ATTR_EXECUTABLE = 1,
	ELF_ATTR_WRITABLE = 2,
	ELF_ATTR_READABLE = 4
} elf_attr_type_t;

typedef struct {
	void		*address;
	uint32_t	size;
	elf_attr_type_t attribute;
} elf_segment_info_t;

int elf_is_elf(const char *p_buffer);
int elf32_header_is_valid(const void *p_buffer);
int elf64_header_is_valid(const void *p_buffer);
int elf_get_segment_info(const void *p_image,
			 int16_t segment_no,
			 elf_segment_info_t *info);

#endif  /* _ELF_INFO_H_ */
