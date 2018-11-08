/*******************************************************************************
* Copyright (c) 2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

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
			salt, salt_len,
			ikm, ikm_len,
			prk) != 0) {
		return 0;
	}

	return 1;
}
