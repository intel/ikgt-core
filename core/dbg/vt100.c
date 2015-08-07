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

#include "vt100.h"
#include "mon_defs.h"
#include "libc.h"

typedef enum {
	LKUP_NOT_FOUND,
	LKUP_BELONG,
	LKUP_FOUND
} lkup_status_t;

#define MAX_ESC_CMD 7
struct {
	unsigned char	vt100_key;
	char		esc_cmd[MAX_ESC_CMD]; /* max of 7 chars ?? */
} supported_esq_cmds[] = {
	{ VT100_KEY_ABORT,	    { ASCII_ESCAPE, 0 } },
	{ VT100_KEY_UP,		    { '[',	    'A', 0,} },
	{ VT100_KEY_DOWN,	    { '[',	    'B', 0} },
	{ VT100_KEY_RIGHT,	    { '[',	    'C', 0} },
	{ VT100_KEY_LEFT,	    { '[',	    'D', 0} },
	{ VT100_KEY_HOME,	    { '[',	    '2', '~', 0} },
	{ VT100_KEY_END,	    { '[',	    '5', '~', 0} },
	{ VT100_KEY_DELETE,	    { '[',	    '4', '~', 0} },
	{ VT100_KEY_INSERT,	    { '[',	    '1', '~', 0} },
};

static char input_buffer[8];
static unsigned input_buffer_length;

static lkup_status_t strncmp_local(char *string1, char *string2, int len2);
static lkup_status_t lkup_escape_cmd(char *esc_cmd,
				     unsigned esc_cmd_len,
				     char *key);

char vt100_getch_raw(void)
{
	char key;

	while (0 == (key = (unsigned char)mon_getc()))
		;
	return key;
}

void vt100_putch(char ch)
{
	mon_putc((uint8_t)ch);
}

void vt100_puts(char *string)
{
	mon_puts(string);
}

void vt100_gotoxy(unsigned line, unsigned col)
{
	char cup_cmd[] = { ASCII_ESCAPE, '[', 0, 0, ';', 0, 0, 'H', 0 };

	cup_cmd[2] = (char)(line / 10 + '0');
	cup_cmd[3] = (char)(line % 10 + '0');
	cup_cmd[5] = (char)(col / 10 + '0');
	cup_cmd[6] = (char)(col % 10 + '0');

	vt100_puts(cup_cmd);
}

char read_dec_number(unsigned *number)
{
	unsigned num = 0;
	char key;

	for (key = vt100_getch_raw(); key >= '0' && key <= '9';
	     key = vt100_getch_raw())
		num = num * 10 + key - '0';
	*number = num;
	return key;
}

int vt100_getpos(unsigned *line, unsigned *col)
{
	char dsr_cmd[] = { ASCII_ESCAPE, '[', '6', 'n', 0 };
	char key;
	int error = 1;

	vt100_puts(dsr_cmd);

	/* Response is: ESC [ Pl ; Pc R */

	do {
		key = vt100_getch_raw();
		if (key != ASCII_ESCAPE) {
			break;
		}

		key = vt100_getch_raw();
		if (key != '[') {
			break;
		}

		key = read_dec_number(line);
		if (key != ';') {
			break;
		}

		key = read_dec_number(col);
		if (key != 'R') {
			break;
		}

		error = 0;
	} while (0);

	return error;
}

void vt100_clrline(void)
{
	vt100_putch(ASCII_ESCAPE);
	vt100_putch('[');
	vt100_putch('0');
	vt100_putch('K');
}

void vt100_delsym(void)
{
	vt100_putch(' ');
	vt100_putch(8);
}

lkup_status_t strncmp_local(char *string1, char *string2, int len2)
{
	int i;

	for (i = 0; i < len2; ++i)
		if (string1[i] != string2[i]) {
			return LKUP_NOT_FOUND;
		}

	return 0 == string1[len2] ? LKUP_FOUND : LKUP_BELONG;
}

lkup_status_t lkup_escape_cmd(char *esc_cmd, unsigned esc_cmd_len, char *key)
{
	int i;
	lkup_status_t cmp = LKUP_NOT_FOUND;

	/* Limit the esc_cmd_len to 7 since the max size of
	 * supported_esq_cmds[i].esc_cmd is 7.  */
	if (esc_cmd_len > MAX_ESC_CMD - 1) {
		esc_cmd_len = MAX_ESC_CMD - 1;
	}

	for (i = 0; i < NELEMENTS(supported_esq_cmds); ++i) {
		cmp =
			strncmp_local(supported_esq_cmds[i].esc_cmd,
				esc_cmd,
				esc_cmd_len);
		if (LKUP_NOT_FOUND != cmp) {
			*key = supported_esq_cmds[i].vt100_key;
			break;
		}
	}
	return cmp;
}

char vt100_getch(void)
{
	char key = 0;
	boolean_t quit = FALSE;
	unsigned i;

	if (input_buffer_length > 0) {
		input_buffer_length--;
		return input_buffer[input_buffer_length];
	}

	while (!quit) {
		key = vt100_getch_raw();

		if (0 == input_buffer_length) {
			if (ASCII_ESCAPE == key) {
				input_buffer_length = 1;
			} else {
				quit = TRUE;
			}
		} else {
			input_buffer[input_buffer_length - 1] = key;

			switch (lkup_escape_cmd(input_buffer,
					input_buffer_length, &key)) {
			case LKUP_FOUND:
				quit = TRUE;
				input_buffer_length = 0;
				break;
			case LKUP_NOT_FOUND:
				key = input_buffer[0];
				input_buffer_length--;
				for (i = 0; i < input_buffer_length; ++i)
					input_buffer[i] = input_buffer[i + 1];
				break;
			default:
				/* continue matching escape sequence */
				input_buffer_length++;
				break;
			}
		}
	}

	return key;
}

void vt100_flush_input(void)
{
	input_buffer_length = 0;
}
