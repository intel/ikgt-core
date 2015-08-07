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

#ifndef _PARSE_IMAGE_H_
#define _PARSE_IMAGE_H_

#include "mon_defs.h"
#include "array_iterators.h"

/**************************************************************************
 *
 * Get info about the MON memory image itself
 *
 ************************************************************************** */
typedef struct {
	const char	*name;
	const char	*start;
	uint32_t	size;
	boolean_t	readable;
	boolean_t	writable;
	boolean_t	executable;
} exec_image_section_info_t;

typedef generic_array_iterator_t exec_image_section_iterator_t;

/*-------------------------------------------------------------------------
 *
 * Iterate through section info while exec_image_section_info_t* != NULL
 *
 *------------------------------------------------------------------------- */
const exec_image_section_info_t
*exec_image_section_first(const void *image,
			  uint32_t image_size,
			  exec_image_section_iterator_t *ctx);

const exec_image_section_info_t
*exec_image_section_next(exec_image_section_iterator_t *ctx);

/*-------------------------------------------------------------------------
 *
 * initialize
 *
 *------------------------------------------------------------------------- */
void exec_image_initialize(void);

#endif   /* _PARSE_IMAGE_H_ */
