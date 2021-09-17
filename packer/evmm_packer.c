/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>

#include "file_pack.h"
#include "vmm_base.h"

#if (defined (MODULE_TRUSTY_GUEST) || defined (MODULE_TRUSTY_TEE)) && defined (PACK_LK)
#define EVMM_PKG_BIN_NAME    "evmm_lk_pkg.bin"
#elif defined (MODULE_OPTEE_GUEST) && defined (PACK_OPTEE)
#define EVMM_PKG_BIN_NAME    "evmm_optee_pkg.bin"
#else
#define EVMM_PKG_BIN_NAME    "evmm_pkg.bin"
#endif

/* default settings if no cmdline inputs
 *  to add new modules, just append them at the end of
 *  this array.
 */
static char* file_list[] = {
	"stage0.bin",
	"stage1.bin",
	"evmm.elf",
#if (defined (MODULE_TRUSTY_GUEST) || defined (MODULE_TRUSTY_TEE)) && defined (PACK_LK)
	"lk.bin",
#endif

#if  defined (MODULE_OPTEE_GUEST) && defined (PACK_OPTEE)
	"tee-pager_v2.bin",
#endif
};

static uint32_t file_size[PACK_BIN_COUNT];

/*
 * append buf (with length) to file
 */
static int append_buf_to_file(FILE *f, const void *input_buf, uint32_t input_buf_len)
{
	fwrite(input_buf, input_buf_len, 1, f);
	if (ferror(f) != 0 ||
		feof(f) != 0) {
		printf("!ERROR(packer): failed to append %d bytes\r\n",
			(uint32_t)input_buf_len);
		return -1;
	}

	fflush(f);

	return 0;
}

/*
 * read file content to a buffer, the caller is responsible
 * for freeing buffer.
 */
static void *read_file_to_buf(const char *fname, uint32_t buf_size)
{
	FILE *f = fopen(fname, "r");
	void *pbuf = NULL;

	if (!f) {
		printf("!ERROR(packer): failed to open %s\r\n", fname);
		goto error;
	}

	pbuf = malloc(buf_size);
	if (!pbuf) {
		printf("!ERROR(packer): failed to allocate memory\r\n");
		goto error;
	}

	fread(pbuf, buf_size, 1, f);
	if (ferror(f) != 0 ||
		feof(f) != 0) {
		printf("!ERROR(packer): failed to read %d bytes in file %s\r\n",
			buf_size,
			fname);

		free(pbuf);
		pbuf = NULL;

		goto error;
	}

error:
	if (f) {
		fclose(f);
	}

	return pbuf;
}

/*
 * to caller, if return value is -1 or 0, the report error.
 */
static int get_file_size(char *file)
{
	struct stat st = { 0 };

	if (stat(file, &st) == 0) {
		return st.st_size;
	} else {
		printf("!ERROR(packer): failed to get file %s size\r\n", file);
		return -1;
	}
}

/*
 *  fill file size into file_size[].
 */
static int fill_file_size(void)
{
	int file_idx;
	int fsize;

	for (file_idx = 0; file_idx < PACK_BIN_COUNT; file_idx++) {
		/* update file size */
		fsize = get_file_size(file_list[file_idx]);

		if (fsize == -1) {
			return -1;
		}

		file_size[file_idx] = (uint32_t)fsize;
	}

	return 0;
}

static uint32_t get_total_file_size(void)
{
	uint32_t file_idx, total_file_size = 0;
	for (file_idx = 0; file_idx < PACK_BIN_COUNT; file_idx++) {
		total_file_size += file_size[file_idx];
	}
	return total_file_size;
}


/* evmm loader might use the memory after evmm_pkg.bin
 *  as heap to hold the memory for stage1 in runtime,
 *  EVMM_PKG_BIN_SIZE can be used here to calculate the
 *  end of evmm_pkg.bin, which can be used as heap base. */
static int total_file_size_check(void)
{
#ifdef EVMM_PKG_BIN_SIZE
	uint32_t total_file_size = 0;

	total_file_size = get_total_file_size();

	if (total_file_size > EVMM_PKG_BIN_SIZE) {
		printf(
			"\r\n!ERROR(packer): Required size 0x%x, actual allocated size 0x%x. Please update macro(EVMM_PKG_BIN_SIZE gordon_peak.cfg)\r\n\r\n",
			total_file_size,
			EVMM_PKG_BIN_SIZE);
		return -1;
	}
	printf("\r\n!INFO(packer): Check total file size is OK\r\n");
#else
	printf("\r\n!INFO(packer): Skip total file size check\r\n");
#endif
	return 0;
}

static int update_file_header(uint32_t *addr, uint32_t size)
{
	int i;
	file_offset_header_t *file_hdr;
	file_hdr = get_file_offsets_header((uint64_t)addr, size);

	if (!file_hdr)
		return -1;

	for (i = 0; i < PACK_BIN_COUNT; i++)
		file_hdr->file_size[i] = file_size[i];

	return 0;
}

/*
 * pack all the files into a new pkg binary file (now named evmm_pkg.bin)
 */
static int pack_files(void)
{
	FILE *newfile = NULL;
	int ret = -1;
	int file_idx;
	void *file_buf = NULL;

	/* remove this file if exists*/
	if (remove(EVMM_PKG_BIN_NAME)) {
		/* ignore it if not exist */
		if (errno != ENOENT) {
			printf(
				"\r\n!ERROR(packer): failed to remove the old file: %s (err - %d) \r\n\r\n",
				EVMM_PKG_BIN_NAME,
				errno);
			goto exit;
		}
	}

	/* open it with "a" for appending, create one if not exists */
	newfile = fopen(EVMM_PKG_BIN_NAME, "a");
	if (newfile == NULL) {
		goto exit;
	}

	for (file_idx = 0; file_idx < PACK_BIN_COUNT; file_idx++) {
		/* read file to tmp buffer */
		file_buf = read_file_to_buf(file_list[file_idx], file_size[file_idx]);
		if (!file_buf) {
			printf(
				"\r\n!ERROR(packer): failed to read file contents: %s\r\n\r\n",
				file_list[file_idx]);
			goto exit;
		}

		/* update file header */
		if (0 == file_idx) {
			if (0 != update_file_header((uint32_t *)file_buf, file_size[file_idx])) {
				goto exit;
			}
		}

		/* append this buf content to the new file */
		if (0 != append_buf_to_file(newfile, file_buf, file_size[file_idx])) {
			goto exit;
		}
		free(file_buf); file_buf = NULL;
	}

	/* success */
	ret = 0;

exit:

	if (newfile) {
		fclose(newfile);
	}
	if (file_buf) {
		free(file_buf);
	}

	return ret;
}


int main(int argc, char *argv[])
{
	int ret = 0;
	int idx = 0;

	/* fill file size into file_size[] */
	ret = fill_file_size();
	if (ret == -1) {
		goto error;
	}

	ret = total_file_size_check();
	if (ret == -1) {
		goto error;
	}

	/* pack/append all the files into a new binary file.*/
	ret = pack_files();
	if (ret == -1) {
		goto error;
	}

	printf("\r\n!INFO(packer): Successfully pack below binaries into %s:\r\n",
		EVMM_PKG_BIN_NAME);
	for (idx = 0; idx < PACK_BIN_COUNT; idx++) {
		printf("\t %20s %16d bytes\n",
			basename(file_list[idx]), file_size[idx]);
	}

	return 0;

error:
	printf("\r\n!INFO(packer): Failed to pack\r\n");
	return ret;
}

/* End of file */
