/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/*------------------------------------------------------------------------*
*  FUNCTION : vmentry_failure_function
*  PURPOSE  : Called upon VMENTER failure
*  ARGUMENTS: uint64_t flag - value of processor flags register
*  RETURNS  : void
*  NOTES    : is not VMEXIT
*------------------------------------------------------------------------*/
void vmentry_failure_function(uint64_t flags);

