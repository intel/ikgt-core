/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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
