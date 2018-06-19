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

/*
 * sprintf implementation
 */

#include "vmm_base.h"
#include "lib/util.h"
#include "lib/string.h"

#define HEX_TYPE        0x01
#define LONG_TYPE       0x02
#define PREFIX_ZERO     0x04
#define UPHEX           0x08
#define SIGNED          0x10

#define DEFAULT_POINTER_WIDTH   8
/* 0xFFFFFFFFFFFFFFFFULL = 1.8*10^19 */
#define MAX_NUMBER_CHARS 19

/* ++
 * Routine Description:
 *     convert uint64_t value to string.
 * Arguments:
 *     buffer_start - the start location of the string that should be placed.
 *     buffer_size - buffer size
 *     value - uint64_t value that convert to string
 *     flags - flags
 *     Width - width of string
 * Returns:
 *     count of the written bytes
 * Notes:
 *     if width is less than the actual length of the value, the width is ignored.
 *     others, write '0' or ' ' on the left of the string according to flags.
 * -- */
static
uint32_t ull2str(char *buffer_start, uint32_t buffer_size, uint64_t value, uint32_t flags, uint32_t width)
{
	uint32_t base;
	char prefix;
	uint32_t actual_len = 0;
	char temp_buffer[MAX_NUMBER_CHARS];
	char *temp_ptr = temp_buffer;
	char *buffer = buffer_start;
	uint64_t uvalue = value;
	uint64_t remainder;
	long long sint64;
	int sint32;
	uint32_t i;

	if (buffer_size == 0){
		return 0;
	}

	if (flags & HEX_TYPE) {
		base = 16;
	}else {
		base = 10;
	}

	if (flags & PREFIX_ZERO) {
		prefix = '0';
	}else {
		prefix = ' ';
	}

	if (flags & SIGNED) {
		if (flags & LONG_TYPE) {
			sint64 = (long long)uvalue;
			if (sint64 < 0) {
				*(buffer++) = '-';
				uvalue = (uint64_t)-sint64;
				actual_len++;
				buffer_size--;
			}
		}else {
			sint32 = (int)uvalue;
			if (sint32 < 0) {
				*(buffer++) = '-';
				uvalue = (uint64_t)-sint32;
				actual_len++;
				buffer_size--;
			}
		}
	}

	do {
		remainder = (uint64_t)(uvalue % base);
		uvalue = uvalue / base;

		if (remainder > 9) {
			if (flags & UPHEX) {
				*(temp_ptr++) = remainder + 'A' - 10;
			}else {
				*(temp_ptr++) = remainder + 'a' - 10;
			}
		}else {
			*(temp_ptr++) = remainder + '0';
		}
		actual_len++;
	}while (uvalue != 0);/* temp buffer will never overflow here, see comments of MAX_NUMBER_CHARS */

	for (i = actual_len; i < width; i++) {
		if (buffer_size == 0)
			break;

		*(buffer++) = prefix;
		buffer_size--;
	}

	while (temp_ptr != temp_buffer) {
		if (buffer_size == 0)
			break;

		*(buffer++) = *(--temp_ptr);
		buffer_size--;
	}

	return (uint32_t)(buffer - buffer_start);
}

/* ++
 * Routine Description:
 *     worker function that parses flag and width information from the
 *     Format string and returns the next index into the Format string that
 *     needs to be parsed. See file headed for details of Flag and Width.
 * Arguments:
 *     Format - Current location in the VSPrint format string.
 *     flags - Returns flags
 *     Width - Returns width of element
 * Returns: Pointer indexed into the Format string for all the information
 *     parsed by this routine.
 * -- */
static
const char *get_flags_and_width(const char *format, uint32_t *flags, uint32_t *width)
{
	const char *endptr;

	*flags = 0;
	*width = str2uint(format, 32, &endptr, 10);

	if (*width == (uint32_t)-1) {
		*width = 0;
	}

	if (*format == '0') {
		*flags |= PREFIX_ZERO;
	}

	for (; *endptr == 'l'; endptr++) {
		*flags |= LONG_TYPE;
	}

	return endptr;
}

static inline uint64_t get_value(va_list argptr, uint32_t flags)
{
	if (flags & LONG_TYPE) {
		return va_arg(argptr, uint64_t);
	} else {
		return (uint64_t)va_arg(argptr, uint32_t);
	}
}

/* ++
 * Routine Description:
 *     string copy with format
 * Arguments:
 *     buffer_start - the start location of the string that should be placed.
 *     buffer_size - buffer size
 *     width - width of string
 *     str - source string.
 * Returns:
 *     count of the written bytes.
 * Notes:
 *     if width is less than the length of string, the string will be truncated,
 *     others, write ' ' on the right of the string according to width
 * -- */

static uint32_t str2str(char *buffer_start, uint32_t buffer_size, uint32_t width, const char *str)
{
	uint32_t to_copy, to_set;
	uint32_t len;

	len = strnlen_s(str, buffer_size);
	if (width == 0) {
		width = len;
	}

	to_copy = MIN(len, width);
	memcpy(buffer_start, str, to_copy);
	/*
	 * Add padding if needed
	 */

	to_set = MIN(width, buffer_size) - to_copy;
	memset(buffer_start + to_copy, ' ', to_set);

	return to_copy + to_set;
}

/* ++
 * Routine Description:
 *     parse the format of the parameter, convert to string and write to
 *     buffer.
 * Arguments:
 *     buffer_start - the start location of the string that should be placed.
 *     buffer_size - buffer size
 *     format_start - pointer to '%'
 *     written_count - output count of the written bytes
 *     argptr - va_list.
 * Returns:
 *     pointer that point next char needed to parse.
 * -- */
static const char *parse_format(char *buffer_start, uint32_t buffer_size, const char *format_start, uint32_t *written_count, va_list argptr)
{
	const char *format;
	const char *ascii_str;
	uint32_t flags;
	uint32_t width;
	uint64_t value;
	uint32_t prefix_len;
	uint32_t count = 0;
	/*
	 * Now it's time to parse what follows after %
	 */
	format = format_start;
	format++;
	format = get_flags_and_width(format, &flags, &width);
	switch (*format) {
		case 's':
			ascii_str = (const char *)va_arg(argptr, char *);
			if (ascii_str == NULL) {
				ascii_str = "<null string>";
			}
			count = str2str(buffer_start, buffer_size, width, ascii_str);
			break;

		case 'c':
			/* ASCII CHAR */
			*buffer_start = (char)va_arg(argptr, int);
			count = 1;
			break;

		case '%':
			/* % */
			*buffer_start = '%';
			count = 1;
			break;

		case 'd':
			/* UNSIGNED DECIMAL */
			flags |= SIGNED;
		case 'u':
			value = get_value(argptr, flags);
			count = ull2str(buffer_start, buffer_size, value, flags, width);
			break;

		case 'X':
			flags |= UPHEX;
		case 'x':
			flags |= HEX_TYPE;
			value = get_value(argptr, flags);
			count = ull2str(buffer_start, buffer_size, value, flags, width);
			break;

		case 'P':
			/* POINTER LOWER CASE */
			flags |= UPHEX;
		case 'p':
			flags |= PREFIX_ZERO | LONG_TYPE | HEX_TYPE;

			if (width < DEFAULT_POINTER_WIDTH + 2) {
				/* set default width */
				width = DEFAULT_POINTER_WIDTH + 2; /* 2 - sizeof "0x" */
			}
			prefix_len = MIN(buffer_size, 2);
			memcpy(buffer_start, "0x", prefix_len);
			count = prefix_len;

			if (buffer_size < 2) {
				break;
			}
			width -= 2;
			buffer_size -= 2;
			buffer_start += 2;
			value = get_value(argptr, flags);
			count += ull2str(buffer_start, buffer_size, value, flags, width);
			break;

		default :
			/* unknown */
			/*
			 * if the type is unknown print it to the screen
			 */

			*buffer_start = '%';
			count = 1;
			format = format_start;
			break;

	}

	*written_count = count;
	format++;
	return format;
}

/*------------------------- interface ------------------------------------- */
/* ++
 * Routine Description:
 *     vsprintf_s function to process format and place the
 * results in Buffer. Since a va_list is used this rountine allows the nesting
 * of Vararg routines. Thus this is the main print working routine
 * Arguments:
 *     buffer_start - buffer to print the results of the parsing of Format
 *                       into.
 *     buffer_size - Maximum number of characters to put into buffer
 *                      (including the terminating null).
 *     format - Format string see file header for more details.
 *     argptr - Vararg list consumed by processing format.
 * Returns:
 *     Number of characters printed.
 * Notes:
 * Format specification
 *
 * Types:
 * 	%X - hex uppercase unsigned integer
 * 	%x - hex lowercase unsigned integer
 * 	%P - hex uppercase unsigned integer, 32bit on x86 and 64bit on em64t
 *       default width - 10 chars, starting with '0x' prefix and including
 *       leading zeroes
 * 	%p - hex lowercase unsigned integer, 32bit on x86 and 64bit on em64t
 *       default width - 10 chars, starting with '0x' prefix and including
 *       leading zeroes
 * 	%u - unsigned decimal
 * 	%d - signed decimal
 * 	%s - ascii string
 * 	%c - ascii char
 * 	%% - %
 *
 * Width and flags:
 * 	'l' - 64bit integer, ex: "%lx"
 * 	'0' - print leading zeroes
 * 	positive_number - field width
 *
 * Width and flags\types  %X/%x  %P/%p  %u  %d  %s  %c  %%
 * 'l'                      o      x    o   o   x   x   x
 * '0'                      o      o    o   o   x   x   x
 * positive_number          o      o    o   o   o   x   x
 * sample:
 * ("%016lx", 0x1000): 0000000000001000
 * ("%06d", -1000): -01000
 * ("%016p", 0x1000):  0x00000000001000
 * ("%5s", "strings"): strin
 * ("%8x", 0x1000): ' '' '' '' '1000
 *--------------------------------------------------------------------------- */
uint32_t vmm_vsprintf_s(char *buffer_start,
		uint32_t buffer_size,
		const char *format,
		va_list argptr)
{
	uint32_t written_count;
	uint32_t index = 0;

	/*
	 * Reserve one place for the terminating null
	 */

	if (!(buffer_start && buffer_size && format && argptr)) {
		return 0;
	}

	if (buffer_size == 1) {
		/* there is only the place for null char */
		*buffer_start = '\0';
		return 0;
	}
	/* leave 1 char for '\0' */
	buffer_size--;
	/*
	 * Process the format string. Stop if buffer is over run.
	 */
	while ((*format != '\0') && (index < buffer_size)) {
		if (*format != '%') {
			if (*format == '\n' && (buffer_size - index) > 1) {
				/*
				 * If carriage return add line feed
				 */
				buffer_start[index++] = '\r';
			}
			buffer_start[index++] = *format;
			format++;
		} else {
			format = parse_format(&buffer_start[index], buffer_size - index, format, &written_count, argptr);
			index += written_count;
		}

	}/* end of for loop */
	buffer_start[index] = '\0';
	return index;
}

uint32_t vmm_sprintf_s(char *buffer_start,
		uint32_t buffer_size,
		const char *format,
		...)
{
	va_list marker;
	uint32_t byte_print;

	va_start(marker, format);
	byte_print = vmm_vsprintf_s(buffer_start, buffer_size, format, marker);
	va_end(marker);

	return byte_print;
}

