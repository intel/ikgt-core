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
#include "vmm_asm.h"
#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "ldr_dbg.h"
#include "device_sec_info.h"
#include "stage0_lib.h"
#include "lib/util.h"

#define TRUSTY_BOOT_NULL     (0x00)
#define TRUSTY_BOOT_CS       (0x08)
#define TRUSTY_BOOT_DS       (0x10)

static void fill_code32_seg(segment_t *ss, uint16_t sel)
{
	ss->base = 0;
	ss->limit = 0xffffffff;
	ss->attributes = 0xc09b;
	ss->selector = sel;
}

static void fill_code64_seg(segment_t *ss, uint16_t sel)
{
	ss->base = 0;
	ss->limit = 0xffffffff;
	ss->attributes = 0xa09b;
	ss->selector = sel;
}

static void fill_data_seg(segment_t *ss, uint16_t sel)
{
	ss->base = 0;
	ss->limit = 0xffffffff;
	ss->attributes = 0xc093;
	ss->selector = sel;
}

static void fill_tss_seg(segment_t *ss, uint16_t sel)
{
	ss->base = 0;
	ss->limit = 0xffffffff;
	ss->attributes = 0x808b;
	/* it is ok for TR to be NULL while the attribute is not 0.
	 * vmentry will not check it and guest OS will set the correct TR later */
	ss->selector = sel;
}

static void fill_unused_seg(segment_t *ss)
{
	ss->base = 0;
	ss->limit = 0;
	ss->attributes = 0x10000;
	ss->selector = 0;
}

void save_current_cpu_state(gcpu_state_t *s)
{
	asm_sgdt(&(s->gdtr));
	asm_sidt(&(s->idtr));
	s->cr0 = asm_get_cr0();
	s->cr3 = asm_get_cr3();
	s->cr4 = asm_get_cr4();

	s->msr_efer = asm_rdmsr(MSR_EFER);

	/* The selector of LDTR in current environment is invalid which indicates
	 * the bootloader is not using LDTR. So set LDTR unusable here. In
	 * future, exception might occur if LDTR is used in bootloader. Then bootloader
	 * will find us since we changed LDTR to 0, and we can fix it for that bootloader. */
	fill_unused_seg(&s->segment[SEG_LDTR]);
	/* TSS is used for RING switch, which is usually not used in bootloader since
	 * bootloader always runs in RING0. So we hardcode TR here. In future, #TS
	 * might occur if TSS is used bootloader. Then bootlaoder will find us since we
	 * changed TR to 0, and we can fix it for that bootlaoder. */
	fill_tss_seg(&s->segment[SEG_TR], 0);
	/* For segments: get selector from current environment, selector of ES/FS/GS are from DS,
	 * hardcode other fields to make guest launch successful. */
	fill_code64_seg(&s->segment[SEG_CS], asm_get_cs());
	fill_data_seg(&s->segment[SEG_DS], asm_get_ds());
	fill_data_seg(&s->segment[SEG_ES], asm_get_ds());
	fill_data_seg(&s->segment[SEG_FS], asm_get_ds());
	fill_data_seg(&s->segment[SEG_GS], asm_get_ds());
	fill_data_seg(&s->segment[SEG_SS], asm_get_ss());
}

/* This funtion will set whole trusty gcpu state, except
 * 1. RIP and RDI, they will be set in VMM
 * 2. Fields should be set to 0. trusty_desc already be cleared to 0 earlier.
 */
void trusty_gcpu_setup(trusty_desc_t *trusty_desc)
{
	/* Stack resides at end of trusty runtime memory */
	trusty_desc->gcpu0_state.gp_reg[REG_RSP] = trusty_desc->lk_file.runtime_addr + trusty_desc->lk_file.runtime_total_size;

	trusty_desc->gcpu0_state.rflags = 0x3002;

	fill_code32_seg(&trusty_desc->gcpu0_state.segment[SEG_CS], TRUSTY_BOOT_CS);
	fill_data_seg(&trusty_desc->gcpu0_state.segment[SEG_DS], TRUSTY_BOOT_DS);
	fill_data_seg(&trusty_desc->gcpu0_state.segment[SEG_ES], TRUSTY_BOOT_DS);
	fill_data_seg(&trusty_desc->gcpu0_state.segment[SEG_FS], TRUSTY_BOOT_DS);
	fill_data_seg(&trusty_desc->gcpu0_state.segment[SEG_GS], TRUSTY_BOOT_DS);
	fill_data_seg(&trusty_desc->gcpu0_state.segment[SEG_SS], TRUSTY_BOOT_DS);
	fill_tss_seg(&trusty_desc->gcpu0_state.segment[SEG_TR], TRUSTY_BOOT_NULL);
	trusty_desc->gcpu0_state.segment[SEG_LDTR].attributes = 0x010000;

	trusty_desc->gcpu0_state.cr0 = 0x11;
}

void make_dummy_trusty_info(void *info)
{
	device_sec_info_v0_t *device_sec_info = (device_sec_info_v0_t *)info;

	memset(device_sec_info, 0, sizeof(device_sec_info_v0_t));

	device_sec_info->size_of_this_struct = sizeof(device_sec_info_v0_t);
	device_sec_info->version = 0;
	device_sec_info->platform = 0;
	device_sec_info->num_seeds = 1;
}
