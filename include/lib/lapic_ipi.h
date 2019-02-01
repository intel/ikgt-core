/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _LAPIC_IPI_H
#define _LAPIC_IPI_H

#ifndef LIB_LAPIC_IPI
#error "LIB_LAPIC_IPI is not defined"
#endif

#include "vmm_base.h"

boolean_t lapic_get_id(uint32_t *p_lapic_id);

//broadcast is excluding self, send is specified.
boolean_t broadcast_nmi(void);
boolean_t broadcast_init(void);
boolean_t broadcast_startup(uint32_t vector);
boolean_t send_nmi(uint32_t lapic_id);
boolean_t send_startup(uint32_t lapic_id, uint32_t vector);
boolean_t send_self_ipi(uint32_t vector);

#endif
