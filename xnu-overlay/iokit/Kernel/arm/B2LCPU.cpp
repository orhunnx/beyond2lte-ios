/* SPDX-License-Identifier: BSD-2-Clause
 * First-boot CPU registration for Exynos 9820.  Only the firmware boot CPU
 * is described by the generated ADT; PSCI, secondary cores and sleep are not
 * part of this milestone.  No Apple-specific CPU register accesses are used.
 */
#include <pexpert/arm64/board_config.h>
#if defined(B2L)
#include <IOKit/IOCPU.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <arm64/proc_reg.h>
#include <kern/processor.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>

extern "C" void b2l_send_self_ipi(void);
extern "C" uint64_t b2l_irq_count(boolean_t timer);

class B2LCPU : public IOCPU {
	OSDeclareDefaultStructors(B2LCPU);
	const OSSymbol *cpuName = nullptr;
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
	void initCPU(bool boot) APPLE_KEXT_OVERRIDE;
	void quiesceCPU(void) APPLE_KEXT_OVERRIDE;
	kern_return_t startCPU(vm_offset_t start, vm_offset_t arg) APPLE_KEXT_OVERRIDE;
	void haltCPU(void) APPLE_KEXT_OVERRIDE;
	void signalCPU(IOCPU *target) APPLE_KEXT_OVERRIDE;
	const OSSymbol *getCPUName(void) APPLE_KEXT_OVERRIDE;
	static void reportInterrupts(void *arg, wait_result_t result);
};
OSDefineMetaClassAndStructors(B2LCPU, IOCPU);

bool
B2LCPU::start(IOService *provider)
{
	const ml_topology_info_t *topology = ml_get_topology_info();
	OSNumber *logical = OSDynamicCast(OSNumber, provider->getProperty("logical-cpu-id"));
	OSData *reg = OSDynamicCast(OSData, provider->getProperty("reg"));
	if (topology->num_cpus != 1 || topology->boot_cpu != &topology->cpus[0] ||
	    logical == nullptr || logical->unsigned32BitValue() != 0 ||
	    reg == nullptr || reg->getLength() != sizeof(uint32_t) ||
	    *(const uint32_t *)reg->getBytesNoCopy() != topology->boot_cpu->phys_id) {
		return false;
	}
	if (!IOCPU::start(provider)) {
		return false;
	}
	cpuName = OSSymbol::withCStringNoCopy("Exynos 9820 boot CPU");
	if (cpuName == nullptr) {
		return false;
	}
	setCPUNumber(0);
	const ml_topology_cpu_t *cpu = topology->boot_cpu;
	ml_processor_info_t info = {};
	info.cpu_id = reinterpret_cast<cpu_id_t>(this);
	info.phys_id = cpu->phys_id;
	info.log_id = 0;
	info.cluster_id = cpu->cluster_id;
	info.cluster_type = cpu->cluster_type;
	info.l2_cache_id = cpu->l2_cache_id;
	info.l2_cache_size = cpu->l2_cache_size;
	info.l3_cache_id = cpu->l3_cache_id;
	info.l3_cache_size = cpu->l3_cache_size;
	perfmon_interrupt_handler_func pmi = nullptr;
	/* This XNU calls processor_boot() internally.  A second processor_start
	 * call would violate the scheduler's boot state machine. */
	kern_return_t kr = ml_processor_register(&info, &machProcessor, &ipi_handler, &pmi);
	if (kr != KERN_SUCCESS || pmi != nullptr) {
		panic("B2L CPU: registration failed (%d), PMI=%p", kr, pmi);
	}
	ml_set_max_cpus(1);
	ml_cpu_init_completed();
	cpusRunning();
	IOService::publishResource(gIOAllCPUInitializedKey, kOSBooleanTrue);
	registerService();
	IOLog("B2L CPU: 1/1 CPU initialized, MPIDR=0x%x\n", cpu->phys_id);
	thread_t reporter;
	kr = kernel_thread_start(&B2LCPU::reportInterrupts, nullptr, &reporter);
	if (kr != KERN_SUCCESS) {
		panic("B2L CPU: cannot start timer verification thread (%d)", kr);
	}
	thread_deallocate(reporter);
	return true;
}

void
B2LCPU::initCPU(bool boot)
{
	if (!boot || getCPUNumber() != 0 || cpu_number() != 0) {
		panic("B2L CPU: unsupported CPU resume or secondary startup");
	}
	ml_broadcast_cpu_event(CPU_BOOTED, 0);
	/* The IRQ handler routes SGI1 to cpu_signal_handler(), which publishes
	 * SIGPdisabled completion to XNU. */
	b2l_send_self_ipi();
	setCPUState(kIOCPUStateRunning);
	kprintf("B2L CPU: initCPU MPIDR=0x%llx EL%llu IRQs=%u\n",
	    __builtin_arm_rsr64("MPIDR_EL1") & 0xffffULL,
	    __builtin_arm_rsr64("CurrentEL") >> 2, ml_get_interrupts_enabled());
}

void
B2LCPU::reportInterrupts(void *arg __unused, wait_result_t result __unused)
{
	/* A line reached after IOSleep demonstrates scheduler wakeup from real
	 * architected timer interrupts, rather than just programmed registers. */
	for (unsigned i = 0; i < 3; ++i) {
		IOSleep(1000);
		uint64_t timers = b2l_irq_count(TRUE);
		uint64_t signals = b2l_irq_count(FALSE);
		if (timers == 0 || signals == 0) {
			panic("B2L CPU: missing interrupt delivery, timer=%llu SGI1=%llu",
			    timers, signals);
		}
		IOLog("B2L IRQ proof: timer=%llu SGI1=%llu scheduler awake\n",
		    timers, signals);
	}
	thread_terminate(current_thread());
}

void B2LCPU::free(void)
{
	OSSafeReleaseNULL(cpuName);
	IOCPU::free();
}
void B2LCPU::signalCPU(IOCPU *target)
{
	if (target != this) {
		panic("B2L CPU: remote IPI outside single-CPU target");
	}
	b2l_send_self_ipi();
}
kern_return_t B2LCPU::startCPU(vm_offset_t, vm_offset_t) { return KERN_NOT_SUPPORTED; }
void B2LCPU::quiesceCPU(void) { panic("B2L CPU: suspend is not implemented"); }
void B2LCPU::haltCPU(void) { panic("B2L CPU: halt is not implemented"); }
const OSSymbol *B2LCPU::getCPUName(void) { return cpuName; }
#endif
