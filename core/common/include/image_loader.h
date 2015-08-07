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

#ifndef _IMAGE_LOADER_H_
#define _IMAGE_LOADER_H_

#include "mon_defs.h"

typedef enum {
	IMAGE_MACHINE_UNKNOWN = 0,
	IMAGE_MACHINE_X86,
	IMAGE_MACHINE_EM64T,
} image_machine_type_t;

typedef enum {
	IMAGE_INFO_OK = 0,
	IMAGE_INFO_WRONG_PARAMS,
	IMAGE_INFO_WRONG_FORMAT,
	IMAGE_INFO_WRONG_MACHINE,
	IMAGE_INFO_NOT_RELOCATABLE,
	IMAGE_INFO_UNRESOLVED_SYMBOLS
} image_info_status_t;

typedef struct {
	image_machine_type_t	machine_type;
	uint32_t		load_size; /* size as required for loading */
} image_info_t;


/*----------------------------------------------------------------------
 *
 * load image into memory
 *
 * Input:
 * void* file_mapped_into_memory - file directly read or mapped in RAM
 * void* image_base_address - load image to this address. Must be alined
 * on 4K.
 * uint32_t allocated_size - buffer size for image
 * uint64_t* p_entry_point_address - address of the uint64_t that will be filled
 * with the address of image entry point if
 * all is ok
 *
 * Output:
 * Return value - FALSE on any error
 *---------------------------------------------------------------------- */
boolean_t
load_image(const void *file_mapped_into_memory,
	   void *image_base_address,
	   uint32_t allocated_size,
	   uint64_t *p_entry_point_address);

/*----------------------------------------------------------------------
 *
 * Get info required for image loading
 *
 * Input:
 * void* file_mapped_into_memory - file directly read or mapped in RAM
 *
 * Output:
 * image_info_t - fills the structure
 *
 * Return value - image_info_status_t
 *---------------------------------------------------------------------- */
image_info_status_t get_image_info(const void *file_mapped_into_memory,
				   uint32_t allocated_size,
				   image_info_t *p_image_info);

#endif     /* _IMAGE_LOADER_H_ */
