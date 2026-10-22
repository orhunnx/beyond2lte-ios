/* SPDX-License-Identifier: BSD-2-Clause
 * Exynos 9820 same-cluster CPU0/CPU1 bring-up through the stock SMC conduit.
 * No CPU_OFF, suspend, EL2 setup or heterogeneous-cluster policy is claimed.
 */
#include <pexpert/arm64/board_config.h>
#if defined(B2L)
#include <IOKit/IOCPU.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <arm64/proc_reg.h>
#include <kern/clock.h>
#include <kern/processor.h>
#include <kern/thread.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>

extern "C" void b2l_secondary_entry(void);
extern "C" void b2l_send_self_ipi(void);
extern "C" void b2l_send_ipi(unsigned cpu);
extern "C" bool b2l_cpu_ipi_ready(unsigned cpu);
extern "C" uint64_t b2l_cpu_irq_count(unsigned cpu, boolean_t timer);
extern "C" bool b2l_prepare_cpu_start(unsigned cpu);
extern "C" processor_t b2l_bind_cpu(processor_t processor);

static_assert(MAX_CPUS == 2, "CPU0/CPU1 experiment only");
class B2LCPU;
static B2LCPU *b2l_cpus[MAX_CPUS];
static unsigned registered_cpus;
static uint32_t xcall_observed[MAX_CPUS];

/* SMCCC v1.0 permits x0..x17 to change. No version/feature probe is needed:
 * the stock DT explicitly supplies this legacy CPU_ON ID and SMC conduit. */
static int32_t b2l_cpu_on(uint64_t target, uint64_t entry, uint64_t context)
{
    register uint64_t x0 __asm__("x0") = 0xc4000003ULL;
    register uint64_t x1 __asm__("x1") = target;
    register uint64_t x2 __asm__("x2") = entry;
    register uint64_t x3 __asm__("x3") = context;
    __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : :
        "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13",
        "x14", "x15", "x16", "x17", "cc", "memory");
    return (int32_t)x0;
}
static void xcall_proof(void *arg)
{
    unsigned expected = (unsigned)(uintptr_t)arg;
    if ((unsigned)cpu_number() != expected || expected >= MAX_CPUS)
        panic("B2L CPU: xcall ran on unexpected CPU");
    __atomic_store_n(&xcall_observed[expected], 1, __ATOMIC_RELEASE);
}

class B2LCPU : public IOCPU {
    OSDeclareDefaultStructors(B2LCPU);
    const OSSymbol *cpuName = nullptr;
    unsigned cpuInitialized = 0;
public:
    bool start(IOService *) APPLE_KEXT_OVERRIDE;
    void free(void) APPLE_KEXT_OVERRIDE;
    void initCPU(bool) APPLE_KEXT_OVERRIDE;
    kern_return_t startCPU(vm_offset_t, vm_offset_t) APPLE_KEXT_OVERRIDE;
    void signalCPU(IOCPU *) APPLE_KEXT_OVERRIDE;
    void quiesceCPU(void) APPLE_KEXT_OVERRIDE;
    void haltCPU(void) APPLE_KEXT_OVERRIDE;
    const OSSymbol *getCPUName(void) APPLE_KEXT_OVERRIDE;
    static void finishStartup(void *, wait_result_t);
};
OSDefineMetaClassAndStructors(B2LCPU, IOCPU);

bool B2LCPU::start(IOService *provider)
{
    const ml_topology_info_t *top = ml_get_topology_info();
    OSNumber *logical = OSDynamicCast(OSNumber, provider->getProperty("logical-cpu-id"));
    OSData *reg = OSDynamicCast(OSData, provider->getProperty("reg"));
    if (!logical || !reg || reg->getLength() != sizeof(uint32_t) ||
        top->num_cpus != 2 || top->boot_cpu != &top->cpus[0] ||
        top->cpus[0].phys_id != 0 || top->cpus[1].phys_id != 1 ||
        logical->unsigned32BitValue() >= 2) return false;
    unsigned id = logical->unsigned32BitValue();
    const ml_topology_cpu_t *cpu = &top->cpus[id];
    if (cpu->cluster_id != 0 || *(const uint32_t *)reg->getBytesNoCopy() != cpu->phys_id)
        return false;
    if (!IOCPU::start(provider)) return false;
    cpuName = OSSymbol::withCStringNoCopy("Exynos 9820 Cortex-A55");
    if (!cpuName) return false;
    setCPUNumber(id);
    ml_processor_info_t info = {};
    info.cpu_id = reinterpret_cast<cpu_id_t>(this);
    info.phys_id = cpu->phys_id;
    info.log_id = id;
    info.cluster_id = cpu->cluster_id;
    info.cluster_type = cpu->cluster_type;
    info.l2_cache_id = cpu->l2_cache_id;
    info.l2_cache_size = cpu->l2_cache_size;
    info.l3_cache_id = cpu->l3_cache_id;
    info.l3_cache_size = cpu->l3_cache_size;
    perfmon_interrupt_handler_func pmi = nullptr;
    /* XNU 12377 invokes processor_boot inside registration. Never call
     * processor_start again: that would violate its boot state machine. */
    kern_return_t kr = ml_processor_register(&info, &machProcessor, &ipi_handler, &pmi);
    if (kr != KERN_SUCCESS || pmi)
        panic("B2L CPU: registration failed (%d), PMI=%p", kr, pmi);
    __atomic_store_n(&b2l_cpus[id], this, __ATOMIC_RELEASE);
    __atomic_add_fetch(&registered_cpus, 1, __ATOMIC_ACQ_REL);
    registerService();
    if (id == 0) {
        thread_t reporter;
        kr = kernel_thread_start(&B2LCPU::finishStartup, nullptr, &reporter);
        if (kr != KERN_SUCCESS) panic("B2L CPU: startup observer failed (%d)", kr);
        thread_deallocate(reporter);
    }
    return true;
}

void B2LCPU::initCPU(bool boot)
{
    unsigned id = getCPUNumber();
    if (!boot || id >= 2 || (unsigned)cpu_number() != id)
        panic("B2L CPU: unsupported resume or wrong startup CPU");
    ml_broadcast_cpu_event(CPU_BOOTED, id);
    /* Secondary init runs at splsched with IRQs masked. Queue self-SGI
     * and return; waiting here would deadlock before secondary_cpu_main. */
    b2l_send_self_ipi();
    setCPUState(kIOCPUStateRunning);
    __atomic_store_n(&cpuInitialized, 1, __ATOMIC_RELEASE);
    kprintf("B2L CPU: cpu=%u entered initCPU, MPIDR=0x%llx\n", id,
        __builtin_arm_rsr64("MPIDR_EL1") & 0xffffULL);
}

kern_return_t B2LCPU::startCPU(vm_offset_t start, vm_offset_t arg)
{
    if (start || arg || getCPUNumber() != 1 ||
        __atomic_load_n(&cpuInitialized, __ATOMIC_ACQUIRE))
        panic("B2L CPU: unsupported CPU_ON request");
    vm_offset_t entry = ml_static_vtop((vm_offset_t)&b2l_secondary_entry);
    if (!entry || (entry & 3)) panic("B2L CPU: invalid secondary entry");
    /* CPU1 starts with caches off. Publish every reset input explicitly to
     * PoC rather than relying on a coherent-IO flush wrapper being active. */
    if (!b2l_prepare_cpu_start(1))
        panic("B2L CPU: secondary reset data incomplete");
    IOLog("B2L CPU: CPU1 SMC CPU_ON entry=0x%llx MPIDR=1\n", (uint64_t)entry);
    int32_t result = b2l_cpu_on(1, entry, 1);
    IOLog("B2L CPU: CPU1 SMC CPU_ON returned %d\n", result);
    /* XNU discards the startCPU return. Fail explicitly on real EL3 errors. */
    if (result != 0) panic("B2L CPU: CPU1 CPU_ON failed (%d)", result);
    return KERN_SUCCESS;
}

void B2LCPU::signalCPU(IOCPU *target)
{
    if (!target || target->getCPUNumber() >= 2)
        panic("B2L CPU: invalid IPI target");
    b2l_send_ipi(target->getCPUNumber());
}

void B2LCPU::finishStartup(void *, wait_result_t)
{
    uint64_t deadline;
    nanoseconds_to_deadline(10 * NSEC_PER_SEC, &deadline);
    for (;;) {
        bool ready = __atomic_load_n(&registered_cpus, __ATOMIC_ACQUIRE) == 2;
        for (unsigned i = 0; ready && i < 2; ++i) {
            B2LCPU *c = __atomic_load_n(&b2l_cpus[i], __ATOMIC_ACQUIRE);
            ready = c && __atomic_load_n(&c->cpuInitialized, __ATOMIC_ACQUIRE) &&
                b2l_cpu_ipi_ready(i);
        }
        if (ready) break;
        if (mach_absolute_time() > deadline)
            panic("B2L CPU: CPU registration/reset/MMU/timer/SGI startup timed out");
        IOSleep(1);
    }
    IOLog("B2L CPU: both CPUs registered; beginning timer and remote IPI proof\n");
    /* Verify real remote XNU xcalls in both directions. Binding and blocking
     * occur only in this normal kernel thread, never in an interrupt. */
    for (unsigned sender = 0; sender < 2; ++sender) {
        processor_t previous = b2l_bind_cpu(b2l_cpus[sender]->machProcessor);
        thread_block(THREAD_CONTINUE_NULL);
        if ((unsigned)cpu_number() != sender) panic("B2L CPU: IPI proof thread not bound");
        /* An otherwise idle secondary may legitimately have no timer IRQ
         * before work arrives. Create a real timed wait on each CPU first. */
        IOSleep(20);
        nanoseconds_to_deadline(NSEC_PER_SEC, &deadline);
        while (!b2l_cpu_irq_count(sender, TRUE)) {
            if (mach_absolute_time() > deadline) panic("B2L CPU: per-CPU timer proof timeout");
            IOSleep(1);
        }
        unsigned target = sender ^ 1;
        kern_return_t kr = cpu_xcall((int)target, (broadcastFunc)xcall_proof,
            (void *)(uintptr_t)target);
        if (kr != KERN_SUCCESS) panic("B2L CPU: remote xcall rejected (%d)", kr);
        nanoseconds_to_deadline(NSEC_PER_SEC, &deadline);
        while (!__atomic_load_n(&xcall_observed[target], __ATOMIC_ACQUIRE)) {
            if (mach_absolute_time() > deadline) panic("B2L CPU: remote xcall timeout");
            IOSleep(1);
        }
        IOLog("B2L CPU: CPU%u timer=%llu SGI1=%llu; xcall CPU%u -> CPU%u verified\n",
            sender, b2l_cpu_irq_count(sender, TRUE), b2l_cpu_irq_count(sender, FALSE),
            sender, target);
        b2l_bind_cpu(previous);
    }
    ml_set_max_cpus(2);
    ml_cpu_init_completed();
    b2l_cpus[0]->cpusRunning();
    IOService::publishResource(gIOAllCPUInitializedKey, kOSBooleanTrue);
    IOLog("B2L CPU: 2/2 CPUs initialized, per-CPU timer and bidirectional SGI1 verified\n");
    thread_terminate(current_thread());
}

void B2LCPU::free(void) { OSSafeReleaseNULL(cpuName); IOCPU::free(); }
void B2LCPU::quiesceCPU(void) { panic("B2L CPU: suspend is not implemented"); }
void B2LCPU::haltCPU(void) { panic("B2L CPU: halt is not implemented"); }
const OSSymbol *B2LCPU::getCPUName(void) { return cpuName; }
#endif
