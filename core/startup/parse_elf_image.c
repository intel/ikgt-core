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

/*
 *
 * Implements both PE32 and PE32+ image parsing
 *
 */

#include "file_codes.h"
#define MON_DEADLOOP()                  MON_DEADLOOP_LOG(PARSE_ELF_IMAGE_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(PARSE_ELF_IMAGE_C, __condition)
#include "parse_image.h"
#include "elf_info.h"
#include "lock.h"
#include "heap.h"

static mon_lock_t g_section_lock;
static boolean_t g_is_locked;
static int16_t g_segment_number;
static const void *g_image;

static exec_image_section_info_t *exec_image_section_next_unprotected(const void
								      *image);

void exec_image_initialize(void)
{
	lock_initialize(&g_section_lock);
	g_is_locked = FALSE;
}

const exec_image_section_info_t *exec_image_section_first(const void *image,
							  uint32_t image_size UNUSED,
							  exec_image_section_iterator_t *ctx UNUSED)
{
	exec_image_section_info_t *info;

	lock_acquire(&g_section_lock);
	g_is_locked = TRUE;
	g_image = image;

	g_segment_number = 0;

	info = exec_image_section_next_unprotected(image);

	if (NULL == info) {
		g_is_locked = FALSE;
		lock_release(&g_section_lock);
	}

	return info;
}

const exec_image_section_info_t
*exec_image_section_next(exec_image_section_iterator_t *ctx UNUSED)
{
	exec_image_section_info_t *info;

	MON_ASSERT(g_is_locked == TRUE);
	if (FALSE == g_is_locked) {
		return NULL;
	}

	info = exec_image_section_next_unprotected(g_image);

	if (NULL == info) {
		g_is_locked = FALSE;
		lock_release(&g_section_lock);
	}

	return info;
}

exec_image_section_info_t *exec_image_section_next_unprotected(const void *image)
{
	static exec_image_section_info_t image_section_info;
	exec_image_section_info_t *info = NULL;
	elf_segment_info_t segment_info;
	boolean_t segment_present;

	for (segment_present =
		     elf_get_segment_info(image, g_segment_number,
			     &segment_info);
	     TRUE == segment_present;
	     segment_present =
		     elf_get_segment_info(image, g_segment_number,
			     &segment_info)) {
		g_segment_number++;

		if (0 !=
		    ((ELF_ATTR_EXECUTABLE | ELF_ATTR_WRITABLE |
		      ELF_ATTR_READABLE) &
		     segment_info.attribute)) {
			/* found loaded section */
			info = &image_section_info;
			info->executable =
				0 !=
				(segment_info.attribute & ELF_ATTR_EXECUTABLE);
			info->writable = 0 !=
					 (segment_info.attribute &
					  ELF_ATTR_WRITABLE);
			info->readable = 0 !=
					 (segment_info.attribute &
					  ELF_ATTR_READABLE);
			info->name = "???";
			info->start =
				(char *)ALIGN_BACKWARD(segment_info.address,
					PAGE_4KB_SIZE);
			info->size =
				segment_info.size +
				((char *)segment_info.address -
				 info->start);
			MON_LOG(mask_anonymous, level_trace,
				"section=%d  flags=%P  start=%P  size=%P\n",
				g_segment_number - 1, segment_info.attribute,
				segment_info.address, segment_info.size);
			break;
		}
	}

	return info;
}
