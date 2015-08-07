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

#ifndef _CLI_ENV_H_
#define _CLI_ENV_H_

#ifdef CLI_INCLUDE
#define CLI_CODE(__xxx) __xxx
#else
#define CLI_CODE(__xxx)
#endif

#include "mon_defs.h"
#include "cli_libc.h"
#include "memory_allocator.h"
#include "lock.h"
#include "hw_utils.h"
#include "vt100.h"

#define STRING_PRINT_FORMAT                         "s"
#define CLI_MEMFREE(__buffer)
#define CLI_MEMALLOC(__bytes)                       mon_malloc(__bytes)

#define CLI_READ_CHAR   vt100_getch
#define CLI_PRINT_CHAR  vt100_putch
#define CLI_FLUSH_INPUT vt100_flush_input

#define CLI_PRINT(...) \
	MON_LOG(mask_cli, level_print_always, __VA_ARGS__)
#define CLI_STRLEN(__string)  mon_strlen(__string)
#define CLI_STRCMP(__string1, __string2)  \
	CLI_strcmp(__string1, __string2)
#define CLI_STRNCMP(__string1, __string2, __n) \
	CLI_strncmp(__string1, __string2, __n)
#define CLI_MEMSET(__buffer, __filler, __size) \
	mon_memset(__buffer, __filler, __size)
#define CLI_MEMCPY(__dst, __src, __size) \
	mon_memcpy(__dst, __src, __size)

#define CLI_ATOL(__string)                                                     \
	((__string[0] == '0' && (__string[1] == 'x' || __string[1] == 'X')) ?      \
	 cli_atol32(__string + 2, 16, NULL) :                                       \
	 cli_atol32(__string, 10, NULL))

#define CLI_ATOL64(__string)                                                   \
	((__string[0] == '0' && (__string[1] == 'x' || __string[1] == 'X')) ?      \
	 cli_atol64(__string + 2, 16, NULL) :                                       \
	 cli_atol64(__string, 10, NULL))
#define CLI_GET_CPU_ID()  hw_cpu_id()
#define CLI_IS_SUBSTR(__bigstring, __smallstring) \
	CLI_is_substr(__bigstring, __smallstring)

typedef enum {
	LED_KEY_CR = VT100_KEY_CR,
	LED_KEY_LN = VT100_KEY_LN,
	LED_KEY_ABORT = VT100_KEY_ABORT,
	LED_KEY_UP = VT100_KEY_UP,
	LED_KEY_DOWN = VT100_KEY_DOWN,
	LED_KEY_LEFT = VT100_KEY_LEFT,
	LED_KEY_RIGHT = VT100_KEY_RIGHT,
	LED_KEY_HOME = VT100_KEY_HOME,
	LED_KEY_END = VT100_KEY_END,
	LED_KEY_DELETE = VT100_KEY_DELETE,
	LED_KEY_RUBOUT = VT100_KEY_RUBOUT,
	LED_KEY_INSERT = VT100_KEY_INSERT
} ctrl_key_t;

/* locks */
#define CLI_LOCK                                    mon_lock_t
#define CLI_LOCK_INIT                               lock_initialize
#define CLI_LOCK_ACQUIRE                            interruptible_lock_acquire
#define CLI_LOCK_RELEASE                            lock_release

/* Note, CLI_ACCESS_LEVELs must be powers of 2 */
#define CLI_ACCESS_LEVEL_SYSTEM     0x00000001
#define CLI_ACCESS_LEVEL_USER       0x00000002

#endif      /* _CLI_ENV_H_ */
