/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#define TRUSTY_VMCALL_STACK_PROFILE 0x6C696E02

int main(int argc, char **argv)
{
	__asm__ __volatile__(
		"vmcall"
		:: "a"(TRUSTY_VMCALL_STACK_PROFILE)
	);

	return 0;
}
