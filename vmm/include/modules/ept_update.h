/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _EPT_UPDATE_H_
#define _EPT_UPDATE_H_

#ifndef MODULE_EPT_UPDATE
#error "MODULE_EPT_UPDATE is not defined"
#endif

void ept_update_install(uint32_t guest_id);

#endif /* _EPT_UPDATE_H_ */
