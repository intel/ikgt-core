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

#ifndef _MON_SERIAL_H_
#define _MON_SERIAL_H_

#include "uart.h"

/*========================================================================
 *
 * Public Functions
 *
 *======================================================================== */

/*=========================== mon_serial_new() =========================== */

/* Initialize a new serial device's parameters
 * Inputs: io_base - I/O Base Address
 *         prog_if - Programming interface
 *         handshake_mode - Handshake mode
 * Return: Handle to the device */
void *mon_serial_new(uint16_t io_base,
		     uart_prog_if_type_t prog_if,
		     uart_handshake_mode_t handshake_mode);

/*=========================== mon_serial_init() ========================== */

/* Initialize a serial device */
/* Input: h_device - Handle of the device */
void mon_serial_init(void *h_device);

/* Input: h_device - Handle of the device */
void mon_serial_reset(void *h_device);

/*======================= mon_serial_get_io_range() ====================== */
/* Input: h_device - Handle of the device
 * Returns: the I/O range (p_io_base:p_io_end) occupied by the device */

void mon_serial_get_io_range(void *h_device,
			     uint16_t *p_io_base,
			     uint16_t *p_io_end);

/*======================= mon_serial_putc_nolock() ======================= */

/* Write a single character to a serial device in a non-locked mode.
 * This function is reentrant, and can be safely called even while the normal
 * mon_serial_putc() runs.  However, it is not optimized for performance and
 * should only be used when necessary, e.g., from an exception handler.
 * Inputs: h_device - Handle of the device
 *         c - Character to send
 * Return: Character that was sent */
char mon_serial_putc_nolock(void *h_device, char c);

/*=========================== mon_serial_putc() ========================== */

/* Write a single character to a serial device.
 * This function is not reentrant, and is for use in the normal case, where the
 * serial device has been previously locked.  It may be interrupted by
 * mon_serial_putc_nolock().  The function attempts to use the full depth of
 * the UART's transmit FIFO to avoid busy loops.
 * Inputs: h_device - Handle of the device
 *         c - Character to send
 * Return: Character that was sent */
char mon_serial_putc(void *h_device, char c);

/*======================= mon_serial_puts_nolock() ======================= */

/* Write a string to a serial device in a non-locked mode.
 * This function is reentrant, and can be safely called even while the normal
 * mon_serial_putc() runs.  However, it should be used only when necessary,
 * e.g. from an exception handler.
 * Inputs: h_device - Handle of the device
 *         string - String to send
 * Return: 0 if failed */
int mon_serial_puts_nolock(void *h_device, const char string[]);

/*=========================== mon_serial_puts() ========================== */

/* Write a string to a serial device
 * This function is not reentrant, and is for use in the normal case, where the
 * serial device has been previously locked.  It may be interrupted by
 * mon_serial_put*_nolock().
 * Inputs: h_device - Handle of the device
 *         string - String to send
 * Return: 0 if failed */
int mon_serial_puts(void *h_device, const char string[]);

/*=========================== mon_serial_getc() ========================== */

/* Poll the serial device and read a single character if ready.
 * This function is not reentrant.  Calling it while it runs in another thread
 * may result in a junk character returned, but the s/w will not crash.
 * Input: h_device - Handle of the device
 * Return: a character read from the device, 0 if none */
char mon_serial_getc(void *h_device);

/*======================== mon_serial_cli_init() ========================= */

/* Initialize CLI command(s) for serial ports */

void mon_serial_cli_init(void);

#endif
