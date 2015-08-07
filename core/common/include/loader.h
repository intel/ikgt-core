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


/* The following definitions and macros will be used to perform alignment */
#define MON_PAGE_TABLE_SHIFT    12
#define MON_PAGE_4KB_SIZE       (1 << MON_PAGE_TABLE_SHIFT)
#define MON_PAGE_4KB_MASK       (MON_PAGE_4KB_SIZE - 1)
#define MON_PAGE_ALIGN_4K(x)    ((((x) & ~MON_PAGE_4KB_MASK) == (x)) ?   \
				 (x) : ((x) & \
					~MON_PAGE_4KB_MASK) + MON_PAGE_4KB_SIZE)

#define UNEXPECTED_ERROR        -1

/*----------------------------------------------------------------------
 * memory_ptr_t
 *
 * Dummy tyoedef: supports for easy change of memory addressing in
 * image_mem_info_t struct.
 *
 *---------------------------------------------------------------------- */
typedef address_t memory_ptr_t;

/*----------------------------------------------------------------------
 * struct image_mem_info_t
 *
 * This struct contains information about the images loaded by loader
 * for use by the MON
 *
 *---------------------------------------------------------------------- */
typedef struct {
	memory_ptr_t	p_image_base_address;   /* image address (loaded by loader) */
	uint32_t	image_byte_size;        /* size of image */
} image_mem_info_t;

/*----------------------------------------------------------------------
 * defines, for accessing the image_mem_info_t structure
 *---------------------------------------------------------------------- */
#define image_index_mon_lp          0
#define image_index_mon_h           1
#define image_index_mon_load_thunk  2
#define image_index_mon_exe         3
#define image_index_bios_e820       4
#define image_index_mon_env         5
#define image_index_cos_env         6
#define image_index_sos1_env        7

/*----------------------------------------------------------------------
 * struct e820_loader_info_t
 *
 * This struct contains all the information that MON-LP requires for
 * its own operation, or is required to hand over to MON-H
 *
 *---------------------------------------------------------------------- */
typedef struct {
	int15_e820_memory_map_entry_ext_t	*p_e820_data;           /* pointer to E820 data */
	uint32_t				e820_entries;           /* entries count */
	uint32_t				e820_entry_size;        /* size as indicated by BIOS */
	uint32_t				e820_buffer_size;       /* limit of region - do not cross! */
} e820_loader_info_t;

typedef struct {
	e820_loader_info_t	e820_info;              /* e820 info from loader */
	image_mem_info_t	*p_loaded_images;       /* array of images loaded by loader */
	uint32_t		loaded_images_count;    /* # of images */
	uint32_t		mon_footprint;          /* MON footprint size (MB) */
} loader_params_t;

typedef void_t (*func_monh_entry_t) (loader_params_t *p_loader_params);
