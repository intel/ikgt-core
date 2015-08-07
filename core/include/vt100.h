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

#ifndef _VT100_H_
#define _VT100_H_

#define ASCII_ESCAPE 0x1B

#define VT100_KEY_CR        ('\r')
#define VT100_KEY_LN        ('\n')
#define VT100_KEY_RUBOUT    8
#define VT100_KEY_ABORT     ASCII_ESCAPE
#define VT100_KEY_UP        1
#define VT100_KEY_DOWN      2
#define VT100_KEY_LEFT      3
#define VT100_KEY_RIGHT     4
#define VT100_KEY_HOME      5
#define VT100_KEY_END       6
#define VT100_KEY_DELETE    7
#define VT100_KEY_INSERT    9

void vt100_putch(char ch);
void vt100_puts(char *string);
void vt100_gotoxy(unsigned line, unsigned col);
int vt100_getpos(unsigned *line, unsigned *col);
void vt100_delsym(void);
void vt100_clrline(void);
char vt100_getch(void);
void vt100_flush_input(void);

#endif                          /* _VT100_H_ */
