/*
 * Copyright (c) 2015-2022 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "gcpu.h"
#include "host_cpu.h"
#include "kvm_workaround.h"

#include "modules/io_monitor.h"
#include "modules/ipc.h"

#define CF9_BIT_SYS_RST  (1u << 1)
#define CF9_BIT_RST_CPU  (1u << 2)
#define CF9_BIT_FULL_RST (1u << 3)

static void clear_vmx(guest_cpu_handle_t gcpu, void *vmx_off_cpu_cnt)
{
	guest_cpu_handle_t gcpu_next = gcpu;

	do {
		vmcs_clr_ptr(gcpu_next->vmcs);

		gcpu_next = gcpu_next->next_same_host_cpu;
	} while (gcpu_next != gcpu);

	vmx_off();

	/* vmx_off_cpu_cnt is NULL when calling from BSP */
	if (vmx_off_cpu_cnt) {
		(*((uint16_t *)vmx_off_cpu_cnt))++;
		__STOP_HERE__;
	}
}

static void cf9_io_write_handler(guest_cpu_handle_t gcpu,
				 uint16_t port_id,
				 uint32_t port_size,
				 uint32_t value)
{
	if ((value & CF9_BIT_SYS_RST) && (value & CF9_BIT_RST_CPU)) {
		/* Clear VMX on all cpus */
		uint16_t vmx_off_cpu_cnt = 0;
		ipc_exec_on_all_other_cpus(clear_vmx, &vmx_off_cpu_cnt);

		while (vmx_off_cpu_cnt < (host_cpu_num - 1)) {
			asm_pause();
		}

		clear_vmx(gcpu, NULL);
		io_transparent_write_handler(gcpu, port_id, port_size, value);

		/* should never reach here */
		__STOP_HERE__;
	}

	io_transparent_write_handler(gcpu, port_id, port_size, value);
}

/*
 * When running on KVM, the nVMX state is not cleared when L2 perform a reset through
 * 0xCF9. So here monitored 0xCF9 and take action accordingly.
 * The issue starts from kernel-5.13.0-30, and introduced by below two patches:
 *    https://patchwork.kernel.org/project/kvm/patch/20201007014417.29276-5-sean.j.christopherson@intel.com/
 *    https://patchwork.kernel.org/project/kvm/patch/20201007014417.29276-6-sean.j.christopherson@intel.com/
 * Issue track:
 *    https://bugzilla.kernel.org/show_bug.cgi?id=215964
 *    https://gitlab.com/qemu-project/qemu/-/issues/1021
 */
void reset_init(void)
{
	if (running_on_kvm()) {
		io_monitor_register(0, 0xCF9, NULL, cf9_io_write_handler);
		/*
		 * According to Intel SDM - Volume 1 - Chapter 19 INPUT/OUTPUT - 19.3 I/O Address Space
		 * The I/O address space consists of 216 (64K) individually addressable 8-bit I/O ports.
		 * Any two consecutive 8-bit ports can be treated as a 16-bit port, and any four consecutive
		 * ports can be a 32-bit port.
		 * So When monitor 0xCF9, below port accessing will cause VMExit:
		 *     0xCF9 -- 8/16/32 bit size
		 *     0xCF8 --   16/32 bit size
		 *     0xCF7 --      32 bit size
		 *     0xCF6 --      32 bit size
		 *
		 * But currently, only accessing to 0xCF8 with 32 bit size is noticed during testing.
		 * So here monitored 0xCF8 as well to avoid assert in VMM. May need to revisit I/O
		 * minitor logic in future if meet any issue. Add read handler to avoid assert when
		 * in DEBUG build.
		 * TODO: revisit I/O port monitor/handle logic.
		 */
		io_monitor_register(0, 0xCF8, io_transparent_read_handler, NULL);
	}
}

