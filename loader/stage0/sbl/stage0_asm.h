/****************************************************************************
* Copyright (c) 2015-2018 Intel Corporation
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

#ifndef _STAGE0_ASM_H_
#define _STAGE0_ASM_H_

#define LOAD_ADDR     0x12200000
/* The size of our stack (1KB) for stage0 */
#define STAGE0_STACK_SIZE            0x400

/* Simple and small GDT entries for booting only: */
#define __BOOT_NULL     (0x00)
#define __BOOT_CS       (0x08)
#define __BOOT_DS       (0x10)

/* Multiboot header Definitions of OS image*/
#define MULTIBOOT_HEADER_MAGIC          0x1BADB002

/* The flags for the Multiboot header (non-ELF) */
#define MULTIBOOT_HEADER_FLAGS         0x00010003

#endif
