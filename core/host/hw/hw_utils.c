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

#include "mon_defs.h"
#include "hw_utils.h"
#include "hw_interlocked.h"
#include "trial_exec.h"
#include "local_apic.h"
#include "8259a_pic.h"
#include "mon_dbg.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(HW_UTILS_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(HW_UTILS_C, __condition)

static uint64_t hw_tsc_ticks_per_second;

#define IA32_DEBUG_IO_PORT   0x80

/*================================== hw_stall() ===========================
 *
 * Stall (busy loop) for a given time, using the platform's speaker port
 * h/w.  Should only be called at initialization, since a guest OS may
 * change the platform setting. */
void hw_stall(uint32_t stall_usec)
{
	uint32_t count;

	for (count = 0; count < stall_usec; count++)
		hw_read_port_8(IA32_DEBUG_IO_PORT);
}

/*======================= hw_calibrate_tsc_ticks_per_second() =============
 *
 * Calibrate the internal variable holding the number of TSC ticks pers second.
 *
 * Should only be called at initialization, as it relies on hw_stall() */
void hw_calibrate_tsc_ticks_per_second(void)
{
	uint64_t start_tsc;

	start_tsc = hw_rdtsc();
	hw_stall(1000);         /* 1 ms */
	hw_tsc_ticks_per_second = (hw_rdtsc() - start_tsc) * 1000;
}

/*======================= hw_calibrate_tsc_ticks_per_second() =============
 *
 * Retrieve the internal variable holding the number of TSC ticks pers second.
 * Note that, depending on the CPU and ASCI modes, this may only be used as a
 * rough estimate. */
uint64_t hw_get_tsc_ticks_per_second(void)
{
	return hw_tsc_ticks_per_second;
}

/*========================== hw_stall_using_tsc() =========================
 *
 * Stall (busy loop) for a given time, using the CPU TSC register.
 * Note that, depending on the CPU and ASCI modes, the stall accuracy may be
 * rough. */
void hw_stall_using_tsc(uint32_t stall_usec)
{
	uint64_t end_tsc;

	MON_ASSERT(hw_tsc_ticks_per_second != 0);

	end_tsc = hw_rdtsc() +
		  ((uint64_t)stall_usec * hw_tsc_ticks_per_second /
		   (uint64_t)1000000);

	while (hw_rdtsc() < end_tsc)
		hw_pause();
}

boolean_t hw_wrmsr_safe(uint32_t msr_id,
			uint64_t value,
			vector_id_t *fault_vector,
			uint32_t *error_code)
{
	boolean_t ret;
	trial_data_t *p_trial = NULL;

	TRY {
		hw_write_msr(msr_id, value);
		ret = TRUE;
	}
	CATCH(p_trial) {
		ret = FALSE;
		if (NULL != p_trial) {
			MON_LOG(mask_anonymous,
				level_error,
				"WRMSR(%P) Failed. FaultVector=%P ErrCode=%P\n",
				msr_id,
				p_trial->fault_vector,
				p_trial->error_code);
			if (NULL != fault_vector) {
				*fault_vector = p_trial->fault_vector;
			}
			if (NULL != error_code) {
				*error_code = p_trial->error_code;
			}
		}
	}
	END_TRY;
	return ret;
}

boolean_t hw_rdmsr_safe(uint32_t msr_id,
			uint64_t *value,
			vector_id_t *fault_vector,
			uint32_t *error_code)
{
	boolean_t ret;
	trial_data_t *p_trial = NULL;

	TRY {
		*value = hw_read_msr(msr_id);
		ret = TRUE;
	}
	CATCH(p_trial) {
		ret = FALSE;
		if (NULL != p_trial) {
			MON_LOG(mask_anonymous,
				level_error,
				"RDMSR[%P] failed. FaultVector=%P ErrCode=%P\n",
				msr_id,
				p_trial->fault_vector,
				p_trial->error_code);
			if (NULL != fault_vector) {
				*fault_vector = p_trial->fault_vector;
			}
			if (NULL != error_code) {
				*error_code = p_trial->error_code;
			}
		}
	}
	END_TRY;
	return ret;
}
