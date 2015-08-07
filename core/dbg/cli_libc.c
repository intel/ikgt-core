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

#include "common_libc.h"
#include "hw_utils.h"
#include "cli_libc.h"

typedef int32_t (*fun_ascii_to_digit_t) (char);

int CLI_strcmp(char *string1, char *string2)
{
	while ((*string1 == *string2) && (0 != *string1)) {
		string1++;
		string2++;
	}
	return *string1 - *string2;
}

int CLI_strncmp(char *string1, char *string2, size_t n)
{
	size_t i;

	for (i = 0; i < n; ++i)
		if (string1[i] != string2[i]) {
			return 1;
		}
	return 0;
}

int CLI_is_substr(char *bigstring, char *smallstring)
{
	while ((*bigstring == *smallstring) && (0 != *smallstring)) {
		bigstring++;
		smallstring++;
	}
	return 0 == *smallstring;
}

int32_t ascii_to_dec_digit(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	return -1;
}

int32_t ascii_to_hex_digit(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'a' && ch <= 'f') {
		return ch - 'a' + 10;
	}
	if (ch >= 'A' && ch <= 'F') {
		return ch - 'A' + 10;
	}
	return -1;
}

uint64_t cli_atol64(char *string, unsigned base, int *perror)
{
	uint64_t value = 0;
	int32_t last_digit;
	fun_ascii_to_digit_t ascii_to_digit;
	int error = 0;

	do {
		if (10 == base) {
			ascii_to_digit = ascii_to_dec_digit;
		} else if (16 == base) {
			ascii_to_digit = ascii_to_hex_digit;
		} else {
			error = -1; /* bad base */
			value = 0;
			break;
		}

		while (*string != 0) {
			last_digit = (*ascii_to_digit) (*string);
			if (-1 == last_digit) {
				error = -1; /* bad input */
				value = 0;
				break;
			}
			value = value * base + last_digit;
			string++;
		}
	} while (0);

	if (NULL != perror) {
		*perror = error;
	}
	return value;
}

uint32_t cli_atol32(char *string, unsigned base, int *error)
{
	return (uint32_t)cli_atol64(string, base, error);
}
