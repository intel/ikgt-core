/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"

int hkdf_sha256(uint8_t *out_key, size_t out_len,
		const uint8_t *secret, size_t secret_len,
		const uint8_t *salt, size_t salt_len,
		const uint8_t *info, size_t info_len)
{
	const mbedtls_md_info_t *md;

	md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	if (md == NULL) {
		return 0;
	}

	if (mbedtls_hkdf(md,
			salt, salt_len,
			secret, secret_len,
			info, info_len,
			out_key, out_len) != 0) {
		return 0;
	}

	return 1;
}

int hmac_sha256(const unsigned char *salt, size_t salt_len,
		const unsigned char *ikm, size_t ikm_len,
		unsigned char *prk)
{
	const mbedtls_md_info_t *md;

	md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	if (md == NULL) {
		return 0;
	}

	if (mbedtls_md_hmac(md,
			ikm, ikm_len,
			salt, salt_len,
			prk) != 0) {
		return 0;
	}

	return 1;
}
