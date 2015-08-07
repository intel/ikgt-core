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

#ifndef _CLI_LIBC_H_
#define _CLI_LIBC_H_

#include "mon_defs.h"
#include "libc.h"

int CLI_strcmp(char *string1, char *string2);
int CLI_strncmp(char *string1, char *string2, size_t n);
int CLI_is_substr(char *bigstring, char *smallstring);
uint32_t cli_atol32(char *string, unsigned base, int *error);
uint64_t cli_atol64(char *string, unsigned base, int *error);

#endif                          /* _CLI_LIBC_H_ */
