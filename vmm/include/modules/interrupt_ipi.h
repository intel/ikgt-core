/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _INTERRUPT_IPI_H_
#define _INTERRUPT_IPI_H_

#ifndef MODULE_INTERRUPT_IPI
#error "MODULE_EXT_INTR is not defined"
#endif

void interrupt_ipi_init(void);

#endif /* _INTERRUPT_IPI_H_ */
