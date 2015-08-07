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

#ifndef _POLICY_MANAGER_
#define _POLICY_MANAGER_

#include "mon_defs.h"
#include "mon_objects.h"

/**************************************************************************
 *
 * Define MON policy manager
 *
 * The Policy Manager is responsible for setting up all switches in different
 * MON objects that influence the overall MON behavior. The MON global
 * behavior should depend on required application behavior it is used for.
 *
 ************************************************************************** */

/*
 * Return codes for the POLICY related functions.
 */
typedef enum {
	POL_RETVAL_SUCCESS = 0,
	POL_RETVAL_FAIL = -256,
	POL_RETVAL_BAD_PARAM,
	POL_RETVAL_BAD_VALUE,
} pol_retval_t;

/*
 * Paging POLICY values.
 */
typedef enum {
	POL_PG_NO_PAGING,
	POL_PG_VTLB,
	POL_PG_EPT,
	POL_PG_PRIVATE,
	POL_PG_ILLEGAL
} mon_paging_policy_t;

/*
 * Paging POLICY values.
 */
typedef enum {
	POL_CACHE_DIS_NO_INTERVENING,
	POL_CACHE_DIS_VIRTUALIZATION,
	POL_CACHE_DIS_ILLEGAL,
} mon_cache_policy_t;

/*
 * POLICY types.
 */
typedef uint64_t mon_policy_t;

/*----------------------------------------------------------------------- */
/*
 * Setup the policy
 *
 * Called by BSP main() before any initializations to setup the MON policy.
 *
 *----------------------------------------------------------------------- */

pol_retval_t global_policy_setup(const mon_policy_t *policy);

boolean_t global_policy_uses_vtlb(void);

boolean_t global_policy_uses_ept(void);

boolean_t global_policy_is_cache_dis_virtualized(void);

pol_retval_t get_global_policy(mon_policy_t *policy);

/*
 * Functions to manipulate mon_policy_t type variables.
 */
pol_retval_t clear_policy(mon_policy_t *policy);

pol_retval_t copy_policy(mon_policy_t *dst_policy,
			 const mon_policy_t *src_policy);

/*
 * Functions for cache policy.
 */
pol_retval_t clear_cache_policy(mon_policy_t *policy);
pol_retval_t set_cache_policy(mon_policy_t *policy,
			      mon_cache_policy_t cache_policy);
pol_retval_t get_cache_policy(const mon_policy_t *policy,
			      mon_cache_policy_t *cache_policy);

/*
 * Functions for paging policy.
 */
pol_retval_t clear_paging_policy(mon_policy_t *policy);
pol_retval_t set_paging_policy(mon_policy_t *policy,
			       mon_paging_policy_t pg_policy);
pol_retval_t get_paging_policy(const mon_policy_t *policy,
			       mon_paging_policy_t *pg_policy);

#endif                          /* _POLICY_MANAGER_ */
