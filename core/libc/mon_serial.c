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

#include "mon_defs.h"
#include "mon_serial.h"
#include "hw_utils.h"
#include "mon_dbg.h"
#include "cli.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(MON_SERIAL_LIBC)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(MON_SERIAL_LIBC, __condition)

/*=========================================================================
 *
 * Private Definitions
 *
 *========================================================================= */

#define MON_SERIAL_DEVICES_MAX 8

typedef struct {
	uint16_t		io_base;
	uint8_t			reserved[2];
	uart_prog_if_type_t	prog_if;
	uart_handshake_mode_t	handshake_mode;
	/* Handshake mode, as set on initialization */
	uint32_t		hw_fifo_size;
	/* FIFO size of the UART h/w.  Set according to
	 * prog_if. */
	uint32_t		chars_in_tx_fifo;
	/* Current # of chars in h/w transmit FIFO.
	 * Note that this is a maximum estimate, actual
	 * number may be lower - this is the case where
	 * mon_serial_putc() is interrupted by
	 * mon_serial_putc_nolock(). */
	boolean_t		puts_lock;
	/* Used by puts_nolocked() to lock out regular
	 * prints */
	boolean_t		in_putc;        /* flags that putc() is executing */
	boolean_t		in_puts;        /* flags that puts() is executing */
	uint32_t		hw_handshake_stopped_count;
	/* Counts the number of consecutive times h/w
	 * handshake has been found to be "stop" */
	boolean_t		is_initialized;
	/* Indicates that the device has been
	 * initialized.  Used for sanity checks. */

	/* Statistics - Used Mainly for Debug */

	uint32_t		num_tx_chars_lock;
	uint32_t		num_tx_chars_nolock;
	/* Counters of transmitted characters in locked
	 * mode (normal) and nolock mode */
	uint32_t		num_rx_chars;
	/* Counter of received characters */
	uint32_t		wait_for_tx_ready_max;
	/* Max number of times tx paused until tx
	 * ready (for any reason). */
	uint32_t		wait_for_tx_ready_avg;
	/* Average number of times tx paused until tx
	 * ready (for any reason).  In units of
	 * 1 / 2^16 (i.e., upper 16 bits is the whole
	 * number part, lower 16 bit is the fraction */
	uint32_t		wait_for_tx_fifo_empty_max;
	/* Max number of times tx paused until tx
	 * FIFO was empty.  In units of */
	uint32_t		wait_for_tx_fifo_empty_avg;
	/* Average number of times tx paused until tx
	 * FIFO was empty.  In units of
	 * 1 / 2^16 (i.e., upper 16 bits is the whole
	 * number part, lower 16 bit is the fraction
	 * part). */
	uint32_t		num_puts_interrupted_by_putc_nolock;
	uint32_t		num_puts_interrupted_by_puts_nolock;
	/* count the number of times the normal puts()
	 * was interrupted by puts_nolock() and
	 * puts_nolock() */
	uint32_t		num_putc_blocked_by_puts_nolock;
	/* Counts the number of times the normal putc()
	 * was blocked by puts_nolock() */
	uint32_t		num_chars_hw_handshake_stopped;
	/* Number of characters for which tx stopped
	 * due to h/w handshake */
	uint32_t		num_chars_hw_handshake_auto_go;
	/* Number of characters for which tx h/w
	 * handshake stop was auto-released */
	uint32_t		hw_handshake_stopped_count_max;
	/* Maximum value of hw_handshake_stopped_count,
	 * for statistics & debug */
	uint32_t		hw_handshake_stopped_count_avg;
	/* Average value of hw_handshake_stopped_count,
	 * for statistics & debug.  In units of
	 * 1 / 2^16 (i.e., upper 16 bits is the whole
	 * number part, lower 16 bit is the fraction
	 * part). */
} mon_serial_device_t;

/*=========================================================================/
 *
 * Static Variables
 *
 *========================================================================= */

static mon_serial_device_t serial_devices[MON_SERIAL_DEVICES_MAX];
static uint32_t initialized_serial_devices;
static const uint32_t serial_tx_stall_usec = 100;
/* Number of usecs to stall between tx
 * attempts if not ready.  at 115200 baud,
 * each character is about 87 usec.
 */
static const uint32_t hw_handshake_stopped_limit = 50000; /* 5 sec */
/* In auto mode, number of stalls at h/w
 * handshake stop until thestatus is
 * considered "go" */
static const uint32_t avg_factor = 16;
/* Factor for averaging pause counters.  In
 * units of 1 / 2^16 */

/*========================================================================
 *
 * Static Functions
 *
 *======================================================================== */

/*=============================== update_max() =========================== */
/*
 * Updates the maximum of a counter
 */
static
void update_max(uint32_t *p_max,        /* Maximum */
		uint32_t count)
{                                       /* New input */
	if (count > *p_max) {
		*p_max = count;
	}
}

/*=============================== update_avg() =========================== */
/*
 * Updates the running average of a counter, using a simple 1-stage IIR
 * algorithm
 *    p_avg - Running average, as uint16_t.uint16_t
 *    count - new input
 */
static
void update_avg(uint32_t *p_avg,
		uint32_t count)
{
	uint64_t avg;

	/* Extend to 64 bits to prevent overflow during calculation */

	avg = (uint64_t)(*p_avg);

	/* Do the IIR.  The formula is:
	 * avg = (1 - f) * avg + f * counter
	 * Here the calculation is factored to UINT48.uint16_t representation */

	avg = ((((1 << 16) - avg_factor) * avg) +
	       (avg_factor * ((uint64_t)count << 16))
	       ) >> 16;

	/* Assign back, taking care of overflows */

	if (avg > (uint32_t)-1) {
		*p_avg = (uint32_t)-1;
	} else {
		*p_avg = (uint32_t)avg;
	}
}

/*========================= update_max_and_avg() ========================= */
/*
 * Updates the running average and maximum of a counter
 *    p_max - Maximum
 *    p_avg - Running average, as uint16_t.uint16_t
 *    count - New input
 */
static
void update_max_and_avg(uint32_t *p_max, uint32_t *p_avg, uint32_t count)
{
	update_max(p_max, count);
	update_avg(p_avg, count);
}

/*========================= is_hw_tx_handshake_go() ====================== */
/*
 * Checks h/w handshake lines for "go" status.  Count the number of times it
 * was "stop".  In UART_HANDSHAKE_AUTO mode, after a limit is reached force
 * the status to "go".
 */
static boolean_t is_hw_tx_handshake_go(mon_serial_device_t *p_device)
{
	/* Modem Status Register image */
	uart_msr_t msr;
	/* flags transmit "go" by h/w handshake */
	boolean_t hw_handshake_go;

	if ((p_device->handshake_mode == UART_HANDSHAKE_AUTO) ||
	    (p_device->handshake_mode == UART_HANDSHAKE_HW)) {
		/* Read the h/w handshake signals */

		msr.data =
			hw_read_port_8(p_device->io_base + UART_REGISTER_MSR);
		hw_handshake_go = (msr.bits.cts == 1) && (msr.bits.dsr == 1);

		if (hw_handshake_go) {
			/* Other side set h/w handshake to "go".  Reset the counter. */

			update_avg(&p_device->hw_handshake_stopped_count_avg,
				p_device->hw_handshake_stopped_count);
			p_device->hw_handshake_stopped_count = 0;
		} else if (p_device->hw_handshake_stopped_count >=
			   hw_handshake_stopped_limit) {
			/* Other side has indicated h/w handshake "stop" for too long. */

			if (p_device->handshake_mode == UART_HANDSHAKE_AUTO) {
				/* In auto mode, assume the h/w handshake is stuck and force */
				/* the status to "go" */

				hw_handshake_go = TRUE;
				p_device->num_chars_hw_handshake_auto_go++;
				if (p_device->hw_handshake_stopped_count ==
				    hw_handshake_stopped_limit) {
					/* Update the statistic only on the first character */
					/* we decided to auto-go */

					update_avg(
						&p_device->hw_handshake_stopped_count_avg,
						p_device->hw_handshake_stopped_count);
					p_device->hw_handshake_stopped_count++;
				}
			}
		} else {
			/* Increment the stop count and update the statistics */

			if (p_device->hw_handshake_stopped_count == 0) {
				/* We just stopped, increment the stops statistics counter */

				p_device->num_chars_hw_handshake_stopped++;
			}

			p_device->hw_handshake_stopped_count++;

			update_max(&p_device->hw_handshake_stopped_count_max,
				p_device->hw_handshake_stopped_count);
		}
	} else {
		/* No h/w handshake, always "go" */

		hw_handshake_go = TRUE;
	}

	return hw_handshake_go;
}

/*====================== cli_display_serial_info() ======================= */

#ifdef DEBUG
/*
 * Display serial ports' information on the CLI
 */
static
int cli_display_serial_info(unsigned argc UNUSED, char *args[] UNUSED)
{
	uint32_t i;

	CLI_PRINT("Serial Device #                :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT("        %1d     ", i);

	CLI_PRINT("\nI/O Base                       :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT("   0x%04x     ", serial_devices[i].io_base);

	CLI_PRINT("\nChars Tx (lock)                :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d     ", serial_devices[i].num_tx_chars_lock);

	CLI_PRINT("\nChars Tx (nolock)              :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d     ", serial_devices[i].num_tx_chars_nolock);

	CLI_PRINT("\nChars Rx                       :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d     ", serial_devices[i].num_rx_chars);

	CLI_PRINT("\nTx Ready Wait Time (max)       :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d uSec",
			serial_devices[i].wait_for_tx_ready_max *
			serial_tx_stall_usec);

	CLI_PRINT("\nTx Ready Wait Time (avg)       :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d uSec",
			(serial_devices[i].wait_for_tx_ready_avg >> 16) *
			serial_tx_stall_usec);

	CLI_PRINT("\nTx FIFO Empty Wait Time (max)  :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d uSec",
			serial_devices[i].wait_for_tx_fifo_empty_max *
			serial_tx_stall_usec);

	CLI_PRINT("\nTx FIFO Empty Wait Time (avg)  :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d uSec",
			(serial_devices[i].wait_for_tx_fifo_empty_avg >> 16) *
			serial_tx_stall_usec);

	CLI_PRINT("\nTx H/S Mode                    :");
	for (i = 0; i < initialized_serial_devices; i++) {
		switch (serial_devices[i].handshake_mode) {
		case UART_HANDSHAKE_AUTO:
			if (serial_devices[i].hw_handshake_stopped_count <
			    hw_handshake_stopped_limit) {
				CLI_PRINT("     Auto-H/W ");
			} else {
				CLI_PRINT("     Auto-None");
			}
			break;

		case UART_HANDSHAKE_HW:
			CLI_PRINT("      H/W     ");
			break;

		case UART_HANDSHAKE_NONE:
			CLI_PRINT("     None     ");
			break;

		default:
			CLI_PRINT(" ?????????????");
		}
	}

	CLI_PRINT("\nTx H/S Stopped Chars           :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d     ",
			serial_devices[i].num_chars_hw_handshake_stopped);

	CLI_PRINT("\nTx H/S Auto-Go Chars           :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d     ",
			serial_devices[i].num_chars_hw_handshake_auto_go);

	CLI_PRINT("\nTx H/S Stopped Time (max)      :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d mSec",
			serial_devices[i].hw_handshake_stopped_count_max *
			serial_tx_stall_usec / 1000);

	CLI_PRINT("\nTx H/S Stopped Time (avg)      :");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d mSec",
			(serial_devices[i].hw_handshake_stopped_count_avg >> 16) *
			serial_tx_stall_usec / 1000);

	CLI_PRINT("\nString Tx inter. by putc_nolock:");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d     ",
			serial_devices[i].num_puts_interrupted_by_putc_nolock);

	CLI_PRINT("\nString Tx inter. by puts_nolock:");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d     ",
			serial_devices[i].num_puts_interrupted_by_puts_nolock);

	CLI_PRINT("\nChars Tx blocked by puts_nolock:");
	for (i = 0; i < initialized_serial_devices; i++)
		CLI_PRINT(" %8d     ",
			serial_devices[i].num_putc_blocked_by_puts_nolock);

	CLI_PRINT("\n");

	return 0;
}

#endif                          /* DEBUG */

/*=======================================================================
 *
 * Public Functions
 *
 *======================================================================= */

/*=========================== mon_serial_new() ========================== */
/*
 * Initialize a new serial device's parameters
 * Input: Programming interface
 * Input: I/O Base Address
 * Input: Handshake mode
 * Return: Handle to the device
 */
void *mon_serial_new(uint16_t io_base, uart_prog_if_type_t prog_if,
		     uart_handshake_mode_t handshake_mode)
{
	mon_serial_device_t *p_device;

	if (initialized_serial_devices >= MON_SERIAL_DEVICES_MAX) {
		return NULL;
	}

	p_device = &serial_devices[initialized_serial_devices++];

	p_device->io_base = io_base;
	p_device->prog_if = prog_if;
	switch (prog_if) {
	case UART_PROG_IF_GENERIC:
	case UART_PROG_IF_16450:
		p_device->hw_fifo_size = 1;
		break;

	case UART_PROG_IF_16550:
	case UART_PROG_IF_16650:
	case UART_PROG_IF_16750:
	case UART_PROG_IF_16850:
	case UART_PROG_IF_16950:
		p_device->hw_fifo_size = 16;
		break;

	default:
		return NULL;
	}
	;
	p_device->chars_in_tx_fifo = p_device->hw_fifo_size;
	/* This forces polling of the transmit empty status bit */
	p_device->handshake_mode = handshake_mode;

	p_device->num_tx_chars_lock = 0;
	p_device->num_tx_chars_nolock = 0;
	p_device->num_rx_chars = 0;

	p_device->wait_for_tx_ready_max = 0;
	p_device->wait_for_tx_ready_avg = 0;
	p_device->wait_for_tx_fifo_empty_max = 0;
	p_device->wait_for_tx_fifo_empty_avg = 0;

	p_device->puts_lock = FALSE;
	p_device->in_putc = FALSE;
	p_device->in_puts = FALSE;
	p_device->num_puts_interrupted_by_putc_nolock = 0;
	p_device->num_puts_interrupted_by_puts_nolock = 0;
	p_device->num_putc_blocked_by_puts_nolock = 0;

	p_device->num_chars_hw_handshake_stopped = 0;
	p_device->num_chars_hw_handshake_auto_go = 0;
	p_device->hw_handshake_stopped_count = 0;
	p_device->hw_handshake_stopped_count_max = 0;
	p_device->hw_handshake_stopped_count_avg = 0;

	p_device->is_initialized = FALSE;

	return p_device;
}

/*=========================== mon_serial_init() ========================= */
/*
 * Initialize a serial device
 * Input: Handle of the device
 */
void mon_serial_init(void *h_device)
{
	mon_serial_device_t *p_device;
	uart_ier_t ier;
	uart_fcr_t fcr;
	uart_lcr_t lcr;
	uart_mcr_t mcr;

	p_device = h_device;
	MON_ASSERT(p_device);
	MON_ASSERT(!p_device->is_initialized);

	/* MCR: Reset DTR, RTS, Out1, Out2 & Loop */

	mcr.bits.dtrc = 0;
	mcr.bits.rts = 0;
	mcr.bits.out1 = 0;
	mcr.bits.out2 = 0;
	mcr.bits.lme = 0;
	mcr.bits.reserved = 0;
	hw_write_port_8(p_device->io_base + UART_REGISTER_MCR, mcr.data);

	/* LCR: Reset DLAB */

	lcr.bits.serialdb = 0x03;       /* 8 data bits */
	lcr.bits.stopb = 0;             /* 1 stop bit */
	lcr.bits.paren = 0;             /* No parity */
	lcr.bits.evenpar = 0;           /* N/A */
	lcr.bits.sticpar = 0;           /* N/A */
	lcr.bits.brcon = 0;             /* No break */
	lcr.bits.dlab = 0;
	hw_write_port_8(p_device->io_base + UART_REGISTER_LCR, lcr.data);

	/* IER: Disable interrupts */

	ier.bits.ravie = 0;
	ier.bits.theie = 0;
	ier.bits.rie = 0;
	ier.bits.mie = 0;
	ier.bits.reserved = 0;
	hw_write_port_8(p_device->io_base + UART_REGISTER_IER, ier.data);

	/* FCR: Disable FIFOs */

	fcr.bits.trfifoe = 0;
	fcr.bits.resetrf = 0;
	fcr.bits.resettf = 0;
	fcr.bits.dms = 0;
	fcr.bits.reserved = 0;
	fcr.bits.rtb = 0;
	hw_write_port_8(p_device->io_base + UART_REGISTER_FCR, fcr.data);

	/* SCR: Scratch register */

	hw_write_port_8(p_device->io_base + UART_REGISTER_SCR, 0x00);

	/* LCR: Set DLAB */

	lcr.bits.dlab = 1;
	hw_write_port_8(p_device->io_base + UART_REGISTER_LCR, lcr.data);

	/* DLL & DLM: Divisor value 1 for 115200 baud */

	hw_write_port_8(p_device->io_base + UART_REGISTER_DLL, 0x01);
	hw_write_port_8(p_device->io_base + UART_REGISTER_DLM, 0x00);

	/* LCR: Reset DLAB */

	lcr.bits.dlab = 0;
	hw_write_port_8(p_device->io_base + UART_REGISTER_LCR, lcr.data);

	/* FCR: Enable and reset Rx & Tx FIFOs */

	fcr.bits.trfifoe = 1;
	fcr.bits.resetrf = 1;
	fcr.bits.resettf = 1;
	hw_write_port_8(p_device->io_base + UART_REGISTER_FCR, fcr.data);

	/* MCR: Set DTR, RTS */

	mcr.bits.dtrc = 1;
	mcr.bits.rts = 1;
	hw_write_port_8(p_device->io_base + UART_REGISTER_MCR, mcr.data);

	p_device->is_initialized = TRUE;
}

void mon_serial_reset(void *h_device)
{
	mon_serial_device_t *p_device;

	p_device = h_device;
	p_device->is_initialized = FALSE;
}

/*======================= mon_serial_get_io_range() ====================== */
/*
 * Get the I/O range occupied by the device
 * Input: Handle of the device
 * Output: Base of I/O range
 * Output: End of I/O range
 * Returns: the I/O range occupied by the device
 */
void mon_serial_get_io_range(void *h_device, uint16_t *p_io_base,
			     uint16_t *p_io_end)
{
	mon_serial_device_t *p_device;

	p_device = h_device;

	*p_io_base = p_device->io_base;
	*p_io_end = p_device->io_base + 7;
}

/*======================= mon_serial_putc_nolock() =======================
 *
 * Write a single character to a serial device in a non-locked mode.
 * This function is reentrant, and can be safely called even while the normal
 * mon_serial_putc() runs.  However, it is not optimized for performance and
 * should only be used when necessary, e.g., from an exception handler.
 * Input: Handle of the device
 * Input: Character to send
 * Return: Character that was sent
 */
char mon_serial_putc_nolock(void *h_device, char c)
{
	mon_serial_device_t *p_device;
	/* Line Status Register image */
	uart_lsr_t lsr;
	boolean_t is_ready;
	/* The Tx FIFO is empty and hw handshake is "go" */
	uint32_t num_wait_for_tx_ready;

	p_device = h_device;

	MON_ASSERT(p_device->is_initialized);

	if (p_device->in_puts) {
		p_device->num_puts_interrupted_by_putc_nolock++;
	}

	/* Another instance of the mon_serial_putc*() functions can be running in
	 * parallel (e.g., on another h/w thread).  We rely on the Tx FIFO to
	 * be deed enough absorb the writes (and hope for the best...).  Thus, we
	 * first loop until the Tx FIFO is empty and h/w handshake is OK
	 * (if applicable) */

	num_wait_for_tx_ready = 0;
	do {
		lsr.data =
			hw_read_port_8(p_device->io_base + UART_REGISTER_LSR);

		is_ready = (lsr.bits.thre == 1) && is_hw_tx_handshake_go(
			p_device);

		if (!is_ready) {
			hw_stall_using_tsc(serial_tx_stall_usec);
			num_wait_for_tx_ready++;
		}
	} while (!is_ready);

	update_max_and_avg(&p_device->wait_for_tx_fifo_empty_max,
		&p_device->wait_for_tx_fifo_empty_avg,
		num_wait_for_tx_ready);
	update_max_and_avg(&p_device->wait_for_tx_ready_max,
		&p_device->wait_for_tx_ready_avg, num_wait_for_tx_ready);

	/* Now write the output character */
	hw_write_port_8(p_device->io_base + UART_REGISTER_THR, c);

	/* Update the statistics */

	p_device->num_tx_chars_nolock++;

	/* Loop again until the Tx FIFO is empty and h/w handshake is OK
	 * (if applicable).  This is done so normal mon_serial_putc() that we may
	 * have interrupted can safely resume. */

	num_wait_for_tx_ready = 0;
	do {
		lsr.data =
			hw_read_port_8(p_device->io_base + UART_REGISTER_LSR);

		is_ready = is_hw_tx_handshake_go(p_device) &&
			   (lsr.bits.thre == 1);

		if (!is_ready) {
			hw_stall_using_tsc(serial_tx_stall_usec);
			num_wait_for_tx_ready++;
		}
	} while (!is_ready);

	update_max_and_avg(&p_device->wait_for_tx_fifo_empty_max,
		&p_device->wait_for_tx_fifo_empty_avg,
		num_wait_for_tx_ready);
	update_max_and_avg(&p_device->wait_for_tx_ready_max,
		&p_device->wait_for_tx_ready_avg, num_wait_for_tx_ready);

	/* Note that we do NOT update chars_in_tx_fifo to be 0.  This allows
	 * parallel putc's that will be absorbed by the FIFO. */

	return c;
}

/*=========================== mon_serial_putc() ========================== */
/*
 * Write a single character to a serial device.
 * This function is not reentrant, and is for use in the normal case, where the
 * serial device has been previously locked.  It may be interrupted by
 * mon_serial_putc_nolock().  The function attempts to use the full depth of
 * the UART's transmit FIFO to avoid busy loops.
 * Input: Handle of the device
 * Input: Character to send
 * Return: Character that was sent
 */
char
mon_serial_putc(void *h_device, char c)
{
	mon_serial_device_t *p_device;
	/* Line Status Register image */
	uart_lsr_t lsr;
	boolean_t is_ready;
	/* The Tx FIFO is not full and hw handshake is "go" */
	boolean_t locked_out = FALSE;
	/* Indicate that the function was locked out at */
	/* least once by puts_lock */
	uint32_t num_wait_for_tx_ready;
	uint32_t num_wait_for_tx_fifo_empty;

	p_device = h_device;

	MON_ASSERT(p_device->is_initialized);

	/* Loop until there's room in the Tx FIFO, h/w handshake is OK
	 * (if applicable), and there is no lock by mon_serial_puts_nolock(). */

	num_wait_for_tx_ready = 0;
	num_wait_for_tx_fifo_empty = 0;

	do {
		lsr.data =
			hw_read_port_8(p_device->io_base + UART_REGISTER_LSR);
		if (lsr.bits.thre == 1) { /* The Tx FIFO is empty */
			p_device->chars_in_tx_fifo = 0;
		}

		is_ready = is_hw_tx_handshake_go(p_device);

		if (is_ready) {
			if (p_device->chars_in_tx_fifo >=
			    p_device->hw_fifo_size) {
				is_ready = FALSE;
				num_wait_for_tx_fifo_empty++;
			}
		}

		if (is_ready && p_device->puts_lock) {
			/* There's an on going string print by mon_serial_puts_nolock() */

			is_ready = FALSE;
			locked_out = TRUE;
		}

		if (!is_ready) {
			hw_stall_using_tsc(serial_tx_stall_usec);
			num_wait_for_tx_ready++;
		}
	} while (!is_ready);

	update_max_and_avg(&p_device->wait_for_tx_ready_max,
		&p_device->wait_for_tx_ready_avg, num_wait_for_tx_ready);
	update_max_and_avg(&p_device->wait_for_tx_fifo_empty_max,
		&p_device->wait_for_tx_fifo_empty_avg,
		num_wait_for_tx_fifo_empty);

	/* Now write the output character */
	hw_write_port_8(p_device->io_base + UART_REGISTER_THR, c);

	p_device->chars_in_tx_fifo++;

	/* Update the statistics */

	p_device->num_tx_chars_lock++;
	if (locked_out) {
		p_device->num_putc_blocked_by_puts_nolock++;
	}

	return c;
}

/*======================= mon_serial_puts_nolock() ======================= */
/*
 * Write a string to a serial device in a non-locked mode.
 * This function is reentrant, and can be safely called even while the normal
 * mon_serial_putc() runs.  However, it should be used only when necessary,
 * e.g. from an exception handler.
 * Input: Handle of the device
 * Input: String to send
 * Return: 0 if failed
 */
int mon_serial_puts_nolock(void *h_device, const char string[])
{
	mon_serial_device_t *p_device;
	uint32_t i;

	p_device = h_device;
	/* Block the normal putc() */
	p_device->puts_lock = TRUE;
	/* Not reliable in case this function is
	 * called by two h/w threads in parallel, but
	 * the impact is not fatal */

	if (p_device->in_puts) {
		p_device->num_puts_interrupted_by_puts_nolock++;
	}

	for (i = 0; string[i] != 0; i++)
		mon_serial_putc_nolock(h_device, string[i]);

	/* Unblock the normal putc() */
	p_device->puts_lock = FALSE;
	/* return any nonnegative value */
	return 1;
}

/*=========================== mon_serial_puts() ========================== */
/*
 * Write a string to a serial device
 * This function is not reentrant, and is for use in the normal case, where the
 * serial device has been previously locked.  It may be interrupted by
 * mon_serial_put*_nolock().
 * Input: Handle of the device
 * Input: String to send
 * Return: 0 if failed
 */
int mon_serial_puts(void *h_device, const char string[])
{
	mon_serial_device_t *p_device;
	uint32_t i;

	p_device = h_device;

	for (i = 0; string[i] != 0; i++) {
		mon_serial_putc(h_device, string[i]);
		p_device->in_puts = TRUE;
	}

	p_device->in_puts = FALSE;

	/* return any nonnegative value */
	return 1;
}

/*=========================== mon_serial_getc() ========================== */
/*
 * Poll the serial device and read a single character if ready.
 * This function is not reentrant.  Calling it while it runs in another thread
 * may result in a junk character returned, but the s/w will not crash.
 * Input: Handle of the device
 * Return: Character read from the device, 0 if none.
 */
char mon_serial_getc(void *h_device)
{
	mon_serial_device_t *p_device;
	uart_lsr_t lsr;
	char c;

	p_device = h_device;

	MON_ASSERT(p_device->is_initialized);

	lsr.data = hw_read_port_8(p_device->io_base + UART_REGISTER_LSR);
	if (lsr.bits.dr) {
		/* Rx not empty */
		c = hw_read_port_8(p_device->io_base + UART_REGISTER_RBR);
		p_device->num_rx_chars++;
	} else {
		c = 0;
	}

	return c;
}

/*======================== mon_serial_cli_init() ========================= */

/*
 * Initialize CLI command(s) for serial ports
 */
void mon_serial_cli_init(void)
{
#ifdef DEBUG
	cli_add_command(cli_display_serial_info,
		"debug serial info",
		"Print serial ports information",
		"", CLI_ACCESS_LEVEL_SYSTEM);
#endif
}
