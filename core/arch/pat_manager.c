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

#include <mon_defs.h>
#include <mon_dbg.h>
#include <mon_startup.h>
#include <hw_utils.h>
#include <guest_cpu.h>
#include <em64t_defs.h>
#include <scheduler.h>
#include <lock.h>
#include <mon_phys_mem_types.h>
#include <pat_manager.h>
#include <host_memory_manager_api.h>
#include <mon_events_data.h>
#include <event_mgr.h>
#include <host_cpu.h>
#include <flat_page_tables.h>
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(PAT_MANAGER_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(PAT_MANAGER_C, __condition)

/*--------------------------------------------------------------------------- */
#define PAT_MNGR_INVALID_PAT_MSR_VALUE (~((uint64_t)0))
#define PAT_MNGR_NUM_OF_ATTRUBUTE_FIELDS 8

/*--------------------------------------------------------------------------- */
static
mon_phys_mem_type_t pat_mngr_get_memory_type(uint64_t pat_value, uint32_t index)
{
	uint64_t memory_type = ((pat_value >> (index * 8)) & 0xff);

	return (mon_phys_mem_type_t)memory_type;
}

uint32_t pat_mngr_get_earliest_pat_index_for_mem_type(
	mon_phys_mem_type_t mem_type,
	uint64_t pat_msr_value)
{
	uint32_t i;

	if (pat_msr_value == PAT_MNGR_INVALID_PAT_MSR_VALUE) {
		return PAT_MNGR_INVALID_PAT_INDEX;
	}

	for (i = 0; i < PAT_MNGR_NUM_OF_ATTRUBUTE_FIELDS; i++) {
		if (pat_mngr_get_memory_type(pat_msr_value, i) == mem_type) {
			return i;
		}
	}

	return PAT_MNGR_INVALID_PAT_INDEX;
}

uint32_t
pat_mngr_retrieve_current_earliest_pat_index_for_mem_type(mon_phys_mem_type_t
							  mem_type)
{
	uint64_t pat_msr_value = hw_read_msr(IA32_MSR_PAT);
	uint32_t result = 0;

	/* assume that PAT MSR not used if its value is ZERO
	 * then use compatibility setttings. */
	if (pat_msr_value == 0) {
		switch (mem_type) {
		case MON_PHYS_MEM_WRITE_BACK:
			/* see IA32 SDM, table 11-11/12 */
			result = 0;
			break;

		case MON_PHYS_MEM_UNCACHABLE:
			/* see IA32 SDM, table 11-11/12 */
			result = 3;
			break;

		default:
			result = PAT_MNGR_INVALID_PAT_INDEX;
			MON_LOG(mask_mon,
				level_error,
				"CPU%d: %s: Error: mem type(%d) currently not supported\n",
				hw_cpu_id(),
				__FUNCTION__,
				mem_type);
			MON_DEBUG_CODE(MON_DEADLOOP();)
			break;
		}
	} else {
		result = pat_mngr_get_earliest_pat_index_for_mem_type(mem_type,
			pat_msr_value);
	}

	return result;
}
