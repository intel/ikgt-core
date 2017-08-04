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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRUSTY_VMCALL_VMM_PROFILE 0x6C696E01
#define PROFILE_BUF_SIZE (4*4096)
#define CYCLE_TO_US(cycle, tsc_per_ms) ((cycle)*1000/(tsc_per_ms))
#define CYCLE_TO_MS(cycle, tsc_per_ms) ((cycle)/(tsc_per_ms))
#define CYCLE_TO_S(cycle, tsc_per_ms) ((cycle)/tsc_per_ms/1000)
#define ARGS_SIZE 16

/* Sample of the profiling data buffer*/
typedef struct {
	uint32_t index;             // range 0~(2<<32-1)
	uint8_t  vmexit_guest_id;   // current guest id will exit
	uint8_t  vmexit_gcpu_id;    // current gcpu id will exit
	uint16_t vmexit_reason;     // current vm VMEXIT reason
	uint64_t vmexit_tsc;        // Start TSC of current VMEXIT
	uint64_t vmenter_tsc;       // Start TSC of next VMRESUME
	uint8_t  vmenter_guest_id;  // Resume guest id
	uint8_t  vmenter_gcpu_id;   // Resume gcpu id
	uint16_t reserve_1;
	uint32_t avail;             // Verify sample available
} profile_data_t;

/* Head of the profiling data buffer */
typedef struct {
	uint32_t put;
	uint32_t max_count;
	uint32_t tsc_per_ms;
	uint32_t pad;
	profile_data_t data[0];
} module_profile_t;

static uint32_t avail_sample_count;

static int trusty_vmm_profile(void *gva)
{
	int ret = -1;

	__asm__ __volatile__(
		"vmcall"
		: "=a"(ret)
		: "a"(TRUSTY_VMCALL_VMM_PROFILE), "D"(gva)
	);

	return ret;
}

static int get_gcpu_num(void *p, uint8_t guest_id)
{
	module_profile_t *pmod = (module_profile_t *)p;
	profile_data_t *ptmp = pmod->data;
	uint32_t i = 0;
	int max_gcpu_id = 0;

	for (i = 0; i < avail_sample_count; i++) {
		if (ptmp->vmexit_guest_id == guest_id && ptmp->vmexit_gcpu_id > max_gcpu_id) {
			max_gcpu_id = ptmp->vmexit_gcpu_id;
		}
		ptmp ++;
	}

	return (max_gcpu_id + 1);
}

static int get_guest_num(void *p)
{
	module_profile_t *pmod = (module_profile_t *)p;
	profile_data_t *ptmp = pmod->data;
	uint8_t max_gid = 0;
	uint32_t i = 0;

	for (i = 0; i < avail_sample_count; i++) {
		if (ptmp->vmexit_guest_id > max_gid) {
			max_gid = ptmp->vmexit_guest_id;
		}
		ptmp ++;
	}

	return (max_gid + 1);
}

static profile_data_t *find_vmexit_sample(void *p, uint8_t guest_id, uint32_t gcpu_id, uint32_t sample_count)
{
	profile_data_t *ptmp = (profile_data_t *)p;
	uint32_t i = 0;

	for (i = 0; i < sample_count; i++) {
		if (ptmp->vmexit_guest_id == guest_id && ptmp->vmexit_gcpu_id == gcpu_id) {
			return ptmp;
		}
		ptmp ++;
	}

	return NULL;
}

static uint64_t get_cycles_for_gcpu(void *p, uint8_t guest_id, uint32_t gcpu_id)
{
	module_profile_t *pmod = (module_profile_t *)p;
	profile_data_t *pend, *ptmp = pmod->data;
	uint32_t i = 0;
	uint64_t cycles = 0;

	for (i = 0; i < avail_sample_count - 1; i++) {
		if (ptmp->vmenter_guest_id == guest_id &&  ptmp->vmenter_gcpu_id == gcpu_id) {
			pend = find_vmexit_sample(ptmp + 1, ptmp->vmenter_guest_id, ptmp->vmenter_gcpu_id, avail_sample_count - i -1);

			if (pend) {
				cycles += pend->vmexit_tsc - ptmp->vmenter_tsc;
			} else {
				cycles += (pmod->data + avail_sample_count -1)->vmenter_tsc - ptmp->vmenter_tsc;
			}
		}
		ptmp ++;
	}

	return cycles;
}

static uint64_t get_vmm_cycles(void *p)
{
	module_profile_t *pmod = (module_profile_t *)p;
	profile_data_t *pend, *ptmp = pmod->data;
	uint32_t i = 0;
	uint64_t cycles = 0;

	for (i = 0; i < avail_sample_count; i++) {
		cycles += ptmp->vmenter_tsc - ptmp->vmexit_tsc;
		ptmp ++;
	}

	return cycles;
}

static uint64_t get_total_cycles(void *p)
{
	module_profile_t *pmod = (module_profile_t *)p;
	profile_data_t *pend, *ptmp = pmod->data;
	int i = 0, j = 0;
	int guest_num, gcpu_num;
	uint64_t total_cycles = 0, guest_cycles = 0, vmm_cycles, gcpu_cycles;

	guest_num = get_guest_num(p);
	vmm_cycles = get_vmm_cycles(p);

	for (i = 0; i< guest_num; i++) {
		gcpu_num = get_gcpu_num(p, i);
		guest_cycles = 0;
		for (j = 0; j < gcpu_num; j++) {
			gcpu_cycles = get_cycles_for_gcpu(p, i, j);
			guest_cycles += gcpu_cycles;
		}
		total_cycles += guest_cycles;
	}

	total_cycles += vmm_cycles;

	return total_cycles;
}

static void output_rawdata(void *p)
{
	uint32_t i = 0;
	module_profile_t *pmod = (module_profile_t *)p;
	profile_data_t *ptmp = pmod->data;
	uint32_t tsc_per_ms = pmod->tsc_per_ms;

	printf("index  vmexit_guest_id vmexit_gcpu_id vmexit_tsc            vmenter_tsc      vmexit_reason resume_guestid resume_cpuid\n");

	for (i = 0; i < avail_sample_count; i++) {
		printf("%3d  %6d           %2u            0x%016lX    0x%016lX       %2u           %2d            %2d\n",
				i,
				ptmp->vmexit_guest_id,
				ptmp->vmexit_gcpu_id,
				ptmp->vmexit_tsc,
				ptmp->vmenter_tsc,
				ptmp->vmexit_reason,
				ptmp->vmenter_guest_id,
				ptmp->vmenter_gcpu_id);
		ptmp ++;
	}

	return;
}

static void output_process_data(void *p, int log_type)
{
	module_profile_t *pmod = (module_profile_t *)p;
	profile_data_t *pend, *ptmp = pmod->data;
	uint32_t tsc_per_ms = pmod->tsc_per_ms;
	uint32_t i = 0;

	if (log_type == 1) {
		printf("guest_id  gcpu_id  guest_gcpu_cycles  guest_gcpu_us  vmexit_reason   vmm_cycles         vmm_us\n");
	}

	for (i = 0; i < avail_sample_count - 1; i++) {
		if (ptmp->index != ptmp->avail) {
			printf("ignore this sample, i = %d\n", i);
			continue;
		}

		pend = find_vmexit_sample(ptmp + 1, ptmp->vmenter_guest_id, ptmp->vmenter_gcpu_id, avail_sample_count - i - 1);

		if (pend) {
			if (log_type == 0) {
				printf("guest[%d] runs on gcpu[ %u] cycles: 0x%016lX[%8lu]us vmexit_reason = %2u, vmm takes: 0x%012lX cycles [%5lu]us\n",
					ptmp->vmenter_guest_id,
					ptmp->vmenter_gcpu_id,
					pend->vmexit_tsc - ptmp->vmenter_tsc,
					CYCLE_TO_US(pend->vmexit_tsc - ptmp->vmenter_tsc, tsc_per_ms),
					pend->vmexit_reason,
					pend->vmenter_tsc - pend->vmexit_tsc,
					CYCLE_TO_US(pend->vmenter_tsc - pend->vmexit_tsc, tsc_per_ms));
			} else {
				printf("%d   %8u      0x%016lX   %10lu   %10u       0x%012lX   %7lu\n",
					ptmp->vmenter_guest_id,
					ptmp->vmenter_gcpu_id,
					pend->vmexit_tsc - ptmp->vmenter_tsc,
					CYCLE_TO_US(pend->vmexit_tsc - ptmp->vmenter_tsc, tsc_per_ms),
					pend->vmexit_reason,
					pend->vmenter_tsc - pend->vmexit_tsc,
					CYCLE_TO_US(pend->vmenter_tsc - pend->vmexit_tsc, tsc_per_ms));
			}
		}
		ptmp ++;

	}

	return;
}

static void output_result_summary(void *p)
{
	module_profile_t *pmod = (module_profile_t *)p;
	profile_data_t *pend, *ptmp = pmod->data;
	int i = 0, j = 0;
	int guest_num, gcpu_num;
	uint64_t total_cycles;
	uint64_t measured_duration_tsc;
	uint64_t guest_cycles;
	uint64_t gcpu_cycles;
	uint64_t vmm_cycles;
	uint32_t tsc_per_ms = pmod->tsc_per_ms;

	guest_num = get_guest_num(p);
	vmm_cycles = get_vmm_cycles(p);
	total_cycles = get_total_cycles(p);
	measured_duration_tsc = (ptmp + avail_sample_count - 1)->vmenter_tsc - ptmp->vmenter_tsc;

	printf("\nSummary of the profile result: \n");
	printf("    Total measured time: %2.2fs, Start at %2.2fs, End at: %2.2fs\n",
			CYCLE_TO_S(measured_duration_tsc, (float)tsc_per_ms),
			CYCLE_TO_S(ptmp->vmenter_tsc, (float)tsc_per_ms),
			CYCLE_TO_S((ptmp + avail_sample_count - 1)->vmenter_tsc, (float)tsc_per_ms));

	printf("    vmm takes %3.3f%% %6lums\n",
			(float)100 * vmm_cycles/total_cycles,
			CYCLE_TO_MS(vmm_cycles, tsc_per_ms));

	for (i=0; i< guest_num; i++) {
		gcpu_num = get_gcpu_num(p, i);
		guest_cycles = 0;
		for (j = 0; j < gcpu_num; j++) {
			gcpu_cycles = get_cycles_for_gcpu(p, i, j);

			printf("                                -- guest[%d]gcpu[%d] takes %6lums \n",
					i, j, CYCLE_TO_MS(gcpu_cycles, tsc_per_ms));
			guest_cycles += gcpu_cycles;
		}
		printf("    guest[%d] takes %3.3f%% %3lums\n",
				i, (float)100 * guest_cycles/total_cycles, CYCLE_TO_MS(guest_cycles, tsc_per_ms));
	}

	return;
}

/* reoder sample data from old to new for convenient calculation */
static void *data_reorder(void *p)
{
	uint32_t size, put;
	module_profile_t *pmod_profile = (module_profile_t *)p;
	module_profile_t *pmod_profile_reorder = NULL;
	profile_data_t *pdata;

	pmod_profile_reorder = (module_profile_t *)malloc(PROFILE_BUF_SIZE);
	if (!pmod_profile_reorder) {
		printf("malloc failed\n");
		return NULL;
	}

	memset(pmod_profile_reorder, 0, (uint64_t)PROFILE_BUF_SIZE);

	memcpy((void *)pmod_profile_reorder, (const void *)pmod_profile, sizeof(module_profile_t));

	put = pmod_profile->put%pmod_profile->max_count;
	size = (pmod_profile->max_count - put - 1) * sizeof(profile_data_t);
	pdata = pmod_profile->data;
	memcpy((void *)pmod_profile_reorder->data, (const void *)(pdata + put + 1), size);


	size = (put + 1)  * sizeof(profile_data_t);
	pdata = pmod_profile_reorder->data;
	memcpy((void *)(pdata + pmod_profile->max_count - put - 1), (const void *)pmod_profile->data, size);

	if (pmod_profile->put >= pmod_profile->max_count) {
		avail_sample_count = pmod_profile->max_count;
	} else {
		avail_sample_count = pmod_profile->put + 1;
	}

	return (void *)pmod_profile_reorder;
}

int main(int argc, char **argv)
{
	int result;
	module_profile_t *pmod_profile = NULL;
	module_profile_t *pmod_profile_reorder = NULL;
	uint32_t dump_raw = 0;
	uint32_t dump_processed_data = 0;
	uint32_t dump_processed_details = 0;

	if (argc > 1) {
		if (strncmp("raw", argv[1], ARGS_SIZE) == 0) {
			dump_raw = 1;
		} else if (strncmp("processed", argv[1], ARGS_SIZE) == 0) {
			dump_processed_data = 1;
		} else if (strncmp("details", argv[1], ARGS_SIZE) == 0) {
			dump_processed_details = 1;
		} else if (strncmp("all", argv[1], ARGS_SIZE) == 0) {
			dump_raw = 1;
			dump_processed_data = 1;
			dump_processed_details = 1;
		} else {
			printf("usage: %s [raw|processed|details|all]\n", argv[0]);
			return -1;
		}
	}

	pmod_profile = (module_profile_t *)malloc(PROFILE_BUF_SIZE);
	if (!pmod_profile) {
		printf("malloc failed\n");
		return -1;
	}

	memset((void *)pmod_profile, 0, PROFILE_BUF_SIZE);

	result = trusty_vmm_profile((void *)pmod_profile);
	if (result < 0) {
		printf("Failed to get profile data from VMM\n");
		goto cleanup;
	}

	pmod_profile_reorder = (module_profile_t *)data_reorder((void *)pmod_profile);
	if (!pmod_profile_reorder) {
		goto cleanup;
	}

	/*output raw data*/
	if (dump_raw) {
		output_rawdata(pmod_profile_reorder);
	}

	/*output processed data*/
	if (dump_processed_data) {
		output_process_data(pmod_profile_reorder, 1);
	}

	/*output processed data with description*/
	if (dump_processed_details) {
		output_process_data(pmod_profile_reorder, 0);
	}

	/*output summary*/
	output_result_summary(pmod_profile_reorder);
cleanup:
	if (pmod_profile) {
		free(pmod_profile);
	}

	if (pmod_profile_reorder) {
		free(pmod_profile_reorder);
	}

	return 0;
}
