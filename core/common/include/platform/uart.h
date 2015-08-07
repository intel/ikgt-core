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

#ifndef _UART_H_
#define _UART_H_

/*=============================================================================
 *
 * UART (Universal Asynchronous Receiver Transmitter) Serial Controller
 *
 * Hardware Definitions File
 *
 *============================================================================= */

/* UART Programming Interface Type (Same as the PCI definition) */

typedef enum {
	UART_PROG_IF_GENERIC = 0,
	UART_PROG_IF_16450 = 1,
	UART_PROG_IF_16550 = 2, /* This is the default */
	UART_PROG_IF_16650 = 3,
	UART_PROG_IF_16750 = 4,
	UART_PROG_IF_16850 = 5,
	UART_PROG_IF_16950 = 6,
	UART_PROG_IF_DEFAULT = 2
} uart_prog_if_type_t;

/* Serial Port Handshake Mode */

typedef enum {
	UART_HANDSHAKE_NONE = 0,        /* No handshake */
	UART_HANDSHAKE_HW = 1,          /* RS-232 signals CTS/RTS and DTR/DSR */
	UART_HANDSHAKE_XON_XOFF = 2,    /* XON (ctrl-S) and XOFF (ctrl-Q) */
	UART_HANDSHAKE_AUTO = 3,        /* Handshake mode is automatically detected */
	UART_HANDSHAKE_DEFAULT = 3
} uart_handshake_mode_t;

/* (24000000/13)MHz input clock */

#define UART_INPUT_CLOCK 1843200

/* 115200 baud with rounding errors */

#define UART_MAX_BAUD_RATE           115400
#define UART_MIN_BAUD_RATE           50

#define UART_MAX_RECEIVE_FIFO_DEPTH  16
#define UART_MIN_TIMEOUT             1          /* 1 uS */
#define UART_MAX_TIMEOUT             100000000  /* 100 seconds */

/* UART Registers */

#define UART_REGISTER_THR 0     /* WO Transmit Holding Register */
#define UART_REGISTER_RBR 0     /* RO Receive Buffer Register */
#define UART_REGISTER_DLL 0     /* R/W Divisor Latch LSB */
#define UART_REGISTER_DLM 1     /* R/W Divisor Latch MSB */
#define UART_REGISTER_IER 1     /* R/W Interrupt Enable Register */
#define UART_REGISTER_IIR 2     /* RO Interrupt Identification Register */
#define UART_REGISTER_FCR 2     /* WO FIFO Cotrol Register */
#define UART_REGISTER_LCR 3     /* R/W Line Control Register */
#define UART_REGISTER_MCR 4     /* R/W Modem Control Register */
#define UART_REGISTER_LSR 5     /* R/W Line Status Register */
#define UART_REGISTER_MSR 6     /* R/W Modem Status Register */
#define UART_REGISTER_SCR 7     /* R/W Scratch Pad Register */


/* Name: uart_ier_bits_t
 * Purpose: Define each bit in Interrupt Enable Register
 * Context:
 * Fields:
 *     RAVIE Bit0: Receiver Data Available Interrupt Enable
 *     THEIE Bit1: Transmistter Holding Register Empty Interrupt Enable
 *     RIE Bit2: Receiver Interrupt Enable
 *     MIE Bit3: Modem Interrupt Enable
 *     reserved Bit4-Bit7: reserved */

typedef struct {
	uint32_t ravie:1;
	uint32_t theie:1;
	uint32_t rie:1;
	uint32_t mie:1;
	uint32_t reserved:4;
} PACKED uart_ier_bits_t;

/* Name: uart_ier_t
 * Purpose:
 * Context:
 * Fields:
 *     Bits uart_ier_bits_t: Bits of the IER
 *     Data uint8_t: the value of the IER */

typedef union {
	uart_ier_bits_t bits;
	uint8_t		data;
} uart_ier_t;

/* Name: uart_iir_bits_t
 * Purpose: Define each bit in Interrupt Identification Register
 * Context:
 * Fields:
 *     IPS Bit0: Interrupt Pending Status
 *     IIB Bit1-Bit3: Interrupt ID Bits
 *     reserved Bit4-Bit5: reserved
 *     FIFOES Bit6-Bit7: FIFO Mode Enable Status */

typedef struct {
	uint32_t ips:1;
	uint32_t iib:3;
	uint32_t reserved:2;
	uint32_t fifoes:2;
} PACKED uart_iir_bits_t;

/* Name: uart_iir_t
 * Purpose:
 * Context:
 * Fields:
 *     Bits uart_iir_bits_t: Bits of the IIR
 *     Data uint8_t: the value of the IIR */

typedef union {
	uart_iir_bits_t bits;
	uint8_t		data;
} uart_iir_t;

/* Name: uart_fcr_bits_t
 * Purpose: Define each bit in FIFO Control Register
 * Context:
 * Fields:
 *     TRFIFOE Bit0: Transmit and Receive FIFO Enable
 *     RESETRF Bit1: Reset Reciever FIFO
 *     RESETTF Bit2: Reset Transmistter FIFO
 *     DMS Bit3: DMA Mode Select
 *     reserved Bit4-Bit5: reserved
 *     RTB Bit6-Bit7: Receive Trigger Bits */

typedef struct {
	uint32_t trfifoe:1;
	uint32_t resetrf:1;
	uint32_t resettf:1;
	uint32_t dms:1;
	uint32_t reserved:2;
	uint32_t rtb:2;
} PACKED uart_fcr_bits_t;

/* Name: uart_fcr_t
 * Purpose:
 * Context:
 * Fields:
 *     Bits uart_fcr_bits_t: Bits of the FCR
 *     Data uint8_t: the value of the FCR */

typedef union {
	uart_fcr_bits_t bits;
	uint8_t		data;
} uart_fcr_t;

/* Name: uart_lcr_bits_t
 * Purpose: Define each bit in Line Control Register
 * Context:
 * Fields:
 *     SERIALDB Bit0-Bit1: Number of Serial Data Bits
 *     STOPB Bit2: Number of Stop Bits
 *     PAREN Bit3: Parity Enable
 *     EVENPAR Bit4: Even Parity Select
 *     STICPAR Bit5: Sticky Parity
 *     BRCON Bit6: Break Control
 *     DLAB Bit7: Divisor Latch Access Bit */

typedef struct {
	uint32_t serialdb:2;
	uint32_t stopb:1;
	uint32_t paren:1;
	uint32_t evenpar:1;
	uint32_t sticpar:1;
	uint32_t brcon:1;
	uint32_t dlab:1;
} PACKED uart_lcr_bits_t;

/* Name: uart_lcr_t
 * Purpose:
 * Context:
 * Fields:
 *     Bits uart_lcr_bits_t: Bits of the LCR
 *     Data uint8_t: the value of the LCR */

typedef union {
	uart_lcr_bits_t bits;
	uint8_t		data;
} uart_lcr_t;

/* Name: uart_mcr_bits_t
 * Purpose: Define each bit in Modem Control Register
 * Context:
 * Fields:
 *     DTRC Bit0: Data Terminal Ready Control
 *     RTS Bit1: Request To Send Control
 *     OUT1 Bit2: Output1
 *     OUT2 Bit3: Output2, used to disable interrupt
 *     LME; Bit4: Loopback Mode Enable
 *     reserved Bit5-Bit7: reserved */

typedef struct {
	uint32_t dtrc:1;
	uint32_t rts:1;
	uint32_t out1:1;
	uint32_t out2:1;
	uint32_t lme:1;
	uint32_t reserved:3;
} PACKED uart_mcr_bits_t;

/* Name: uart_mcr_t
 * Purpose:
 * Context:
 * Fields:
 *     Bits uart_mcr_bits_t: Bits of the MCR
 *     Data uint8_t: the value of the MCR */

typedef union {
	uart_mcr_bits_t bits;
	uint8_t		data;
} uart_mcr_t;

/* Name: uart_lsr_bits_t
 * Purpose: Define each bit in Line Status Register
 * Context:
 * Fields:
 *     DR Bit0: Receiver Data Ready Status
 *     OE Bit1: Overrun Error Status
 *     PE Bit2: Parity Error Status
 *     FE Bit3: Framing Error Status
 *     BI Bit4: Break Interrupt Status
 *     THRE Bit5: Transmistter Holding Register Status
 *     TEMT Bit6: Transmitter Empty Status
 *     FIFOE Bit7: FIFO Error Status */

typedef struct {
	uint32_t dr:1;
	uint32_t oe:1;
	uint32_t pe:1;
	uint32_t fe:1;
	uint32_t bi:1;
	uint32_t thre:1;
	uint32_t temt:1;
	uint32_t fifoe:1;
} PACKED uart_lsr_bits_t;

/* Name: uart_lsr_t
 * Purpose:
 * Context:
 * Fields:
 *     Bits uart_lsr_bits_t: Bits of the LSR
 *     Data uint8_t: the value of the LSR */

typedef union {
	uart_lsr_bits_t bits;
	uint8_t		data;
} uart_lsr_t;

/* Name:    uart_msr_bits_t
 * Purpose: Define each bit in Modem Status Register
 * Context:
 * Fields:
 *     DeltaCTS Bit0: Delta Clear To Send Status
 *     DeltaDSR Bit1: Delta Data Set Ready Status
 *     TrailingEdgeRI Bit2: Trailing Edge of Ring Indicator Status
 *     DeltaDCD Bit3: Delta Data Carrier Detect Status
 *     CTS      Bit4: Clear To Send Status
 *     DSR      Bit5: Data Set Ready Status
 *     RI       Bit6: Ring Indicator Status
 *     DCD      Bit7: Data Carrier Detect Status */

typedef struct {
	uint32_t delta_cts:1;
	uint32_t delta_dsr:1;
	uint32_t trailing_edge_ri:1;
	uint32_t delta_dcd:1;
	uint32_t cts:1;
	uint32_t dsr:1;
	uint32_t ri:1;
	uint32_t dcd:1;
} PACKED uart_msr_bits_t;

/* Name: uart_msr_t
 * Purpose:
 * Context:
 * Fields:
 *     Bits uart_msr_bits_t: Bits of the MSR
 *     Data uint8_t: the value of the MSR */

typedef union {
	uart_msr_bits_t bits;
	uint8_t		data;
} uart_msr_t;


#endif
