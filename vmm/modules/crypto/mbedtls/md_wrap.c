/*
 * Copyright (c) 2006-2015 ARM Limited.
 * Copyright (c) 2018-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "md_internal.h"
#include "sha256.h"
#include "md.h"

/*
 * Wrappers for generic message digests
 */

static int sha256_update_wrap( void *ctx, const unsigned char *input,
                                size_t ilen )
{
    return( mbedtls_sha256_update_ret( (mbedtls_sha256_context *) ctx,
                                       input, ilen ) );
}

static int sha256_finish_wrap( void *ctx, unsigned char *output )
{
    return( mbedtls_sha256_finish_ret( (mbedtls_sha256_context *) ctx,
                                       output ) );
}

static void *sha256_ctx_alloc( void )
{
    void *ctx = mbedtls_calloc( 1, sizeof( mbedtls_sha256_context ) );

    if( ctx != NULL )
        mbedtls_sha256_init( (mbedtls_sha256_context *) ctx );

    return( ctx );
}

static void sha256_ctx_free( void *ctx )
{
    mbedtls_sha256_free( (mbedtls_sha256_context *) ctx );
    mbedtls_free( ctx );
}

static void sha256_clone_wrap( void *dst, const void *src )
{
    mbedtls_sha256_clone( (mbedtls_sha256_context *) dst,
                    (const mbedtls_sha256_context *) src );
}

static int sha256_process_wrap( void *ctx, const unsigned char *data )
{
    return( mbedtls_internal_sha256_process( (mbedtls_sha256_context *) ctx,
                                             data ) );
}

static int sha256_starts_wrap( void *ctx )
{
    return( mbedtls_sha256_starts_ret( (mbedtls_sha256_context *) ctx, 0 ) );
}

static int sha256_wrap( const unsigned char *input, size_t ilen,
                        unsigned char *output )
{
    return( mbedtls_sha256_ret( input, ilen, output, 0 ) );
}

const mbedtls_md_info_t mbedtls_sha256_info = {
    0,
    MBEDTLS_MD_SHA256,
    "SHA256",
    32,
    64,
    sha256_starts_wrap,
    sha256_update_wrap,
    sha256_finish_wrap,
    sha256_wrap,
    sha256_ctx_alloc,
    sha256_ctx_free,
    sha256_clone_wrap,
    sha256_process_wrap,
};
