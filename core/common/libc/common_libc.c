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
 *
 * commonly used libc utilities
 *
 */

#include "common_libc.h"

extern void mon_lock_xchg_byte(uint8_t *dst, uint8_t *src);

void *CDECL mon_memset(void *dest, int filler, size_t count)
{
	size_t i = 0, j, cnt_64bit;
	uint64_t filler_64;
	uint64_t *fill = &filler_64;

	cnt_64bit = count >> 3;

	if (cnt_64bit) {
		if (filler != 0) {
			*(uint8_t *)fill = (uint8_t)filler;
			*((uint8_t *)fill + 1) = (uint8_t)filler;
			*((uint16_t *)fill + 1) = *(uint16_t *)fill;
			*((uint32_t *)fill + 1) = *(uint32_t *)fill;
		}

		for (i = 0; i < cnt_64bit; i++) {
			if (filler == 0) {
				((uint64_t *)dest)[i] = 0;
			} else {
				((uint64_t *)dest)[i] = filler_64;
			}
		}
		i = i << 3;
	}

	for (j = i; j < count; j++)
		((uint8_t *)dest)[j] = (uint8_t)filler;

	return dest;
}


void *CDECL mon_memcpy_ascending(void *dest, const void *src, size_t count)
{
	size_t i = 0, j, cnt_64bit;
	uint64_t *d = (uint64_t *)dest;
	const uint64_t *s = (const uint64_t *)src;

	cnt_64bit = count >> 3;

	if (cnt_64bit) {
		for (i = 0; i < cnt_64bit; i++)
			((uint64_t *)d)[i] = ((uint64_t *)s)[i];

		i = i << 3;
	}

	for (j = i; j < count; j++)
		((uint8_t *)dest)[j] = ((uint8_t *)src)[j];

	return dest;
}

void *CDECL mon_memcpy_descending(void *dest, const void *src, size_t count)
{
	size_t i, cnt, rem;
	MON_LONG *d = (MON_LONG *)dest;
	const MON_LONG *s = (const MON_LONG *)src;

	cnt = COUNT_32_64(count);
	rem = REMAINDER_32_64(count);

	for (i = 0; i < rem; i++)
		((uint8_t *)d)[count - i - 1] = ((uint8_t *)s)[count - i - 1];

	if (cnt) {
		for (i = cnt; i > 0; i--)
			((MON_LONG *)d)[i - 1] = ((MON_LONG *)s)[i - 1];
	}

	return dest;
}

void *CDECL mon_memcpy(void *dest, const void *src, size_t count)
{
	if (dest >= src) {
		return mon_memcpy_descending(dest, src, count);
	} else {
		return mon_memcpy_ascending(dest, src, count);
	}
}

size_t CDECL mon_strlen(const char *string)
{
	size_t len = 0;
	const char *next = string;

	if (!string) {
		return SIZE_T_ALL_ONES;
	}

	for (; *next != 0; ++next)
		++len;

	return len;
}

char *CDECL mon_strcpy(char *dst, const char *src)
{
	if (!src || !dst) {
		return NULL;
	}

	while ((*dst++ = *src++) != 0) {
	}

	return dst;
}

char *CDECL mon_strcpy_s(char *dst, size_t dst_length, const char *src)
{
	size_t src_length = mon_strlen(src);
	const char *s = src;

	if (!src || !dst || !dst_length || dst_length < src_length + 1) {
		return NULL;
	}

	while (*s != 0)
		*dst++ = *s++;

	*dst = '\0';

	return dst;
}

uint32_t CDECL mon_strcmp(const char *string1, const char *string2)
{
	const char *str1 = string1;
	const char *str2 = string2;

	if (str1 == str2) {
		return 0;
	}

	if (NULL == str1) {
		return (uint32_t)-1;
	}

	if (NULL == str2) {
		return 1;
	}

	while (*str1 == *str2) {
		if ('\0' == *str1) {
			break;
		}
		str1++;
		str2++;
	}

	return *str1 - *str2;
}

int CDECL mon_memcmp(const void *mem1, const void *mem2, size_t count)
{
	const char *m1 = mem1;
	const char *m2 = mem2;

	while (count) {
		count--;
		if (m1[count] != m2[count]) {
			break;
		}
	}

	return m1[count] - m2[count];
}

void CDECL mon_memcpy_assuming_mmio(uint8_t *dst, uint8_t *src, int32_t count)
{
	switch (count) {
	case 0:
		break;

	case 1:
		*dst = *src;
		break;

	case 2:
		*(uint16_t *)dst = *(uint16_t *)src;
		break;

	case 4:
		*(uint32_t *)dst = *(uint32_t *)src;
		break;

	case 8:
		*(uint64_t *)dst = *(uint64_t *)src;
		break;

	case 16:
		*(uint64_t *)dst = *(uint64_t *)src;
		dst += sizeof(uint64_t);
		src += sizeof(uint64_t);
		*(uint64_t *)dst = *(uint64_t *)src;
		break;

	default:
		mon_memcpy(dst, src, count);
		break;
	}
}

/******************* Locked versions of functions ***********************/

/*
 * NOTE: Use mon_lock_memcpy with caution. Although it is a locked version of
 * memcpy, it locks only at the DWORD level. Users need to implement their
 * own MUTEX LOCK to ensure other processor cores don't get in the way.
 * This copy only ensures that at DWORD level there are no synchronization
 * issues.
 */
void *CDECL mon_lock_memcpy_ascending(void *dest, const void *src, size_t count)
{
	size_t i = 0, j, cnt;
	MON_LONG *d = (MON_LONG *)dest;
	const MON_LONG *s = (const MON_LONG *)src;

	cnt = COUNT_32_64(count);

	if (cnt) {
		for (i = 0; i < cnt; i++)
			mon_lock_xchg_32_64_word(&((MON_LONG *)d)[i],
				&((MON_LONG *)s)[i]);

		i = SHL_32_64(i);
	}

	for (j = i; j < count; j++)
		mon_lock_xchg_byte(&((uint8_t *)dest)[j], &((uint8_t *)src)[j]);


	return dest;
}

void *CDECL mon_lock_memcpy_descending(void *dest, const void *src,
				       size_t count)
{
	size_t i, cnt, rem;
	MON_LONG *d = (MON_LONG *)dest;
	const MON_LONG *s = (const MON_LONG *)src;

	cnt = COUNT_32_64(count);
	rem = REMAINDER_32_64(count);

	for (i = 0; i < rem; i++)
		mon_lock_xchg_byte(&((uint8_t *)d)[count - i - 1],
			&((uint8_t *)s)[count - i - 1]);

	if (cnt) {
		for (i = cnt; i > 0; i--)
			mon_lock_xchg_32_64_word(&((MON_LONG *)d)[i - 1],
				&((MON_LONG *)s)[i - 1]);
	}

	return dest;
}

/*
 * NOTE: READ THE NOTE AT BEGINNING OF Locked Versions of functions.
 */
void *CDECL mon_lock_memcpy(void *dest, const void *src, size_t count)
{
	if (dest >= src) {
		return mon_lock_memcpy_descending(dest, src, count);
	} else {
		return mon_lock_memcpy_ascending(dest, src, count);
	}
}
