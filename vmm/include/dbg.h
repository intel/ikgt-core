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

#ifndef _VMM_DBG_H_
#define _VMM_DBG_H_

#include "lock.h"

#include "lib/print.h"

#ifdef LIB_PRINT
extern vmm_lock_t vmm_print_lock;
#define vmm_printf(fmt, ...) \
{\
	lock_acquire_write(&vmm_print_lock); \
	D(print_init(FALSE);) \
	printf(fmt, ##__VA_ARGS__); \
	lock_release(&vmm_print_lock); \
}
#define vmm_print_init(setup) \
{\
	lock_init(&vmm_print_lock); \
	print_init(setup);\
}
#else
#define vmm_printf(fmt, ...)
#define vmm_print_init(setup)
#endif

#define LEVEL_PANIC 1
#define LEVEL_INFO 2
#define LEVEL_WARNING 3
#define LEVEL_TRACE 4

void clear_deadloop_flag(void);
void register_final_deadloop_handler(void (*func)(void));
void vmm_deadloop(const char *file_name,  uint32_t line_num);

#if LOG_LEVEL >= LEVEL_PANIC
#define print_panic(fmt, ...) { \
	print_init(FALSE); \
	vmm_printf("PANIC:" fmt, ##__VA_ARGS__); }
#else
#define print_panic(fmt, ...)
#endif

#if LOG_LEVEL >= LEVEL_INFO
#define print_info(fmt, ...) vmm_printf(fmt, ##__VA_ARGS__);
#else
#define print_info(fmt, ...)
#endif

#if LOG_LEVEL >= LEVEL_WARNING
#define print_warn(fmt, ...) vmm_printf("WARNING:" fmt,  ##__VA_ARGS__);
#else
#define print_warn(fmt, ...)
#endif

#if LOG_LEVEL >= LEVEL_TRACE
#define print_trace(fmt, ...) vmm_printf(fmt, ##__VA_ARGS__);
#else
#define print_trace(fmt, ...)
#endif

#define VMM_DEADLOOP() vmm_deadloop(__FILE__, __LINE__)
#define VMM_ASSERT(__condition) { if (!(__condition)) vmm_deadloop(__FILE__, __LINE__);}
#define VMM_ASSERT_EX(__condition, msg, ...) \
{ \
	if (!(__condition)) \
	{\
		print_panic(msg, ##__VA_ARGS__); \
		vmm_deadloop(__FILE__, __LINE__); \
	}\
}

#endif    /* _VMM_DBG_H_ */
