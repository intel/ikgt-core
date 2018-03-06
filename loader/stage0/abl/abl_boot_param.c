/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
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

#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "abl_boot_param.h"
#include "trusty_info.h"
#include "guest_setup.h"
#include "stage0_lib.h"
#include "ldr_dbg.h"
#include "device_sec_info.h"

#include "lib/util.h"
#include "lib/string.h"

#define CHECK_FLAG(flag,bit)	((flag) & (1 << (bit)))

#define STAGE1_IMG_SIZE            0xC000

/* arguments parsed from cmdline */
typedef struct boot_param {
	uint64_t image_boot_param_addr;
	uint64_t cpu_num;
	uint64_t cpu_freq;
} boot_param_t;

/* loader memory layout */
typedef struct {
	/* below must be page aligned for
	 * further ept protection */
	/* stage1 image in RAM */
	uint8_t stage1[STAGE1_IMG_SIZE];

	evmm_desc_t xd;

	device_sec_info_v0_t device_sec_info;

	/* add more if any */
} memory_layout_t;

static void fill_evmm_boot_params(evmm_desc_t *evmm_desc, vmm_boot_params_t *vmm_boot_params)
{
	memory_layout_t *loader_mem;
	loader_mem = (memory_layout_t *)(uint64_t)vmm_boot_params->VMMheapAddr;

	evmm_desc->evmm_file.runtime_addr = (uint64_t)vmm_boot_params->VMMMemBase;
	evmm_desc->evmm_file.runtime_total_size = ((uint64_t)(vmm_boot_params->VMMMemSize)) << 10;

	evmm_desc->stage1_file.runtime_addr = (uint64_t)loader_mem->stage1;
	evmm_desc->stage1_file.runtime_total_size = STAGE1_IMG_SIZE;

	evmm_desc->sipi_ap_wkup_addr = (uint64_t)vmm_boot_params->VMMSipiApWkupAddr;
}

static boolean_t fill_device_sec_info(device_sec_info_v0_t *device_sec_info, abl_trusty_boot_params_t *trusty_boot_params, const char *serial)
{
	uint32_t i;

	device_sec_info->size_of_this_struct = sizeof(device_sec_info_v0_t);
	device_sec_info->version = 0;
	device_sec_info->flags = 0x1 | 0x0 | 0x0; // in manufacturing mode | secure boot disabled | production seed
	device_sec_info->platform = 1; // APL + ABL

	if (trusty_boot_params->num_seeds > ABL_SEED_LIST_MAX) {
		print_panic("Number of seeds exceeds predefined max number!\n");
		return FALSE;
	}

	device_sec_info->num_seeds = trusty_boot_params->num_seeds;

	memset(device_sec_info->dseed_list, 0, sizeof(device_sec_info->dseed_list));
	/* copy seed_list from ABL to device_sec_info->dseed_list */
	for (i = 0; i < (trusty_boot_params->num_seeds); i++) {
		device_sec_info->dseed_list[i].cse_svn = trusty_boot_params->seed_list[i].svn;
		memcpy(device_sec_info->dseed_list[i].seed, trusty_boot_params->seed_list[i].seed, ABL_SEED_LEN);
	}
	/* Do NOT erase seed here, OSloader need to derive RPMB key from seed */
	//memset(abl_trusty_boot_params->seed_list, 0, sizeof(abl_trusty_boot_params->seed_list));

	memcpy(device_sec_info->serial, serial, sizeof(device_sec_info->serial));

	return TRUE;
}

static void fill_trusty_desc(trusty_desc_t *trusty_desc, device_sec_info_v0_t *device_sec_info, abl_trusty_boot_params_t *trusty_boot_params)
{
	/* get lk runtime addr/total_size */
	trusty_desc->lk_file.runtime_addr = (uint64_t)trusty_boot_params->TrustyMemBase;
	trusty_desc->lk_file.runtime_total_size = ((uint64_t)(trusty_boot_params->TrustyMemSize)) << 10;
	trusty_desc->dev_sec_info = device_sec_info;
}

static boolean_t get_emmc_serial(android_image_boot_params_t *android_boot_params, char *serial)
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
static boolean_t cmdline_parse(multiboot_info_t *mbi, boot_param_t *boot_param)
{
	const char *cmdline;
	const char *arg;
	const char *param;
	uint32_t addr, cpu_num, cpu_freq;

	if (!mbi || !boot_param)
		return FALSE;

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
	boot_param->image_boot_param_addr = (uint64_t)addr;

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
	boot_param->cpu_num = (uint8_t)cpu_num;

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
	boot_param->cpu_freq = cpu_freq;

	return TRUE;
}

static evmm_desc_t *setup_boot_params(uint64_t image_boot_params_addr)
{
	vmm_boot_params_t *vmm_boot_params = NULL;
	abl_trusty_boot_params_t *trusty_boot_params = NULL;
	android_image_boot_params_t *android_boot_params = NULL;
	uint32_t i;
	evmm_desc_t *evmm_desc;
	uint64_t *p_ImageID;
	memory_layout_t *loader_mem;
	image_element_t *ImageElement;
	image_boot_params_t *image_boot_params = (image_boot_params_t *)image_boot_params_addr;

	char serial[MMC_PROD_NAME_WITH_PSN_LEN] = {0};

	if (!image_boot_params)
		return NULL;
	if (image_boot_params->Version != IMAGE_BOOT_PARAMS_VERSION)
		return NULL;

	ImageElement = (image_element_t *)(uint64_t)image_boot_params->ImageElementAddr;
	for (i=0; i < image_boot_params->NbImage; i++, ImageElement++) {

		p_ImageID = (uint64_t *)ImageElement->ImageID;

		switch((uint64_t)(*p_ImageID)) {
		case VMM_IMAGE_ID:
			vmm_boot_params = (vmm_boot_params_t *) (uint64_t)ImageElement->ImageDataPtr;
			break;
		case TRUSTY_IMAGE_ID:
			trusty_boot_params = (abl_trusty_boot_params_t *) (uint64_t)ImageElement->ImageDataPtr;
			break;
		case ANDROID_IMAGE_ID:
			android_boot_params = (android_image_boot_params_t *) (uint64_t)ImageElement->ImageDataPtr;
			break;
		default:
			break;
		}
	}

	if (!(vmm_boot_params && trusty_boot_params && android_boot_params))
		return NULL;

	if ((vmm_boot_params->Version != VMM_BOOT_PARAMS_VERSION) ||
		(trusty_boot_params->Version != TRUSTY_BOOT_PARAMS_VERSION) ||
		(android_boot_params->Version != ANDROID_BOOT_PARAMS_VERSION))
		return NULL;

	if (!android_boot_params->ImagePreload)
		return NULL;

	loader_mem = (memory_layout_t *)(uint64_t)vmm_boot_params->VMMheapAddr;

	evmm_desc = &(loader_mem->xd);
	memset(evmm_desc, 0, sizeof(evmm_desc_t));

	fill_evmm_boot_params(evmm_desc, vmm_boot_params);
	g0_gcpu_setup(evmm_desc, android_boot_params);

	if (!get_emmc_serial(android_boot_params, serial)) {
		print_panic("Failed to search EMMC serial number string from Guest cmdline!\n");
		return NULL;
	}

	if(!fill_device_sec_info(&(loader_mem->device_sec_info), trusty_boot_params, serial)) {
		print_panic("Failed to fill trusty boot info!\n");
		return NULL;
	}

	fill_trusty_desc(&(loader_mem->xd.trusty_desc), &(loader_mem->device_sec_info), trusty_boot_params);

	return evmm_desc;
}

evmm_desc_t *boot_params_parse(const init_register_t *init_reg)
{
	evmm_desc_t *evmm_desc = NULL;
	boot_param_t boot_param;
	uint64_t tom = 0;
	boolean_t ret = FALSE;

	multiboot_info_t *mbi = (multiboot_info_t *)(uint64_t)init_reg->ebx;

	ret = cmdline_parse(mbi, &boot_param);
	if (!ret) {
		print_panic("cmdline parse failed!\n");
		return NULL;
	}

	evmm_desc = setup_boot_params(boot_param.image_boot_param_addr);
	if (!evmm_desc) {
		print_panic("Failed to setup boot params!\n");
		return NULL;
	}

	tom = get_top_of_memory(mbi);
	if (tom == 0) {
		print_panic("Failed to get top_of memory from mbi!\n");
		return NULL;
	}
	evmm_desc->top_of_mem = tom;

	evmm_desc->num_of_cpu = boot_param.cpu_num;
	evmm_desc->tsc_per_ms = boot_param.cpu_freq * 1000ULL;

	return evmm_desc;
}
