################################################################################
# Copyright (c) 2017 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

This readme covers instructions for building and launching evmm, which is a core
VTx layer.

=============================================================================
Content of the Source Package
=============================================================================

	vmm
	This contains evmm source files and build makefiles

	loader
	This contains pre-OS loader for evmm launch

	packer
	This contains packer tool to build final image from vmm and loader

	product
	This contains config file for different boards

=============================================================================
Config files
=============================================================================

	below configures should be changed if you are using different boards:
	- DEBUG
		Description: Enable debug build.
	- LOG_LEVEL
		Description: Define log level (1:panic, 2:info, 3:warning, 4:trace).
	- MAX_CPU_NUM
		Description: Must have. It is the max cpu number of the board.
	- TOP_OF_MEM
		Description: Optional, in some platform, it is must have. it is the "top of memory" from e820 table. Note: it is NOT memory size. for example, in GordenPeak, it have 8G memory but the top of memory is 10G
	- TSC_PER_MS
		Description: Optional, in some platform, it is must have. It can be got from kernel log, eg. "[    0.000000] tsc: Detected 1881.600 MHz processor". TSC_PER_MS is defined in KHz.
	- CPU_NUM
		Description: Optional (if you don't know the value, or it should be calculated dynamically, you should remove it). It is the number of the cpus in your board.
					 When it is defined, it must be equal to MAX_CPU_NUM.
	- LOADER_STAGE0_SUB
		Description: Must have. it specifies which stage0 is used.
	- EPT_POLICY
		Description: Default ept policy.
	- EVMM_PKG_BIN_SIZE
		Description: Used to check if the final evmm_pkg.bin (or ikgt_pkg.bin) exceeds the size.
	- STACK_PROTECTOR
		Description: make use of gcc to emit extra code and check buffer overflows.
	- SYNC_CPU_IN_BOOT
		Description: bsp wait for ap before the first guest launched.

	- LIB_LAPIC_IPI
		Description: Provide api to send IPI.

	- LIB_MP_INIT
		Description: Wakeup ap from real mode to 64 bit mode.
		Dependency:
			LIB_LAPIC_IPI

	- LIB_PCI
		Description: Provide pci read/write functions.

	- LIB_PRINT
		Description: Provide print functions.
		Dependency:
			LIB_PCI (optional)
		SubFlags:
			- SERIAL_MMIO
				Description: Determine whether the print devices uses IO or MMIO.
			- SERIAL_PCI
				Description: If print device is a PCI device, the PCI bus/device/function must be specified here.
				Dependency: LIB_PCI
				SubFlags:
					- SERIAL_PCI_BUS
					- SERIAL_PCI_DEV
					- SERIAL_PCI_FUN
			- SERIAL_BASE
				Description: If SERIAL_PCI is not define, serial base (IO or MMIO) should be specified by this flag.

	- LIB_EFI_SERVICES
		Description: Support UEFI services
		SubFlags:
			- START_AP_BY_EFI_MP_SERVICE
				Description: Startup APs by EFI_MP_SERVICES

	- MODULE_VMCALL
		Description: Vmcall support.

	- MODULE_DEADLOOP
		Description: Inject GP to guest 0 when system hang in host.

	- MODULE_ACPI
		Description: Provide api to search ACPI tables.

	- MODULE_IO_MONITOR
		Description: Provide api to monitor IO access.

	- MODULE_SUSPEND
		Description: S3 support.
		Dependency:
			LIB_MP_INIT,
			MODULE_IO_MONITOR,
			MODULE_ACPI,
			MODULE_IPC

	- MODULE_LAPIC_ID
		Description: Provide api to get APIC ID according to host cpu id.
		Dependency:
			LIB_LAPIC_IPI

	- MODULE_IPC
		Description: Execute function in other cpus.
		Dependency:
			LIB_LAPIC_IPI,
			MODULE_LAPIC_ID (optional)

	- MODULE_DR
		Description: Isolation for dr0~3, 6.

	- MODULE_CR
		Description: Isolation for CR2, CR8.

	- MODULE_XSAVE
		Description: Isolation for fpu/mmx/avx registers.

	- MODULE_FXSAVE
		Description: Isolation for fpu/mmx/avx registers.

	- MODULE_MSR_ISOLATION
		Description: Provide api to isolate MSRs.
		SubFlags:
			- MAX_ISOLATED_MSR_COUNT
				Description: Specify the max msr count to be isolated.

	- MODULE_MSR_MONITOR
		Description: Provide api to monitor MSR access.

	- MODULE_TRUSTY_GUEST
		Description: Enable Trusty.
		Dependency:
			LIB_IPC (optional),
			MODULE_VMCALL,
			MODULE_MSR_ISOLATION(optional),
			MODULE_DEADLOOP(optional)
		SubFlags:
			- ENABLE_SGUEST_SMP
				Description: Enable SMP for LK.
				Dependency: LIB_IPC
			- DMA_FROM_CSE
				Description: Optional, allow CSE device access multi guests' memory by DMA. This macro should take the value of PCI device id(PCI_DEV(Bus:Device:Func));
		- PACK_LK
			Description: pack lk.elf into evmm_pkg.bin
			- DERIVE_KEY
				Description: Derive key
				Dependency: MODULE_CRYPTO

	- MODULE_VTD
		Description: Enable VT-d. Current policy for VT-d is to use same memory layout as guest 0 (Android).
		Dependency:
			MODULE_ACPI
		SubFlags:
			- DMAR_MAX_ENGINE
				Description: Specify max DMAR engine in system. usually it is 4.
			- SKIP_DMAR_GPU
				Description: Workaround for bug OAM-42091, conflict with GFX.
			- MULTI_GUEST_DMA
				Description: Optional, allow a device access multi guests' memory by DMA.

	- MODULE_DEV_BLK
		Description: provide API to block access to devices from guests.
	Dependency:
		LIB_PCI(optional),
		MODULE_ACPI(optional),
		MODULE_IO_MONITOR(optional)

	- MODULE_VMX_TIMER
		Description: Provide api to use vmx timer.

	- MODULE_TSC
		Description: Allow guest 0 to modify TSC while keep other guests unaffected.
		Dependency:
			MODULE_MSR_MONITOR

	- MODULE_EXT_INTR
	Description: monitor all external interrupts.

	- MODULE_VMEXIT_INIT
	Description: handle INIT vmexit event. This module handle INIT signal properly when guest disable/enable CPUs(AP) at runtime.

	- MODULE_VMENTER_CHECK
		Description: For debug. Check vmentry failure reasons.

	- MODULE_VMEXIT_TASK_SWITCH
		Description: Support task switch in vmexit.

	- MODULE_PROFILE
		SubFlags:
		- STACK_PROFILE
		Description: For debug. Profile EVMM stack usage
		- TIME_PROFILE
		Description: For debug. Profile guest OS and EVMM performance

	- MODULE_INTERRUPT_IPI
		Description: Deliver interrupt to Guest by Self-IPI.

	- MODULE_PERF_CTRL_ISOLATION
		Description: Monitor IA32_PERF_GLOBAL_CTRL MSR and do guest-host/guest-guest isolation.

	- MODULE_SPECTRE
		Description: To prevent spectre attack.

	- MODULE_INSTRUCTION_DECODE
		Description: Support instruction decode

	- AP_START_IN_HLT
		Description: Set processors to HLT when init. Default is WAIT_FOR_SIPI.

	- MODULE_CRYPTO
		Description: add hkdf and kdf crypto lib support

	- MODULE_APS_STATE
		Description: set Guest APs to init state

End of file
