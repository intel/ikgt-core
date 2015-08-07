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

#ifndef _HW_SETJMP_H_
#define _HW_SETJMP_H_

/* Context saved registers:
 * rbx, rbp
 * r12, r13, r14, r15,
 * rsp, rip */
#define SETJMP_BUFFER_ITEMS  8

typedef uint8_t setjmp_buffer_t[SETJMP_BUFFER_ITEMS * sizeof(address_t)];

extern int setjmp(setjmp_buffer_t *env);
extern void longjmp(setjmp_buffer_t *env, int errcode);
extern void hw_exception_post_handler(void);

#endif                          /* _HW_SETJMP_H_ */
