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

#ifndef _MON_DBG_H_
#define _MON_DBG_H_

#include "mon_startup.h"
#include "heap.h"
#include "cli_monitor.h"

/*
 * externs
 */
extern cpu_id_t ASM_FUNCTION hw_cpu_id(void);
extern void ipc_set_no_resend_flag(boolean_t val);
extern boolean_t mon_debug_port_init_params(const mon_debug_port_params_t *
					    p_params);
extern mon_debug_port_virt_mode_t mon_debug_port_get_virt_mode(void);
/* If the debug port uses an I/O range, returns its base
 * address. -Otherwise, returns 0 */
extern uint16_t mon_debug_port_get_io_base(void);
/* If the debug port uses an I/O range, returns its end
 * address. - Otherwise, returns 0 */
extern uint16_t mon_debug_port_get_io_end(void);
/* __attribute__((format * (printf, 1,2))); */
extern int CDECL mon_printf(const char *format, ...);
extern int CDECL mon_vprintf(const char *format, va_list args);

extern mon_startup_struct_t mon_startup_data;
extern void mon_deadloop_dump(uint32_t file_code, uint32_t line_num);

boolean_t deadloop_helper(const char *assert_condition,
			  const char *func_name,
			  const char *file_name,
			  uint32_t line_num,
			  uint32_t access_level);

#ifdef DEBUG
#define MON_DEBUG_CODE(__xxx) __xxx
#else
#define MON_DEBUG_CODE(__xxx)
#endif

enum debug_bit_mask {
	/* NOTE: values should be in the range of 0 - 31 */
	/* a temporary mask to maintain backwards compatibility. Eventually
	 * every component should create its own mask. */
	mask_anonymous = 0,
	mask_cli = 1,
	mask_emulator = 2,
	mask_gdb = 3,
	mask_ept = 4,
	mask_mon = 5,
	mask_xmon_api = 6,
	mask_plugin = 7
};

#define DEBUG_MASK_ALL (unsigned long long)(-1)

enum msg_level {
	level_print_always = 0,
	level_error = 1,
	level_warning = 2,
	level_info = 3,
	level_trace = 4
};

#define MON_COMPILE_TIME_ADDRESS(__a) \
	((__a) - mon_startup_data.mon_memory_layout[mon_image].base_address)
#define MON_RANGE_ADDRESS(__a)                                                 \
	((__a) >= mon_startup_data.mon_memory_layout[mon_image].base_address &&   \
	 (__a) < (mon_startup_data.mon_memory_layout[mon_image].base_address +    \
		  mon_startup_data.mon_memory_layout[mon_image].total_size))

#define MON_DEFAULT_LOG_MASK mon_startup_data.debug_params.mask
#define MON_DEFAULT_LOG_LEVEL mon_startup_data.debug_params.verbosity

#define MON_BREAKPOINT()                                \
	{                                               \
		ipc_set_no_resend_flag(TRUE);           \
		MON_UP_BREAKPOINT();                    \
	}

#ifdef CLI_INCLUDE
#define MON_DEADLOOP_HELPER cli_deadloop_helper
#else
#define MON_DEADLOOP_HELPER deadloop_helper
#endif

#ifdef DEBUG
#define MON_DEADLOOP_LOG(FILE_CODE)                                             \
	{                                                                       \
		if (MON_DEADLOOP_HELPER(NULL, __FUNCTION__, __FILE__, __LINE__, \
			    1)) {                                               \
			MON_UP_BREAKPOINT();                                    \
		}                                                               \
	}
#else
#define MON_DEADLOOP_LOG(FILE_CODE)     mon_deadloop_dump(FILE_CODE, __LINE__);
#endif

#ifdef LOG_MASK
#define MON_MASK_CHECK(MASK) ((((unsigned long long)1 << MASK) & LOG_MASK) && \
	(((unsigned long long)1 << MASK) & MON_DEFAULT_LOG_MASK))
#else
#define MON_MASK_CHECK(MASK) \
	(((unsigned long long)1 << MASK) & MON_DEFAULT_LOG_MASK)
#endif

#ifdef LOG_LEVEL
#define MON_LEVEL_CHECK(LEVEL) \
	((LEVEL <= LOG_LEVEL) && (LEVEL <= MON_DEFAULT_LOG_LEVEL))
#else
#define MON_LEVEL_CHECK(LEVEL)  (LEVEL <= MON_DEFAULT_LOG_LEVEL)
#endif

#define MON_LOG(MASK, LEVEL, ...) \
	MON_DEBUG_CODE(((LEVEL == level_print_always) || \
			(LEVEL == level_error) || \
			((MON_MASK_CHECK(MASK) && MON_LEVEL_CHECK(LEVEL)))) && \
	(mon_printf(__VA_ARGS__)))

#define MON_LOG_NOLOCK(...)  MON_DEBUG_CODE(mon_printf_nolock(__VA_ARGS__))

#ifdef DEBUG
#define MON_ASSERT_LOG(FILE_CODE, __condition)                                 \
	{                                                                              \
		if (!(__condition)) {                                                      \
			if (MON_DEADLOOP_HELPER(#__condition, __FUNCTION__,                    \
				    __FILE__, __LINE__, 1)) {                      \
				MON_UP_BREAKPOINT();                                               \
			}                                                                      \
		}                                                                          \
	}
#else
#define MON_ASSERT_LOG(FILE_CODE, __condition)                                 \
	{                                                                              \
		if (!(__condition)) {                                                  \
			mon_deadloop_dump(FILE_CODE, __LINE__);                            \
		}                                                                      \
	}
#endif

#define MON_ASSERT_NOLOCK_LOG(FILE_CODE, __condition) \
	MON_ASSERT_LOG(FILE_CODE, __condition)

#define MON_CALLTRACE_ENTER() \
	MON_LOG(mask_anonymous, level_trace, "[%d enter>>> %s\n", hw_cpu_id(), \
	__FUNCTION__)
#define MON_CALLTRACE_LEAVE() \
	MON_LOG(mask_anonymous, level_trace, "<<<leave %d] %s\n", hw_cpu_id(), \
	__FUNCTION__)

int CLI_active(void);

/* ERROR levels used in the macros below. */
enum error_level {
	API_ERROR = 0,
	FATAL_ERROR = 1,
	FATAL_ERROR_DEADLOOP = 2
};

/* Depending on the error_level, it either injects an exception into the guest
 * or causes an infinite loop that never returns. */
#define MON_ERROR_CHECK(__condition, __error_level)                     \
	{                                                                       \
		if (!(__condition)) {                                               \
			cli_handle_error(#__condition, __FUNCTION__, __FILE__,           \
				__LINE__, __error_level);                       \
		}                                                                   \
	}

#endif    /* _MON_DBG_H_ */
