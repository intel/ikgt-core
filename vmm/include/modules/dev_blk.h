/*******************************************************************************
 * Copyright (c) 2015 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *******************************************************************************/

#ifndef _DEV_BLK_H_
#define _DEV_BLK_H_

#ifndef MODULE_DEV_BLK
#error "MODULE_DEV_BLK is not defined"
#endif

/* block_mmio() must be called after guest is created */
void block_mmio(uint16_t guest_id, uint64_t start_addr, uint64_t size);
void device_block_init(void);

#if (LIB_PCI && MODULE_ACPI && MODULE_IO_MONITOR)
/* block_pci_device() must be called between guest creation and gcpu initialization */
void block_pci_device(uint16_t guest_id, uint16_t pci_dev);
#endif

#endif /* _DEV_BLK_H_ */
