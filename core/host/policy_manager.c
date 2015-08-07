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

#include "policy_manager.h"
#include "mon_dbg.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(POLICY_MANAGER_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(POLICY_MANAGER_C, __condition)


#define FIELD_MASK(size, offset) ((BIT_VALUE64((size)) - 1) << (offset))

#define  POL_PG_FIELD_OFFS     0
#define  POL_PG_FIELD_SIZE     10
#define  POL_PG_MASK           FIELD_MASK(POL_PG_FIELD_SIZE, POL_PG_FIELD_OFFS)

#define  POL_CACHE_FIELD_OFFS  (POL_PG_FIELD_OFFS + POL_PG_FIELD_SIZE)
#define  POL_CACHE_FIELD_SIZE  2
#define  POL_CACHE_MASK        \
	FIELD_MASK(POL_CACHE_FIELD_SIZE, POL_CACHE_FIELD_OFFS)

static mon_policy_t g_mon_policy;
static boolean_t g_init_done = FALSE;
extern mon_paging_policy_t g_pg_policy;

/***************************************************************************
*
* Policy Manager
*
***************************************************************************/

/* ---------------------------- Global Policy APIs ------------------------ */

/*--------------------------------------------------------------------------
 *
 * Setup the global policy.
 *
 * Called by BSP main() before any initializations to setup the MON policy.
 *
 *-------------------------------------------------------------------------- */
pol_retval_t global_policy_setup(const mon_policy_t *policy)
{
	if (!g_init_done) {
		clear_policy(&g_mon_policy);
		g_init_done = TRUE;
	}

	return copy_policy(&g_mon_policy, policy);
}

boolean_t global_policy_uses_vtlb(void)
{
	mon_paging_policy_t pg_policy;

	get_paging_policy(&g_mon_policy, &pg_policy);

	return pg_policy == POL_PG_VTLB;
}

boolean_t global_policy_uses_ept(void)
{
	mon_paging_policy_t pg_policy;

	get_paging_policy(&g_mon_policy, &pg_policy);

	return pg_policy == POL_PG_EPT;
}

pol_retval_t get_global_policy(mon_policy_t *policy)
{
	return copy_policy(policy, &g_mon_policy);
}

boolean_t global_policy_is_cache_dis_virtualized(void)
{
	mon_cache_policy_t cache_policy;

	get_cache_policy(&g_mon_policy, &cache_policy);

	return cache_policy == POL_CACHE_DIS_VIRTUALIZATION;
}

/* -------------------------- Policy Manipulation APIs --------------------- */

pol_retval_t clear_policy(mon_policy_t *policy)
{
	*policy = 0;

	return POL_RETVAL_SUCCESS;
}

pol_retval_t copy_policy(mon_policy_t *dst_policy,
			 const mon_policy_t *src_policy)
{
	MON_ASSERT(dst_policy != NULL);

	*dst_policy = *src_policy;

	return POL_RETVAL_SUCCESS;
}

static pol_retval_t get_policy(const mon_policy_t *policy, void *policy_enum,
			       uint32_t offs, uint32_t size, uint32_t err_val)
{
	uint64_t bit = BIT_VALUE64(offs);
	uint32_t count;
	pol_retval_t ret = POL_RETVAL_SUCCESS;

	MON_ASSERT(policy != NULL);
	MON_ASSERT(policy_enum != NULL);

	for (count = 0; (*policy & bit) == 0 && count < size;
	     count++, bit <<= 1)
		;

	if (count == size) {
		ret = POL_RETVAL_BAD_VALUE;
		*(uint32_t *)policy_enum = err_val;
	} else {
		*(uint32_t *)policy_enum = count;
	}

	return ret;
}

pol_retval_t set_paging_policy(mon_policy_t *policy,
			       mon_paging_policy_t pg_policy)
{
	MON_ASSERT(policy != NULL);

	BITMAP_ASSIGN64(*policy, POL_PG_MASK,
		BIT_VALUE64((int)pg_policy + POL_PG_FIELD_OFFS));

	g_pg_policy = pg_policy;

	return POL_RETVAL_SUCCESS;
}

pol_retval_t get_paging_policy(const mon_policy_t *policy,
			       mon_paging_policy_t *pg_policy)
{
	return get_policy(policy,
		pg_policy,
		POL_PG_FIELD_OFFS,
		POL_PG_FIELD_SIZE,
		POL_CACHE_DIS_ILLEGAL);
}

pol_retval_t set_cache_policy(mon_policy_t *policy,
			      mon_cache_policy_t cache_policy)
{
	MON_ASSERT(policy != NULL);

	BITMAP_ASSIGN64(*policy, POL_CACHE_MASK,
		BIT_VALUE64((int)cache_policy + POL_CACHE_FIELD_OFFS));

	return POL_RETVAL_SUCCESS;
}

pol_retval_t get_cache_policy(const mon_policy_t *policy,
			      mon_cache_policy_t *cache_policy)
{
	return get_policy(policy, cache_policy, POL_CACHE_FIELD_OFFS,
		POL_CACHE_FIELD_SIZE, POL_CACHE_DIS_ILLEGAL);
}
