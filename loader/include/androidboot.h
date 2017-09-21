/****************************************************************************
* Copyright (c) 2015 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0

* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
****************************************************************************/
#ifndef _ANDROID_BOOT_H_
#define _ANDROID_BOOT_H_
#define BOOT_MAGIC_SIZE 8

/*
 * The aosp boot header.
 * magic: The android magic to identify our image as an android boot image
 * kernel_size: size of the packaged kernel. In our case, the size of the image
 * padding: padding provided to match the alignment for a EFI flash page size because
 *          fastboot flashes an EFI_PAGE_SIZE block
 * All other parameters are not used
 */
typedef struct {
	uint8_t magic[BOOT_MAGIC_SIZE]; /*android magic: "ANDROID!"*/

	uint32_t kernel_size;  /* size in bytes */
	uint32_t kernel_addr;  /* physical load addr */

	uint32_t ramdisk_size; /* size in bytes */
	uint32_t ramdisk_addr; /* physical load addr */

	uint32_t second_size;  /* size in bytes */
	uint32_t second_addr;  /* physical load addr */

	uint32_t tags_addr;    /* physical addr for kernel tags */
	uint32_t page_size;    /* flash page size we assume */

	uint8_t padding[2008]; /*padding to 2048*/

} android_boot_header_t;

#endif
