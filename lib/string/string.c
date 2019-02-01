/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "lib/util.h"

/* Only valid for digits and letters:
 * '0'~'9': 0x30~0x39
 * 'A'~'Z': 0x41~0x5A
 * 'a'~'z': 0x61~0x7A */
#define TOLOWER(x) ((x) | 0x20)

static uint32_t to_digit(char character, uint32_t base)
{
	uint32_t value = (uint32_t)-1;
	char l_char = TOLOWER(character);

	if (l_char >= '0' && l_char <= '9')
		value = l_char - '0';
	else if (l_char >= 'a' && l_char <= 'f')
		value = 10 + l_char - 'a';

	return value < base ? value : (uint32_t)-1;
}

/*
 * FUNCTION NAME: strnlen_s
 *
 * DESCRIPTION  : The strnlen_s function computes the length of the string pointed
 *                to by str.
 *
 * ARGUMENTS    :
 *     IN :
 *          str       pointer to string
 *          maxlen    restricted maximum length.
 *     OUT:
 *          none.
 *
 * RETURN       : The function returns the string length, excluding  the terminating
 *                null character.  If str is NULL, then strnlen_s returns 0.
 *
 *                Otherwise, the strnlen_s function returns the number of characters
 *                that precede the terminating null character. If there is no null
 *                character in the first maxlen characters of str then strnlen_s returns
 *                maxlen. At most the first maxlen characters of str are accessed
 *                by strnlen_s.
 *
 */
uint32_t strnlen_s (const char *str, uint32_t maxlen)
{
	uint32_t count;

	if (str == NULL) {
		return 0;
	}

	count = 0;
	while (*str && maxlen) {
		count++;
		maxlen--;
		str++;
	}

	return count;
}

/*
 * FUNCTION NAME: strstr_s
 *
 * DESCRIPTION  : The strstr_s() function locates the first occurrence of the
 *                substring pointed to by str2 which would be located in the
 *                string pointed to by str1.
 *
 * ARGUMENTS    :
 *     IN :
 *          str1       pointer to string to be searched for the substring
 *          maxlen1    restricted maximum length of str1
 *          str2       pointer to the sub string
 *          maxlen2    the maximum number of characters to copy from str2
 *     OUT:
 *          none.
 *
 * RETURN       : The first occurrence of the substring pointed to by str2
 *
 */
const char *strstr_s (const char *str1, uint32_t maxlen1, const char *str2, uint32_t maxlen2)
{
	uint32_t len1, len2;
	uint32_t i;

	if ((str1 == NULL) || (str2 == NULL)) {
		return NULL;
	}

	if ((maxlen1 == 0) || (maxlen2 == 0)) {
		return NULL;
	}

	len1 = strnlen_s(str1, maxlen1);
	len2 = strnlen_s(str2, maxlen2);

	if (len1 == 0)
		return NULL;

	/*
	 * str2 points to a string with zero length, or
	 * str2 equals str1, return str1
	 */
	if (len2 == 0 || str1 == str2)
		return str1;

	while (len1 >= len2) {
		for (i=0; i<len2; i++) {
			if (str1[i] != str2[i])
				break;
		}
		if (i == len2)
			return str1;
		str1++;
		len1--;
	}

	/*
	 * substring was not found, return NULL
	 */
	return NULL;
}

/*
 * FUNCTION NAME: str2uint
 *
 * DESCRIPTION  : The str2uint() function convert a string to an unsigned
 *                integer.
 *
 * ARGUMENTS    :
 *     IN :
 *          str       pointer to string to be converted
 *          maxlen    restricted maximum length of str
 *          endptr    pointer to the character that stops the scan
 *          base      number base to use
 *     OUT:
 *          none.
 *
 * RETURN       : Normally return the converted value.
 *                (uint32_t)-1: overflow, or str is NULL
 *                0           : maxlen is 0, or first character is '\0'
 *
 */
uint32_t str2uint(const char *str, uint32_t maxlen, const char **endptr, uint32_t base)
{
	uint64_t value = 0;
	uint32_t digit;

	if (!str)
		return (uint32_t)-1;

	/* skip 0x/0X */
	if ((base == 16) &&
		(maxlen >= 2) &&
		(str[0] == '0') &&
		(TOLOWER(str[1]) == 'x')) {
		str += 2;
		maxlen -= 2;
	}

	for (; *str != '\0' && maxlen; str++, maxlen--) {
		digit = to_digit(*str, base);

		/* hit unaccpetable character, convertion is done */
		if (digit == (uint32_t)-1)
			break;

		value = (value * base) + digit;

		/* check if overflow */
		if (value >> 32) {
			value = (uint64_t)(-1ULL);
			break;
		}
	}

	if (endptr)
		*endptr = str;

	return (uint32_t)value;
}
