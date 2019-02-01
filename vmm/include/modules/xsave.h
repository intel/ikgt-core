/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _XSAVE_H_
#define _XSAVE_H_

#ifndef MODULE_XSAVE
#error "MODULE_XSAVE is not defined"
#endif

#define XSAVE_MMX     (1ull << 0)                                        /*MMX:bit 0*/
#define XSAVE_SSE     ((1ull << 1) | XSAVE_MMX)                          /*SEE:bit 1,depends on MMX*/
#define XSAVE_AVX     ((1ull << 2) | XSAVE_SSE)                          /*AVX:bit 2,depends on SSE*/
#define XSAVE_MPT     ((1ull << 3) | (1u << 4) | XSAVE_MMX)              /*MPT:bit 3,4,depends on MMX*/
#define XSAVE_AVX512  ((1ull << 5) | (1u << 6) | (1u << 7) | XSAVE_AVX)  /*AVX512:bit 5,6,7,depends on AVX*/
#define XSAVE_PKRU    ((1ull << 9) | XSAVE_MMX)                          /*PKRU:bit 9,depends on MMX*/

/*if components is 0,max xcr0 will be used to isolate all components supported*/
#define XSAVE_SUPPORTED_ALL     0

/* CR4.OSXSAVE has already been set in get_init_cr4() if supported*/
void xsave_isolation_init(uint64_t components);
#endif
