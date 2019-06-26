/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "vmm_base.h"

#include "lib/print.h"
#include "lib/serial.h"
#include "lib/string.h"

#define PRINTF_BUFFER_SIZE 256

static uint64_t serial_base;

void print_init(boolean_t setup)
{
	serial_base = get_serial_base();
	if (setup) {
		serial_init(serial_base);
	}
}
/*caller must make sure this function is NOT
called simultaneously in different cpus*/
void printf(const char *format, ...)
{
	uint32_t printed_size;
	/* use static buffer to save stack space */
	va_list args;
	char buffer[PRINTF_BUFFER_SIZE];

	va_start(args, format);
	printed_size = vmm_vsprintf_s(buffer, PRINTF_BUFFER_SIZE, format, args);
	va_end(args);
	if (printed_size > 0)
		serial_puts(buffer, serial_base);
}

