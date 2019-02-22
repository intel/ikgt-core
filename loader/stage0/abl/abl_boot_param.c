/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "abl_boot_param.h"
#include "stage0_lib.h"
#include "ldr_dbg.h"
#include "device_sec_info.h"

#include "lib/util.h"
#include "lib/string.h"

boolean_t get_emmc_serial(android_image_boot_params_t *android_boot_params, char *serial)
{
	multiboot_info_t *mbi;
	const char *cmdline;
	const char *arg;
#ifdef DEBUG
	const char *end;
#endif

	/* get MBI from guest EBX */
	mbi = (multiboot_info_t *)(uint64_t)android_boot_params->CpuState.cpu_gp_register[1];

	if (!CHECK_FLAG(mbi->flags, 2)) {
		print_panic("Guest Multiboot info does not contain cmdline field!\n");
		return FALSE;
	}

	cmdline = (const char *)(uint64_t)mbi->cmdline;
	arg = strstr_s(cmdline, MAX_STR_LEN, "androidboot.serialno=", sizeof("androidboot.serialno="));
	if (arg == NULL) {
		print_panic("Cannot find EMMC serial number form Guest cmdline!\n");
		return FALSE;
	}

	arg += (sizeof("androidboot.serialno=") - 1);

#ifdef DEBUG
	end = strstr_s(arg, MMC_PROD_NAME_WITH_PSN_LEN, " ", 1);
	if (end == NULL) {
		print_panic("Cannot locate end of EMMC serial string!\n");
		return FALSE;
	}

	if ((end - arg) != (MMC_PROD_NAME_WITH_PSN_LEN - 1)) {
		print_panic("Invalid EMMC serial string length!\n");
		return FALSE;
	}
#endif

	memcpy(serial, arg, MMC_PROD_NAME_WITH_PSN_LEN - 1);
	serial[MMC_PROD_NAME_WITH_PSN_LEN - 1] = '\0';

	print_trace("EMMC Serial:%s#\n", serial);

	return TRUE;
}

/* The cmdline is present like:
 *       "ImageBootParamsAddr=0x12345 ABL.hwver=51,4,1900,b00f,0,8192"
 */
boolean_t cmdline_parse(multiboot_info_t *mbi, cmdline_params_t *cmdline_param)
{
	const char *cmdline;
	const char *arg;
	const char *param;
	uint32_t addr, cpu_num, cpu_freq;

	if (!mbi || !cmdline_param) {
		print_panic("mbi or cmdline_params is NULL\n");
		return FALSE;
	}

	if (!CHECK_FLAG(mbi->flags, 2)) {
		print_panic("Multiboot info does not contain cmdline field!\n");
		return FALSE;
	}

	cmdline = (const char *)(uint64_t)mbi->cmdline;

	/* Parse ImageBootParamsAddr */
	print_trace("cmdline form ABL: %s\n", cmdline);
	arg = strstr_s(cmdline, MAX_STR_LEN, "ImageBootParamsAddr=", sizeof("ImageBootParamsAddr=")-1);

	if (!arg) {
		print_panic("ImageBootParamsAddr not found in cmdline!\n");
		return FALSE;
	}

	param = arg + sizeof("ImageBootParamsAddr=") - 1;

	addr = str2uint(param, 18, NULL, 16);
	if ((addr == (uint32_t)-1) ||
		(addr == 0)) {
		print_panic("Failed to parse image_boot_param_addr!\n");
		return FALSE;
	}
	cmdline_param->image_boot_param_addr = (uint64_t)addr;

	/* Parse ABL.hwver */
	arg = strstr_s(cmdline, MAX_STR_LEN, "ABL.hwver=", sizeof("ABL.hwver=")-1);
	if (!arg) {
		print_panic("ABL.hwver not found in cmdline\n");
		return FALSE;
	}

	param = arg + sizeof("ABL.hwver=") - 1;
	/* The ABL.hwver is like:
	 *	"51,4,1900,b00f,0,8192" which means as below:
	 *      "cpu_stepping,number_of_cores,max_non_turbo_frequency,platform_id,sku,total_amount_of_memory_present"
	 */

	/* skip cpu stepping and get cpu numbers
	 * assume cpu stepping will not longer than 5 */
	param = strstr_s(param, 5, ",", 1);
	if (!param) {
		print_panic("CPU number not found in cmdline!\n");
		return FALSE;
	}
	/* now the param point to the 1st ','
	 * get cpu number from param+1 */
	cpu_num = str2uint(param+1, 2, &param, 10);
	if ((cpu_num > MAX_CPU_NUM) ||
		(cpu_num == 0)) {
		print_panic("CPU number parse failed!\n");
		return FALSE;
	}
	cmdline_param->cpu_num = (uint8_t)cpu_num;

	/* the param should point to the 2nd ','
	 * get max non-turbo frequency from param+1 */
	if(*param != ',') {
		print_panic("CPU frequency not found in cmdline!\n");
		return FALSE;
	}
	cpu_freq = str2uint(param+1, 10, NULL, 10);
	if ((cpu_freq == (uint32_t)-1) ||
		(cpu_freq == 0)) {
		print_panic("CPU frequency parse failed!\n");
		return FALSE;
	}
	cmdline_param->cpu_freq = cpu_freq;

	return TRUE;
}

/*
 * Find boot params from cmdline used to launch stage1, evmm
 * and trusty
 */
boolean_t find_boot_params(cmdline_params_t *cmdline_params,
			abl_trusty_boot_params_t **trusty_boot,
			vmm_boot_params_t **vmm_boot,
			android_image_boot_params_t **and_boot)
{
	uint32_t i;
	uint64_t *p_ImageID;
	image_element_t *ImageElement;
	image_params_t *image_params;

	if (!cmdline_params || !trusty_boot || !vmm_boot || !and_boot)
		return FALSE;

	image_params = (image_params_t *)cmdline_params->image_boot_param_addr;

	if (!image_params)
		return FALSE;

	if (image_params->Version != IMAGE_BOOT_PARAMS_VERSION)
		return FALSE;

	ImageElement = (image_element_t *)(uint64_t)image_params->ImageElementAddr;
	for (i=0; i < image_params->NbImage; i++, ImageElement++) {
		p_ImageID = (uint64_t *)ImageElement->ImageID;

		switch((uint64_t)(*p_ImageID)) {
		case VMM_IMAGE_ID:
			*vmm_boot = (vmm_boot_params_t *)(uint64_t)ImageElement->ImageDataPtr;
			break;
		case TRUSTY_IMAGE_ID:
			*trusty_boot = (abl_trusty_boot_params_t *)(uint64_t)ImageElement->ImageDataPtr;
			break;
		case ANDROID_IMAGE_ID:
			*and_boot = (android_image_boot_params_t *)(uint64_t)ImageElement->ImageDataPtr;
			break;
		default:
			break;
		}
	}

	if (!(*trusty_boot && *vmm_boot && *and_boot))
		return FALSE;

	if (((*trusty_boot)->Version != TRUSTY_BOOT_PARAMS_VERSION) ||
		((*vmm_boot)->Version    != VMM_BOOT_PARAMS_VERSION) ||
		((*and_boot)->Version    != ANDROID_BOOT_PARAMS_VERSION))
		return FALSE;

	if (!(*and_boot)->ImagePreload)
		return FALSE;

	if (!(*vmm_boot)->VMMheapAddr)
		return FALSE;

	return TRUE;
}

void fill_g0gcpu0(gcpu_state_t *evmm_g0gcpu0, cpu_state_t *abl_g0gcpu0)
{
	/* save multiboot initial state */
	/* hardcode GP register sequence here to align with ABL */
	evmm_g0gcpu0->gp_reg[REG_RAX] = abl_g0gcpu0->cpu_gp_register[0];
	evmm_g0gcpu0->gp_reg[REG_RBX] = abl_g0gcpu0->cpu_gp_register[1];
	evmm_g0gcpu0->gp_reg[REG_RCX] = abl_g0gcpu0->cpu_gp_register[2];
	evmm_g0gcpu0->gp_reg[REG_RDX] = abl_g0gcpu0->cpu_gp_register[3];
	evmm_g0gcpu0->gp_reg[REG_RSI] = abl_g0gcpu0->cpu_gp_register[4];
	evmm_g0gcpu0->gp_reg[REG_RDI] = abl_g0gcpu0->cpu_gp_register[5];
	evmm_g0gcpu0->gp_reg[REG_RBP] = abl_g0gcpu0->cpu_gp_register[6];
	evmm_g0gcpu0->gp_reg[REG_RSP] = abl_g0gcpu0->cpu_gp_register[7];

	evmm_g0gcpu0->rip = abl_g0gcpu0->rip;
	evmm_g0gcpu0->rflags = abl_g0gcpu0->rflags;

	evmm_g0gcpu0->gdtr.base       = abl_g0gcpu0->gdtr.base;
	evmm_g0gcpu0->gdtr.limit      = abl_g0gcpu0->gdtr.limit;

	evmm_g0gcpu0->idtr.base       = abl_g0gcpu0->idtr.base;
	evmm_g0gcpu0->idtr.limit      = abl_g0gcpu0->idtr.limit;

	evmm_g0gcpu0->cr0 = abl_g0gcpu0->cr0;
	evmm_g0gcpu0->cr3 = abl_g0gcpu0->cr3;
	evmm_g0gcpu0->cr4 = abl_g0gcpu0->cr4;

	evmm_g0gcpu0->segment[SEG_CS].base       = abl_g0gcpu0->cs.base;
	evmm_g0gcpu0->segment[SEG_CS].limit      = abl_g0gcpu0->cs.limit;
	evmm_g0gcpu0->segment[SEG_CS].attributes = abl_g0gcpu0->cs.attributes;
	evmm_g0gcpu0->segment[SEG_CS].selector   = abl_g0gcpu0->cs.selector;

	evmm_g0gcpu0->segment[SEG_DS].base       = abl_g0gcpu0->ds.base;
	evmm_g0gcpu0->segment[SEG_DS].limit      = abl_g0gcpu0->ds.limit;
	evmm_g0gcpu0->segment[SEG_DS].attributes = abl_g0gcpu0->ds.attributes;
	evmm_g0gcpu0->segment[SEG_DS].selector   = abl_g0gcpu0->ds.selector;

	evmm_g0gcpu0->segment[SEG_SS].base       = abl_g0gcpu0->ss.base;
	evmm_g0gcpu0->segment[SEG_SS].limit      = abl_g0gcpu0->ss.limit;
	evmm_g0gcpu0->segment[SEG_SS].attributes = abl_g0gcpu0->ss.attributes;
	evmm_g0gcpu0->segment[SEG_SS].selector   = abl_g0gcpu0->ss.selector;

	evmm_g0gcpu0->segment[SEG_ES].base       = abl_g0gcpu0->es.base;
	evmm_g0gcpu0->segment[SEG_ES].limit      = abl_g0gcpu0->es.limit;
	evmm_g0gcpu0->segment[SEG_ES].attributes = abl_g0gcpu0->es.attributes;
	evmm_g0gcpu0->segment[SEG_ES].selector   = abl_g0gcpu0->es.selector;

	evmm_g0gcpu0->segment[SEG_FS].base       = abl_g0gcpu0->fs.base;
	evmm_g0gcpu0->segment[SEG_FS].limit      = abl_g0gcpu0->fs.limit;
	evmm_g0gcpu0->segment[SEG_FS].attributes = abl_g0gcpu0->fs.attributes;
	evmm_g0gcpu0->segment[SEG_FS].selector   = abl_g0gcpu0->fs.selector;

	evmm_g0gcpu0->segment[SEG_GS].base       = abl_g0gcpu0->gs.base;
	evmm_g0gcpu0->segment[SEG_GS].limit      = abl_g0gcpu0->gs.limit;
	evmm_g0gcpu0->segment[SEG_GS].attributes = abl_g0gcpu0->gs.attributes;
	evmm_g0gcpu0->segment[SEG_GS].selector   = abl_g0gcpu0->gs.selector;

	evmm_g0gcpu0->segment[SEG_LDTR].base       = abl_g0gcpu0->ldtr.base;
	evmm_g0gcpu0->segment[SEG_LDTR].limit      = abl_g0gcpu0->ldtr.limit;
	evmm_g0gcpu0->segment[SEG_LDTR].attributes = abl_g0gcpu0->ldtr.attributes;
	evmm_g0gcpu0->segment[SEG_LDTR].selector   = abl_g0gcpu0->ldtr.selector;

	evmm_g0gcpu0->segment[SEG_TR].base       = abl_g0gcpu0->tr.base;
	evmm_g0gcpu0->segment[SEG_TR].limit      = abl_g0gcpu0->tr.limit;
	evmm_g0gcpu0->segment[SEG_TR].attributes = abl_g0gcpu0->tr.attributes;
	evmm_g0gcpu0->segment[SEG_TR].selector   = abl_g0gcpu0->tr.selector;

	evmm_g0gcpu0->msr_efer = abl_g0gcpu0->msr_efer;
}
