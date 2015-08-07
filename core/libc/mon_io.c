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
#include "mon_dbg.h"
#include "libc_internal.h"
#include "hw_interlocked.h"
#include "vmcall.h"
#include "host_memory_manager_api.h"
#include "mon_globals.h"
#include "mon_serial.h"

extern int CLI_active(void);
/*
 * C-written CRT routines should be put here
 */

#define PRINTF_BUFFER_SIZE  512

/* Used to guard the print function.
 *     0 : not locked
 *     1 or more : locked
 */
static uint32_t printf_lock;

/*
 *-------------- Internal functions ----------------------
 */

/*===================== raw_lock(), raw_unlock() ==========================
 *
 * These functions are used for doing lock/unlock
 * without CPU identification/validation
 * The reason is to have the lock facility at the stage when cpu ID is unknown
 * e.g. for LOGs at the bootstrap time
 *
 *========================================================================= */
static
void raw_lock(volatile uint32_t *p_lock_var)
{
	uint32_t old_value;

	for (;; ) {
		/* Loop until the successfully incremented the lock variable */
		/* from 0 to 1 (i.e., we are the only lockers */

		old_value = hw_interlocked_compare_exchange(
			(int32_t *)p_lock_var,
			0, /* Expected */
			1); /* New */
		if (0 == old_value) {
			break;
		}
		hw_pause();
	}
}

static
void raw_force_lock(volatile uint32_t *p_lock_var)
{
	int32_t old_value;

	for (;; ) {
		/* Loop until successfully incremented the lock variable */

		old_value = *p_lock_var;
		if (old_value ==
		    hw_interlocked_compare_exchange((int32_t *)p_lock_var,
			    old_value,                                                  /* Expected */
			    old_value + 1)) {                                           /* New */
			break;
		}
		hw_pause();
	}
}

static
void raw_unlock(volatile uint32_t *p_lock_var)
{
	int32_t old_value;

	for (;; ) {
		/* Loop until successfully decremented the lock variable */

		old_value = *p_lock_var;
		if (old_value ==
		    hw_interlocked_compare_exchange((int32_t *)p_lock_var,
			    old_value,                                                  /* Expected */
			    old_value - 1)) {                                           /* New */
			break;
		}
		hw_pause();
	}
}

/*=========================================================================
 *
 * Generic Debug Port Static Variables
 *
 *========================================================================= */

static mon_debug_port_type_t debug_port_type = MON_DEBUG_PORT_NONE;
static mon_debug_port_virt_mode_t debug_port_virt_mode =
	MON_DEBUG_PORT_VIRT_NONE;
static void *debug_port_handle;

/*========================================================================
 *
 * Generic Debug Port Functions
 *
 *======================================================================== */

/*======================================================================== */

boolean_t mon_debug_port_init_params(const mon_debug_port_params_t *p_params)
{
	boolean_t err = FALSE;
	uint16_t debug_port_io_base = MON_DEBUG_PORT_SERIAL_IO_BASE_DEFAULT;

	debug_port_handle = NULL;
	debug_port_type = MON_DEBUG_PORT_SERIAL;
	debug_port_virt_mode = MON_DEBUG_PORT_VIRT_HIDE;

	if (p_params) {
		/* valid parameters structure, use it */

		/* Only a serial debug port is currently supported.  Furtheremore, */
		/* only I/O-based serial port is supported. */

		if (p_params->type == MON_DEBUG_PORT_SERIAL) {
			debug_port_type = (mon_debug_port_type_t)p_params->type;

			switch (p_params->ident_type) {
			case MON_DEBUG_PORT_IDENT_IO:
				debug_port_io_base = p_params->ident.io_base;
				break;

			case MON_DEBUG_PORT_IDENT_DEFAULT:
				debug_port_io_base =
					MON_DEBUG_PORT_SERIAL_IO_BASE_DEFAULT;
				break;

			default:
				debug_port_io_base =
					MON_DEBUG_PORT_SERIAL_IO_BASE_DEFAULT;
				err = TRUE;
			}

			debug_port_virt_mode =
				(mon_debug_port_virt_mode_t)p_params->virt_mode;
		} else {
			/* No debug port */

			debug_port_type = MON_DEBUG_PORT_NONE;
			debug_port_virt_mode = MON_DEBUG_PORT_VIRT_NONE;
		}
	}

	if (debug_port_type == MON_DEBUG_PORT_SERIAL) {
		debug_port_handle = mon_serial_new(debug_port_io_base,
			UART_PROG_IF_DEFAULT,
			UART_HANDSHAKE_DEFAULT);
	}

	return err;
}

/*======================================================================== */

static
void mon_debug_port_init(void)
{
	if (debug_port_type == MON_DEBUG_PORT_SERIAL) {
		mon_serial_init(debug_port_handle);
	}
}

/*======================================================================== */

void mon_debug_port_clear(void)
{
	if (debug_port_type == MON_DEBUG_PORT_SERIAL) {
		mon_serial_reset(debug_port_handle);
	}
}

/*========================================================================
 *
 * Debug port info accessors
 *
 *======================================================================== */
static
mon_debug_port_type_t mon_debug_port_get_type(void)
{
	return debug_port_type;
}

mon_debug_port_virt_mode_t mon_debug_port_get_virt_mode(void)
{
	return debug_port_virt_mode;
}

/* If the debug port uses an I/O range, returns its base address.
 * Otherwise, returns 0 */
uint16_t mon_debug_port_get_io_base(void)
{
	uint16_t io_base = 0;
	uint16_t io_end = 0;

	if (debug_port_type == MON_DEBUG_PORT_SERIAL) {
		mon_serial_get_io_range(debug_port_handle, &io_base, &io_end);
	}

	return io_base;
}

/* If the debug port uses an I/O range, returns its end address.
 * Otherwise, returns 0 */
uint16_t mon_debug_port_get_io_end(void)
{
	uint16_t io_base = 0;
	uint16_t io_end = 0;

	if (debug_port_type == MON_DEBUG_PORT_SERIAL) {
		mon_serial_get_io_range(debug_port_handle, &io_base, &io_end);
	}

	return io_end;
}

/*======================= mon_debug_port_*_mux() =========================== */
/*
 * Multiplexers to debug port, according to its type (none, serial etc.).
 */
static
uint8_t mon_debug_port_getc(void)
{
	if (mon_debug_port_get_type() == MON_DEBUG_PORT_SERIAL) {
		return mon_serial_getc(debug_port_handle);
	} else {
		return 0;
	}
}

static
uint8_t mon_debug_port_putc_nolock(uint8_t ch)
{
	if (mon_debug_port_get_type() == MON_DEBUG_PORT_SERIAL) {
		return mon_serial_putc_nolock(debug_port_handle, ch);
	} else {
		return ch;
	}
}

static
uint8_t mon_debug_port_putc(uint8_t ch)
{
	if (mon_debug_port_get_type() == MON_DEBUG_PORT_SERIAL) {
		return mon_serial_putc(debug_port_handle, ch);
	} else {
		return ch;
	}
}

static
int mon_debug_port_puts_direct(boolean_t is_locked, const char *string)
{
	int ret = 1;

	if (mon_debug_port_get_type() == MON_DEBUG_PORT_SERIAL) {
		if (is_locked) {
			/* Print using the regular function */

			ret = mon_serial_puts(debug_port_handle, string);
		} else {
			/* Force lock, so that regular (locked) prints will not interfere */
			/* until we're done.  Note that here we may interfere with ongoing */
			/* regular prints - but this is the nature of "nolock". */

			raw_force_lock(&printf_lock);

			/* Print using the "nolock" function */

			ret = mon_serial_puts_nolock(debug_port_handle, string);

			/* Unlock */

			raw_unlock(&printf_lock);
		}
	}

	return ret;
}

/*======================= mon_debug_port_puts() =========================== */
/*
 * Writes a string to the debug port, according to its type (none, serial
 * etc.).
 * Takes care of the case where running as guest
 */
static
int mon_debug_port_puts(boolean_t is_locked, const char *string)
{
	int ret = 1;

	ret = mon_debug_port_puts_direct(is_locked, string);

	return ret;
}

/*=========================================================================
 *
 * Emulator debug support functions
 *
 *========================================================================= */
#ifdef DEBUG
static
mon_status_t mon_io_vmcall_puts_handler(guest_cpu_handle_t gcpu UNUSED,
					address_t *arg1, address_t *arg2 UNUSED,
					address_t *arg3 UNUSED)
{
	const char *string = (const char *)*arg1;

	raw_lock(&printf_lock);

	mon_debug_port_puts_direct(TRUE, string);

	raw_unlock(&printf_lock);

	return MON_OK;
}

#endif

/*=========================================================================
 *
 * Generic I/O Functions
 *
 *========================================================================= */
static
void printf_init(void)
{
}

static
int mon_printf_int(boolean_t use_lock, char *buffer, uint32_t buffer_size,
		   const char *format, va_list args)
{
	uint32_t printed_size = 0;

	if (use_lock) {
		raw_lock(&printf_lock);
	}

	printed_size = mon_vsprintf_s(buffer, buffer_size, format, args);

	if (printed_size && (printed_size != UINT32_ALL_ONES)) {
		printed_size = mon_debug_port_puts(use_lock, buffer);
	}

	if (use_lock) {
		raw_unlock(&printf_lock);
	}

	return printed_size;
}

static
int CDECL mon_printf_nolock_alloc_buffer(const char *format, va_list args)
{
	/* use buffer on the stack */
	char buffer[PRINTF_BUFFER_SIZE];

	return mon_printf_int(FALSE, buffer, PRINTF_BUFFER_SIZE, format, args);
}

/*
 *-------------- Interface functions ----------------------
 */
void mon_io_init(void)
{
	mon_debug_port_init();

	printf_init();
}

int mon_puts_nolock(const char *string)
{
	int ret = 1;

	ret = mon_debug_port_puts(FALSE, string);

	/* According to the spec, puts always ends with new line */

	if (ret != EOF) {
		mon_debug_port_puts(FALSE, "\n\r");
	}

	return ret;
}

int mon_puts(const char *string)
{
	int ret = 1;

	raw_lock(&printf_lock);

	ret = mon_debug_port_puts(TRUE, string);

	/* According to the spec, puts always ends with new line */

	if (ret != EOF) {
		mon_debug_port_puts(TRUE, "\n\r");
	}

	raw_unlock(&printf_lock);

	return ret;
}

uint8_t mon_getc(void)
{
	if (CLI_active()) {
		return mon_debug_port_getc();
	} else {
		return 0;
	}
}

uint8_t mon_putc_nolock(uint8_t ch)
{
	ch = mon_debug_port_putc_nolock(ch);

	return ch;
}

uint8_t mon_putc(uint8_t ch)
{
	raw_lock(&printf_lock);

	ch = mon_debug_port_putc(ch);

	raw_unlock(&printf_lock);

	return ch;
}

int CDECL mon_vprintf(const char *format, va_list args)
{
	/* use static buffer to save stack space */

	static char buffer[PRINTF_BUFFER_SIZE];

	return mon_printf_int(TRUE, buffer, PRINTF_BUFFER_SIZE, format, args);
}

int CDECL mon_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	return mon_vprintf(format, args);
}

#ifdef DEBUG
/* printf without taking any locks - use from NMI handlers */
int CDECL mon_printf_nolock(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	return mon_printf_nolock_alloc_buffer(format, args);
}

void mon_io_emulator_register(guest_id_t guest_id)
{
	mon_vmcall_register(guest_id,
		VMCALL_EMULATOR_PUTS,
		(vmcall_handler_t)mon_io_vmcall_puts_handler, FALSE);
}
#endif
