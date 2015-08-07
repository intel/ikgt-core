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

#ifndef _IMAGE_ACCESS_MEM_H_
#define _IMAGE_ACCESS_MEM_H_

#include "image_access_gen.h"

/*--------------------------Local Types Definitions-------------------------*/
typedef struct {
	gen_image_access_t	gen;    /* inherits to gen_image_access_t */
	char			*image;  /* image located in memory */
	size_t			size;   /* size of image in memory, counted in bytes */
} mem_image_access_t;

gen_image_access_t *mem_image_create(char *image, size_t size);
void   mem_image_close(gen_image_access_t *);
size_t mem_image_read(gen_image_access_t *, void *, size_t, size_t);
size_t mem_image_map_to_mem(gen_image_access_t *, void **, size_t, size_t);
gen_image_access_t *mem_image_create_ex(char *image, size_t size, void *buf);

#endif   /* _IMAGE_ACCESS_MEM_H_ */
