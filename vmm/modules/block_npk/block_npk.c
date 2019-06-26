/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "lib/pci.h"
#include "modules/dev_blk.h"
#include "modules/block_npk.h"

void block_npk(void)
{
	/* Guest id is 0 means android or Linux */
	block_pci_device(0, PCI_DEV(NPK_PCI_BUS, NPK_PCI_DEV, NPK_PCI_FUN));

	return;
}
