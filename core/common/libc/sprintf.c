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

#include "common_libc.h"

#define LEFT_JUSTIFY    0x01
#define PREFIX_SIGN     0x02
#define PREFIX_BLANK    0x04
#define COMMA_TYPE      0x08
#define LONG_TYPE       0x10
#define PREFIX_ZERO     0x20
#define UPHEX           0x40
#define UNSIGNED        0x80
#define ZERO_HEX_PREFIX 0x100

#if 8 == ARCH_ADDRESS_WIDTH
#define SIZE_T_SIZE_TYPE        LONG_TYPE
/* make it 4 also */
#define DEFAULT_POINTER_WIDTH   (sizeof(uint32_t) * 2)
#else
#define SIZE_T_SIZE_TYPE 0
#define DEFAULT_POINTER_WIDTH   (sizeof(uint32_t) * 2)
#endif

#define STRING_CHARS(a) (ARRAY_SIZE(a) - 1)
#define TIME_PLACEHOLDER "99/99/9999  99:99"
#define GUID_PLACEHOLDER "99999999-9999-9999-9999-999999999999"
#define MAX_NUMBER_CHARS 30

static const char up_hex[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'A', 'B', 'C', 'D', 'E', 'F' };

static const char low_hex[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'a', 'b', 'c', 'd', 'e', 'f' };

static const char decimal[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };

/* -------------------------- internal------------------------------------ */
/* ++
 * Routine Description:
 *     prints an mon_guid_t.
 * Arguments:
 *     Guid - Pointer to GUID to print.
 *     Buffer - Buffe to print Guid into.
 *     BufferSize - Size of Buffer.
 * Returns:
 *     Number of characters printed.
 * -- */
static
uint32_t safe_guid_to_string(mon_guid_t *guid, char *buffer, size_t buffer_size)
{
	uint32_t size;

	if (buffer_size <= STRING_CHARS(GUID_PLACEHOLDER)) {
		/* Not enough room for terminating null */
		return 0;
	}

	size = mon_sprintf_s(buffer, buffer_size,
		"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		guid->data1,
		guid->data2,
		guid->data3,
		guid->data4[0],
		guid->data4[1],
		guid->data4[2],
		guid->data4[3],
		guid->data4[4],
		guid->data4[5],
		guid->data4[6],
		guid->data4[7]
		);

	/* sprintf_s will null terminate the string. The -1 skips the null */
	return STRING_CHARS(GUID_PLACEHOLDER);
}

/* ++
 * Routine Description:
 *     worker function that prints a Value as a based number in Buffer
 * Arguments:
 *     Buffer - Location to place ascii based number string of Value.
 *     value - value to convert to a string in Buffer.
 *     flags - flags to use in printing decimal string,
 *             see file header for details.
 *     Width - Width of value.
 * Returns:
 *     Number of characters printed.
 * -- */
static
uint32_t safe_value_to_string(char *buffer,
			      /* total space in out buff */
			      uint32_t buffer_limits,
			      int64_t value,
			      uint32_t flags,
			      uint32_t width,
			      uint32_t base,
			      const char *chars)

{
	char temp_buffer[MAX_NUMBER_CHARS];
	char *temp_str;
	char *buffer_ptr;
	uint32_t count, comma_count, pre_count;
	uint32_t remainder;
	char prefix;
	uint32_t index;
	uint32_t actual_chars = buffer_limits - 1;
	uint64_t uvalue, temp_value;

	/* Sanity */
	if (buffer_limits == 0) {
		return 0;
	}

	temp_str = temp_buffer;
	buffer_ptr = buffer;
	count = 0;
	comma_count = 0;
	pre_count = 0;

	if (actual_chars) {
		if (!(flags & UNSIGNED)) {
			if (value < 0) {
				*(buffer_ptr++) = '-';
				value = -value;
				pre_count++;
			} else if (flags & PREFIX_SIGN) {
				*(buffer_ptr++) = '+';
				pre_count++;
			} else if (flags & PREFIX_BLANK) {
				*(buffer_ptr++) = ' ';
				pre_count++;
			}
		}
	}

	uvalue = (uint64_t)value;

	do {
		temp_value = uvalue / base;
		remainder = (uint32_t)(uvalue % base);

		uvalue = temp_value;

		*(temp_str++) = chars[remainder];

		count++;
		if ((flags & COMMA_TYPE) == COMMA_TYPE) {
			if ((count % 3 == 0) &&
			    (MAX_NUMBER_CHARS >
			     (count + comma_count + pre_count))) {
				if ((uvalue != 0)
				    && ((temp_str - temp_buffer + 1) <
					MAX_NUMBER_CHARS)) {
					*(temp_str++) = ',';
					comma_count++;
				}
			}
		}
	} while ((uvalue != 0) &&
		 ((temp_str - temp_buffer) < MAX_NUMBER_CHARS));

	if (flags & PREFIX_ZERO) {
		prefix = '0';
	} else if (!(flags & LEFT_JUSTIFY)) {
		prefix = ' ';
	} else {
		prefix = 0;
	}

	if (prefix != 0) {
		for (index = count + comma_count + pre_count;
		     index < MIN(width, MAX_NUMBER_CHARS); index++)
			*(temp_str++) = prefix;
	}

	/*
	 * Reverse temp string into Buffer.
	 */
	while ((temp_str != temp_buffer) && (buffer_limits-- > 0))
		*(buffer_ptr++) = *(--temp_str);

	*buffer_ptr = 0;
	return (uint32_t)(buffer_ptr - buffer);
}

static
uint32_t safe_value_to_decimal_str(char *buffer,
				   /* total space in out buff */
				   uint32_t buffer_limits,
				   int64_t value, uint32_t flags,
				   uint32_t width)
{
	return safe_value_to_string(buffer,
		buffer_limits,
		value, flags, width, 10, decimal);
}

static
uint32_t safe_value_to_hex_str(char *buffer,
			       /* total space in out buff */
			       uint32_t buffer_limits,
			       uint64_t value, uint32_t flags, uint32_t width)
{
	uint32_t prefix_size = 0;

	if ((flags & ZERO_HEX_PREFIX) && buffer && (buffer_limits > 2)) {
		*(buffer++) = '0';
		--buffer_limits;

		if (width != 0) {
			--width;
		}

		*(buffer++) = 'x';
		--buffer_limits;

		if (width != 0) {
			--width;
		}

		prefix_size = 2;
	}

	return prefix_size + safe_value_to_string(buffer,
		buffer_limits,
		value,
		flags | UNSIGNED,
		width,
		16,
		(flags & UPHEX) ? up_hex :
		low_hex);
}

/* ++
 * Routine Description:
 *     worker function that prints mon_time_t.
 * Arguments:
 *     Time - Pointer to mon_time_t sturcture to print.
 *     Buffer - Buffer to print Time into.
 *     BufferSize - Size of Buffer.
 * Returns: Number of characters printed.
 * -- */
static
uint32_t safe_time_to_string(mon_time_t *time,
			     char *buffer,
			     uint32_t buffer_size)

{
	uint32_t size;

	if (buffer_size <= STRING_CHARS(TIME_PLACEHOLDER)) {
		/* Not enough room for terminating null */
		return 0;
	}

	size = mon_sprintf_s(buffer, buffer_size,
		"%02d/%02d/%04d  %02d:%02d",
		time->day,
		time->month, time->year, time->hour, time->minute);

	/*
	 * SPrint will null terminate the string. The -1 skips the null
	 */
	return STRING_CHARS(TIME_PLACEHOLDER);
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
 *     Precision - Returns precision of element
 *     Marker - Vararg list that may be paritally consumed and returned.
 * Returns: Pointer indexed into the Format string for all the information
 *     parsed by this routine.
 * -- */
static
const char *get_flags_and_width_and_precision(const char *format,
					      uint32_t *flags,
					      uint32_t *width,
					      uint32_t *precision,
					      va_list marker
					      )
{
	uint32_t count;
	boolean_t done;
	boolean_t at_precision;
	boolean_t done_precision;

	*flags = 0;
	*width = 0;
	*precision = 0xFFFF;
	at_precision = FALSE;
	done_precision = FALSE;

	for (done = FALSE; !done; ) {
		format++;

		switch (*format) {
		case '-':
			*flags |= LEFT_JUSTIFY;
			break;
		case '+':
			*flags |= PREFIX_SIGN;
			break;
		case ' ':
			*flags |= PREFIX_BLANK;
			break;
		case ',':
			*flags |= COMMA_TYPE;
			break;
		case '#':
			*flags |= ZERO_HEX_PREFIX | PREFIX_ZERO;
			break;
		case 'L':
		case 'l':
			*flags |= LONG_TYPE;
			break;
		case 'I':
			*flags |= SIZE_T_SIZE_TYPE;
			break;

		case '*':
			if (at_precision) {
				*precision = va_arg(marker, uint32_t);
			} else {
				*width = va_arg(marker, uint32_t);
			}
			break;

		case '.':
			if (done_precision) {
				done = TRUE;
			} else {
				at_precision = TRUE;
				done_precision = TRUE;
				*precision = 0;
			}
			break;

		case '0':
			/* zero is at the number beginning */
			if (!at_precision) {
				*flags |= PREFIX_ZERO;
			}
			break;

		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			count = 0;
			do {
				count = (count * 10) + *format - '0';
				format++;
			} while ((*format >= '0') && (*format <= '9'));
			format--;
			if (at_precision) {
				*precision = count;
			} else {
				*width = count;
			}
			at_precision = FALSE;
			break;

		default:
			done = TRUE;
		}
	}
	return format;
}

/*------------------------- interface ------------------------------------- */
/* ++
 * Routine Description:
 *     vsprintf_s function to process format and place the
 * results in Buffer. Since a va_list is used this rountine allows the nesting
 * of Vararg routines. Thus this is the main print working routine
 * Arguments:
 *     start_of_buffer - buffer to print the results of the parsing of Format
 *                       into.
 *     size_of_buffer - Maximum number of characters to put into buffer
 *                      (including the terminating null).
 *     format_staring - Format string see file header for more details.
 *     argptr - Vararg list consumed by processing format.
 * Returns:
 *     Number of characters printed.
 * -- */
int CDECL mon_vsprintf_s(char *start_of_buffer,
			 size_t size_of_buffer,
			 const char *format_string,
			 va_list argptr)
{
	char *buffer;
	const char *format;
	char *ascii_str;
	uint32_t chars_written;
	uint32_t index;
	uint32_t flags;
	uint32_t width;
	uint32_t precision; /* BUGBUG: Precision is currently used only for strings */
	uint32_t count;
	uint64_t value;
	mon_guid_t *tmp_GUID;

	/*
	 * Reserve one place for the terminating null
	 */
	int32_t available_chars = (int32_t)(size_of_buffer - 1);

	if (!(start_of_buffer && size_of_buffer && format_string && argptr)) {
		return -1;
	}

	if (available_chars == 0) {
		/* there is only the place for null char */
		*start_of_buffer = '\0';
		return 0;
	}

	/*
	 * Process the format string. Stop if buffer is over run.
	 */
	buffer = start_of_buffer;
	format = format_string;
	for (index = 0; (*format != '\0') && (available_chars > 0); ++format) {
		if (*format != '%') {
			if (*format == '\n' && available_chars > 2) {
				/*
				 * If carriage return add line feed
				 */
				buffer[index++] = '\r';
				--available_chars;
			}
			buffer[index++] = *format;
			--available_chars;
		} else {
			/*
			 * Now it's time to parse what follows after %
			 */
			format = get_flags_and_width_and_precision(format,
				&flags,
				&width,
				&precision,
				argptr
				);
			switch (*format) {
			case 'X':
				/* HEX UPPER_CASE */
				flags |= UPHEX;
			/*
			 * break skiped on purpose
			 */
			case 'x':
				/* HEX LOWER CASE */
				if ((flags & LONG_TYPE) == LONG_TYPE) {
					value = va_arg(argptr, uint64_t);
				} else {
					value = (uint64_t)va_arg(argptr,
						uint32_t);
				}
				chars_written =
					safe_value_to_hex_str(&buffer[index],
						available_chars,
						value,
						flags,
						width);
				if (chars_written == 0) {
					break;
				}
				index += chars_written;
				available_chars -= chars_written;
				break;

			case 'P':
				/* POINTER LOWER CASE */
				flags |= UPHEX;
			/*
			 * break skiped on purpose
			 */
			case 'p':
				/* POINTER LOWER CASE */
				flags |= ZERO_HEX_PREFIX | PREFIX_ZERO |
					 SIZE_T_SIZE_TYPE;

				if (width == 0) {
					/* set default width */
					width = DEFAULT_POINTER_WIDTH + 2; /* 2 - sizeof "0x" */
				}

				if ((flags & LONG_TYPE) == LONG_TYPE) {
					value = va_arg(argptr, uint64_t);
				} else {
					value = (uint64_t)va_arg(argptr,
						uint32_t);
				}

				chars_written =
					safe_value_to_hex_str(&buffer[index],
						available_chars,
						value,
						flags,
						width);
				if (chars_written == 0) {
					break;
				}

				index += chars_written;
				available_chars -= chars_written;
				break;

			case 'u':
				/* UNSIGNED DECIMAL */
				flags |= UNSIGNED;
			/*
			 * break skiped on purpose
			 */
			case 'd':
			/* SIGNED DECIMAL */
			case 'i':
				/* SIGNED DECIMAL */
				if ((flags & LONG_TYPE) == LONG_TYPE) {
					value = va_arg(argptr, int64_t);
				} else {
					value = (uint64_t)(int64_t)va_arg(
						argptr,
						int32_t);
				}
				chars_written = safe_value_to_decimal_str(
					&buffer[index],
					available_chars,
					value,
					flags,
					width);
				if (chars_written == 0) {
					break;
				}
				index += chars_written;
				available_chars -= chars_written;
				break;

			case 's':
				/* ASCII STRING */
				ascii_str = (char *)va_arg(argptr, char *);
				if (ascii_str == NULL) {
					ascii_str = "<null string>";
				}
				for (count = 0;
				     (*ascii_str != '\0') &&
				     ((0 == width) || (count < width)) &&
				     (available_chars > 0);
				     ascii_str++, count++) {
					available_chars--;
					buffer[index++] = *ascii_str;
				}
				/*
				 * Add padding if needed
				 */
				for (;
				     (count < width) && (available_chars-- > 0);
				     count++)
					buffer[index++] = ' ';
				break;

			case 'c':
				/* ASCII CHAR */
				buffer[index++] = (char)va_arg(argptr, int);
				--available_chars;
				break;

			case 'g':
				/* mon_guid_t* */
				tmp_GUID = va_arg(argptr, mon_guid_t *);

				if (tmp_GUID != NULL) {
					chars_written = safe_guid_to_string(
						tmp_GUID,
						&buffer[index],
						available_chars);
					if (chars_written == 0) {
						break;
					}
					index += chars_written;
					available_chars -= chars_written;
				}
				break;

			case 't':
				/* mon_time_t* */
				chars_written =
					safe_time_to_string(va_arg(argptr,
							mon_time_t *),
						&buffer[index],
						available_chars);
				if (chars_written == 0) {
					break;
				}
				index += chars_written;
				available_chars -= chars_written;
				break;

			case '%':
				/* % */
				buffer[index++] = *format;
				--available_chars;
				break;

			default:
				/* unknown */
				/*
				 * if the type is unknown print it to the screen
				 */
				buffer[index++] = *format;
				--available_chars;
				break;
			}
		}
	} /* end of for loop */

	buffer[index] = '\0';
	return index;
}

int CDECL mon_sprintf_s(char *buffer,
			size_t size_of_buffer,
			const char *format,
			...)
{
	va_list marker;

	va_start(marker, format);

	return mon_vsprintf_s(buffer, size_of_buffer, format, marker);
}
