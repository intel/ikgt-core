/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "vmm_base.h"

#ifdef LIB_PCI
#include "lib/pci.h"
#endif

#define UART_REG_THR 0     /* WO Transmit Holding Register */
#define UART_REG_RBR 0     /* RO Receive Buffer Register */
#define UART_REG_DLL 0     /* R/W Divisor Latch LSB */
#define UART_REG_DLM 1     /* R/W Divisor Latch MSB */
#define UART_REG_IER 1     /* R/W Interrupt Enable Register */
#define UART_REG_IIR 2     /* RO Interrupt Identification Register */
#define UART_REG_FCR 2     /* WO FIFO Cotrol Register */
#define UART_REG_LCR 3     /* R/W Line Control Register */
#define UART_REG_MCR 4     /* R/W Modem Control Register */
#define UART_REG_LSR 5     /* R/W Line Status Register */
#define UART_REG_MSR 6     /* R/W Modem Status Register */
#define UART_REG_SCR 7     /* R/W Scratch Pad Register */

#define UART_LSR_THRE_MASK (1 << 5)

typedef uint8_t (*serial_get_t) (uint64_t base, uint32_t reg);
typedef void (*serial_set_t) (uint64_t base, uint32_t reg, uint8_t c);

typedef struct serial_device {
	serial_get_t get;
	serial_set_t set;
	uint64_t base;
} serial_device_t;

serial_device_t g_ser_dev = {
	.get = NULL,
	.set = NULL,
	.base = -1ULL
};

static inline uint8_t serial_mmio_get(uint64_t base_addr, uint32_t reg)
{
	return *(volatile uint8_t *)(base_addr + (uint64_t)reg * 4);
}

static inline void serial_mmio_set(uint64_t base_addr, uint32_t reg, uint8_t val)
{
	*(volatile uint8_t *)(base_addr + (uint64_t)reg * 4) = val;
}

static inline uint8_t serial_io_get(uint64_t base_addr, uint32_t reg)
{
	return asm_in8((uint16_t)base_addr + (uint16_t)reg);
}

static inline void serial_io_set(uint64_t base_addr, uint32_t reg, uint8_t val)
{
	asm_out8((uint16_t)base_addr + (uint16_t)reg, val);
}

static void serial_8250_init(uint64_t serial_base)
{
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200 //default baud
#endif
	unsigned char c;
	unsigned div = 115200/SERIAL_BAUD;

	if (serial_base == -1ULL)
		return;

	g_ser_dev.set(serial_base, UART_REG_LCR, 0x3); // 8n1
	g_ser_dev.set(serial_base, UART_REG_IER, 0x0); // disable interrupt
	g_ser_dev.set(serial_base, UART_REG_FCR, 0x0); // disable fifo
	g_ser_dev.set(serial_base, UART_REG_MCR, 0x3); // DTR + RTS

	c = g_ser_dev.get(serial_base, UART_REG_LCR);
	g_ser_dev.set(serial_base, UART_REG_LCR,  c | BIT(8));          // Set DLAB(Divisor Latch Access Bit)
	g_ser_dev.set(serial_base, UART_REG_DLL,  div & 0xff);          // Set Divisor Latch Low byte
	g_ser_dev.set(serial_base, UART_REG_DLM,  (div >> 8) & 0xff);   // Set Divisor Latch High byte
	g_ser_dev.set(serial_base, UART_REG_LCR,  c & ~BIT(8));         // Clear DLAB
}

#ifdef SERIAL_PCI
static void init_pci_serial(uint16_t dev)
{
	uint32_t bar0;
	uint16_t class;

	class = pci_read16(dev, PCI_CLASS_REG);
	if ((class != PCI_CLASS_COM_SERIAL) &&
	    (class != PCI_CLASS_COM_MODEM)) {
		return;
	}

	bar0 = pci_read32(dev, PCI_BAR_REG(0));

	if (PCI_BAR_IS_IO_SPACE(bar0)) {
		pci_write16(dev, PCI_CMD_REG, pci_read16(dev, PCI_CMD_REG) | PCI_CMD_IO_SPACE);
		g_ser_dev.base = bar0 & PCI_BAR_IO_MASK;
		g_ser_dev.get = serial_io_get;
		g_ser_dev.set = serial_io_set;
	} else {
		pci_write16(dev, PCI_CMD_REG, pci_read16(dev, PCI_CMD_REG) | PCI_CMD_MEMORY_SPACE);
		g_ser_dev.base = bar0 & PCI_BAR_MMIO_MASK;
		g_ser_dev.get = serial_mmio_get;
		g_ser_dev.set = serial_mmio_set;
	}
}
#endif

#ifdef SERIAL_IO
static void init_io_serial(uint64_t base)
{
	g_ser_dev.base = base;
	g_ser_dev.get = serial_io_get;
	g_ser_dev.set = serial_io_set;
}
#endif

#ifdef SERIAL_MMIO
static void init_mmio_serial(uint64_t base)
{
	g_ser_dev.base = base;
	g_ser_dev.get = serial_mmio_get;
	g_ser_dev.set = serial_mmio_set;
}
#endif

void serial_init(boolean_t setup)
{
#ifdef SERIAL_PCI
	init_pci_serial(SERIAL_PCI);
#elif defined SERIAL_IO
	init_io_serial(SERIAL_IO);
#elif defined SERIAL_MMIO
	init_mmio_serial(SERIAL_MMIO);
#else
#error "Serial type(SERIAL_PCI/SERIAL_IO/SERIAL_MMIO) is not defined"
#endif

	if (setup) {
		serial_8250_init(g_ser_dev.base);
	}
}

static void serial_putc(char c)
{
	uint8_t data;

	if (g_ser_dev.base == -1ULL) {
		return;
	}

	while (1)
	{
		data = g_ser_dev.get(g_ser_dev.base, UART_REG_LSR);
		if (data & UART_LSR_THRE_MASK)
			break;
	}
	g_ser_dev.set(g_ser_dev.base, UART_REG_THR, c);
}

void serial_puts(const char *str)
{
	uint32_t i;

	if (g_ser_dev.base == -1ULL) {
		return;
	}

	for (i = 0; str[i] != 0; i++)
		serial_putc(str[i]);
}
