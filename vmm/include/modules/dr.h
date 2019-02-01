/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _DR_H_
#define _DR_H_

#ifndef MODULE_DR
#error "MODULE_DR is not defined"
#endif

/* this module only isolates DR0~DR3, DR6 between GUESTs.
 * for DR7 and DEBUG_CTRL_MSR, they are isolated by VMCS directly
 * for host DRs, DR7 and DEBUG_CTRL_MSR will be set to 0x400
 * and 0x0 in each VMExit, which disables all DRs.
 * So, host DR0~DR3, DR6 are NOT isolated from guests.
 */
void dr_isolation_init(void);
#endif
