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

#ifdef SERIAL_MMIO
static inline uint8_t serial_get_reg(uint64_t base_addr, uint32_t reg)
{
	return *(volatile uint8_t *)(base_addr + (uint64_t)reg * 4);
}

static inline void serial_set_reg(uint64_t base_addr, uint32_t reg, uint8_t val)
{
	*(volatile uint8_t *)(base_addr + (uint64_t)reg * 4) = val;
}
#else
static inline uint8_t serial_get_reg(uint64_t base_addr, uint32_t reg)
{
	return asm_in8((uint16_t)base_addr + (uint16_t)reg);
}

static inline void serial_set_reg(uint64_t base_addr, uint32_t reg, uint8_t val)
{
	asm_out8((uint16_t)base_addr + (uint16_t)reg, val);
}
#endif

uint64_t get_serial_base(void)
{

#ifdef SERIAL_PCI
#ifdef LIB_PCI
	return (uint64_t)(pci_read32(PCI_DEV(SERIAL_PCI_BUS, SERIAL_PCI_DEV,
				SERIAL_PCI_FUN), 0x10) & (~0xFULL));
#else
#error "LIB_PCI is not defined"
#endif
#else
	return (uint64_t)SERIAL_BASE;
#endif
}

static UNUSED void serial_pci_mmio_init(uint64_t serial_base)
{
	// temp workaround: get UART out of reset
	serial_set_reg(serial_base, 0x204, 0x00);
	serial_set_reg(serial_base, 0x204, 0x07);
	// update and enable (M, N) clock divider.  Magic register value is from PythonSv.
	*(uint32_t *)(serial_base + 0x200) = 0xffff04b5;

	serial_set_reg(serial_base, 0x0c, 0x83);
	serial_set_reg(serial_base, 0x00, 0x01);
	serial_set_reg(serial_base, 0x04, 0x00);
	serial_set_reg(serial_base, 0x0c, 0x03);

	// enable & reset (receive and transmit, 64 byte) FIFO:
	serial_set_reg(serial_base, 0x08, 0x27);

	// enable logging & printing of console output.
	serial_set_reg(serial_base, 0X1C, 0x00);
}

void serial_init(UNUSED uint64_t serial_base)
{
#if (defined SERIAL_PCI) && (defined SERIAL_MMIO)
	serial_pci_mmio_init(serial_base);
#endif
}


static void serial_putc(char c, uint64_t serial_base)
{
	uint8_t data;

	while (1)
	{
		data = serial_get_reg(serial_base, UART_REG_LSR);
		if (data & UART_LSR_THRE_MASK)
			break;
	}
	serial_set_reg(serial_base, UART_REG_THR, c);
}

void serial_puts(const char *str, uint64_t serial_base)
{
	uint32_t i;
	for (i = 0; str[i] != 0; i++)
		serial_putc(str[i], serial_base);
}
