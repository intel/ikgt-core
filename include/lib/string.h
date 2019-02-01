/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _SPRINTF_H_
#define _SPRINTF_H_

#include "vmm_base.h"

/* maximum string length */
#define MAX_STR_LEN 0x1000

/* The strnlen_s function computes the length of the string pointed to by str */
uint32_t strnlen_s (const char *str, uint32_t maxlen);

/* The strstr_s function locates the first occurrence of the substring pointed
 * by str2 which would be located in the string pointed by str1 */
const char *strstr_s (const char *str1, uint32_t maxlen1, const char *str2, uint32_t maxlen2);

/* The str2uint() function convert a string to an unsigned integer */
uint32_t str2uint(const char *str, uint32_t maxlen, const char **endptr, uint32_t base);

#ifdef __GNUC__
#define va_list        __builtin_va_list
#define va_start(ap,v) __builtin_va_start((ap),v)
#define va_arg(ap,t)   __builtin_va_arg(ap,t)
#define va_end(ap)     __builtin_va_end(ap)
#else
/*
 *  find size of parameter aligned on the native integer size
 */

//#define VA_ARG_SIZE(type)  ALIGN_F(sizeof(type), sizeof(uint64_t))
/* it is always 8 for int,char,long,longlong,pointer type */
#define VA_ARG_SIZE(type) 8

typedef char *va_list;
#define va_start(ap,v) (ap = (va_list)&(v) + VA_ARG_SIZE(v))
#define va_arg(ap,t) (*(t *)((ap += VA_ARG_SIZE(t)) - VA_ARG_SIZE(t)))
#define va_end(ap)   (ap = (va_list)0)
#endif		/* #ifdef __GNUC__ */

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
			 va_list argptr);

uint32_t vmm_sprintf_s(char *buffer_start,
			 uint32_t buffer_size,
			 const char *format,
			...);
#endif
