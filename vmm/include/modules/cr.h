/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _CR_H_
#define _CR_H_

#ifndef MODULE_CR
#error "MODULE_CR is not defined"
#endif

/* this module only isolates CR2 and CR8 between GUESTs.
 * for CR0, CR3, CR4, they are isolated by VMCS directly
 * for host CR2 and CR8, since it will not impact host and
 * host will not use them, they are NOT isolated from guests.
 */
void cr_isolation_init(void);
#endif

