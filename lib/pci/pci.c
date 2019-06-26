/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"

#include "lib/pci.h"

/* Notes:
 * 1. According to PCI spec, the 2 low bits of PCI_CFG_ADDR are
 *    read-only and must be 0. We have verified it with tests.
 *
 * 2. For pci_read/write16(), the caller must guarantee the reg is
 *    2-bytes aligned; and for pci_read/write32() the reg must be
 *    4-bytes aligned. Otherwise, the behavior is undefined.
 */

uint8_t pci_read8(uint16_t pci_dev, uint8_t reg)
{
	asm_out32(PCI_CFG_ADDR, PCI_ADDR(pci_dev, reg) & (~0x3U));
	return asm_in8(PCI_CFG_DATA | (reg & 0x3));
}

void pci_write8(uint16_t pci_dev, uint8_t reg, uint8_t val)
{
	asm_out32(PCI_CFG_ADDR, PCI_ADDR(pci_dev, reg) & (~0x3U));
	asm_out8(PCI_CFG_DATA | (reg & 0x3), val);
}

uint16_t pci_read16(uint16_t pci_dev, uint8_t reg)
{
	asm_out32(PCI_CFG_ADDR, PCI_ADDR(pci_dev, reg) & (~0x3U));
	return asm_in16(PCI_CFG_DATA | (reg & 0x2));
}

void pci_write16(uint16_t pci_dev, uint8_t reg, uint16_t val)
{
	asm_out32(PCI_CFG_ADDR, PCI_ADDR(pci_dev, reg) & (~0x3U));
	asm_out16(PCI_CFG_DATA | (reg & 0x2), val);
}

uint32_t pci_read32(uint16_t pci_dev, uint8_t reg)
{
	asm_out32(PCI_CFG_ADDR, PCI_ADDR(pci_dev, reg) & (~0x3U));
	return asm_in32(PCI_CFG_DATA);
}

void pci_write32(uint16_t pci_dev, uint8_t reg, uint32_t val)
{
	asm_out32(PCI_CFG_ADDR, PCI_ADDR(pci_dev, reg) & (~0x3U));
	asm_out32(PCI_CFG_DATA, val);
}
