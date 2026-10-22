/* SPDX-License-Identifier: BSD-2-Clause
 * Exynos 9820 MPIDR0..5 Cortex-A55/A75 bring-up through the stock SMC conduit.
 * M4 MPIDRs0x100/0x101 remain absent. No CPU_OFF, suspend or DVFS policy.
 */
#include <pexpert/arm64/board_config.h>
#if defined(B2L)
#include <IOKit/IOCPU.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <arm64/proc_reg.h>
extern "C" int get_preemption_level(void);
#include <kern/clock.h>
#include <kern/processor.h>
#include <kern/thread.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include "b2l_secondary.h"

extern "C" void b2l_secondary_entry(void);
extern "C" uint64_t b2l_smp_progress(unsigned cpu);
extern "C" void b2l_early_trace(const char *, ...);
extern "C" void b2l_send_self_ipi(void);
extern "C" void b2l_send_ipi(unsigned cpu);
extern "C" bool b2l_cpu_ipi_ready(unsigned cpu);
extern "C" uint64_t b2l_cpu_irq_count(unsigned cpu, boolean_t timer);
extern "C" bool b2l_prepare_cpu_start(unsigned cpu);
extern "C" bool b2l_smp_rejected(void);
extern "C" processor_t b2l_bind_cpu(processor_t processor);

static_assert(MAX_CPUS == 6, "CPU0..5 A55/A75 experiment only");
class B2LCPU;
static B2LCPU *b2l_cpus[MAX_CPUS];
static unsigned providers_ready, registered_cpus;
static uint64_t secondary_pa;
static volatile unsigned long long *secondary_page;
extern "C" vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
static void prepareSecondaryPage(void)
{
    DTEntry chosen; const void *property; unsigned size; uint64_t range[2];
    if (SecureDTLookupEntry(nullptr,"/chosen",&chosen)!=kSuccess ||
        SecureDTGetProperty(chosen,"b2l,secondary-trampoline",&property,&size)!=kSuccess || size!=16)
        panic("B2L SMP: missing firmware-owned low entry");
    memcpy(range,property,sizeof(range));
    if (!b2l_secondary_address(range[0]) || range[1]!=B2L_SECONDARY_BYTES)
        panic("B2L SMP: invalid low secondary allocation");
    secondary_pa=range[0];
    secondary_page=(volatile unsigned long long *)ml_io_map(secondary_pa,B2L_SECONDARY_BYTES);
    if (!secondary_page || !b2l_secondary_template(secondary_page) ||
        secondary_page[B2L_SECONDARY_TARGET_WORD] || secondary_page[B2L_SECONDARY_STAGE_WORD])
        panic("B2L SMP: low entry template is not pristine");
    b2l_early_trace("B2L SMP: low entry prepared pa=0x%llx size=16KiB\n",secondary_pa);
}
static uint32_t xcall_observed[MAX_CPUS];

/* Observe context without changing IRQ state, binding or preemption counts.
 * Never repair an imbalance by forcibly enabling preemption. */
static void startupContext(const char *stage, unsigned target, bool sleepable)
{
    int level = get_preemption_level();
    boolean_t irq = ml_get_interrupts_enabled();
    b2l_early_trace("B2L CTX: %s target=%u current=%u preempt=%d irq=%u thread=%p\n",
        stage, target, (unsigned)cpu_number(), level, (unsigned)irq, current_thread());
    if (sleepable && (level != 0 || !irq))
        panic("B2L SMP: non-sleepable context at %s target=%u current=%u preempt=%d irq=%u",
            stage, target, (unsigned)cpu_number(), level, (unsigned)irq);
}


/* SMCCC v1.0 permits x0..x17 to change. No version/feature probe is needed:
 * the stock DT explicitly supplies this legacy CPU_ON ID and SMC conduit. */
static int32_t b2l_cpu_on(uint64_t target, uint64_t entry, uint64_t context)
{
    register uint64_t x0 __asm__("x0") = 0xc4000003ULL;
    register uint64_t x1 __asm__("x1") = target;
    register uint64_t x2 __asm__("x2") = entry;
    register uint64_t x3 __asm__("x3") = context;
    register uint64_t x4 __asm__("x4") = 0;
    register uint64_t x5 __asm__("x5") = 0;
    register uint64_t x6 __asm__("x6") = 0;
    register uint64_t x7 __asm__("x7") = 0;
    __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
        "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7) : :
        "x8", "x9", "x10", "x11", "x12", "x13",
        "x14", "x15", "x16", "x17", "cc", "memory");
    return (int32_t)x0;
}
static void xcall_proof(void *arg)
{
    /* XNU uses a non-NULL parameter as the xcall publication sentinel. */
    uintptr_t encoded = (uintptr_t)arg;
    if (!encoded || encoded > MAX_CPUS)
        panic("B2L CPU: invalid xcall proof parameter");
    unsigned expected = (unsigned)(encoded - 1);
    if ((unsigned)cpu_number() != expected)
        panic("B2L CPU: xcall ran on unexpected CPU");
    __atomic_store_n(&xcall_observed[expected], 1, __ATOMIC_RELEASE);
}

class B2LCPU : public IOCPU {
    OSDeclareDefaultStructors(B2LCPU);
    const OSSymbol *cpuName = nullptr;
    unsigned cpuInitialized = 0;
public:
    bool start(IOService *) APPLE_KEXT_OVERRIDE;
    void registerProcessor(void);
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
        top->num_cpus != 6 || top->boot_cpu != &top->cpus[0] ||
        top->cpus[0].phys_id != 0 ||
        logical->unsigned32BitValue() >= 6) return false;
    unsigned id = logical->unsigned32BitValue();
    const ml_topology_cpu_t *cpu = &top->cpus[id];
    if (cpu->phys_id != id || cpu->cluster_id != 0 || *(const uint32_t *)reg->getBytesNoCopy() != cpu->phys_id)
        return false;
    if (!IOCPU::start(provider)) return false;
    cpuName = OSSymbol::withCStringNoCopy(id < 4 ? "Exynos 9820 Cortex-A55" : "Exynos 9820 Cortex-A75");
    if (!cpuName) return false;
    setCPUNumber(id);
    /* IOService matching is asynchronous. Collect all providers before a
     * single coordinator registers CPU0 first, then the secondaries. */
    B2LCPU *expected = nullptr;
    if (!__atomic_compare_exchange_n(&b2l_cpus[id], &expected, this, false,
            __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        panic("B2L SMP: duplicate CPU provider %u", id);
    unsigned count = __atomic_add_fetch(&providers_ready, 1, __ATOMIC_ACQ_REL);
    b2l_early_trace("B2L SMP: provider CPU%u ready (%u/6)\n", id, count);
    if (count == 6) {
        thread_t coordinator;
        kern_return_t kr = kernel_thread_start(&B2LCPU::finishStartup, nullptr, &coordinator);
        if (kr != KERN_SUCCESS) panic("B2L CPU: coordinator failed (%d)", kr);
        thread_deallocate(coordinator);
    }
    return true;
}

void B2LCPU::registerProcessor(void)
{
    unsigned id = getCPUNumber();
    const ml_topology_cpu_t *cpu = &ml_get_topology_info()->cpus[id];
    startupContext("register-enter", id, true);
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
    b2l_early_trace("B2L SMP: register CPU%u begin\n", id);
    kern_return_t kr = ml_processor_register(&info, &machProcessor, &ipi_handler, &pmi);
    startupContext("native-register-return", id, true);
    if (kr != KERN_SUCCESS || pmi)
        panic("B2L CPU: registration failed (%d), PMI=%p", kr, pmi);
    b2l_early_trace("B2L SMP: register CPU%u returned\n", id);
    __atomic_add_fetch(&registered_cpus, 1, __ATOMIC_ACQ_REL);
    startupContext("publish-begin", id, true);
    registerService();
    startupContext("publish-return", id, true);
}

void B2LCPU::initCPU(bool boot)
{
    unsigned id = getCPUNumber();
    if (!boot || id >= 6 || (unsigned)cpu_number() != id)
        panic("B2L CPU: unsupported resume or wrong startup CPU");
    startupContext("initCPU-enter", id, false);
    ml_broadcast_cpu_event(CPU_BOOTED, id);
    /* Secondary init runs at splsched with IRQs masked. Queue self-SGI
     * and return; waiting here would deadlock before secondary_cpu_main. */
    b2l_send_self_ipi();
    setCPUState(kIOCPUStateRunning);
    __atomic_store_n(&cpuInitialized, 1, __ATOMIC_RELEASE);
    startupContext("initCPU-return", id, false);
    b2l_early_trace("B2L CPU: cpu=%u entered initCPU, MPIDR=0x%llx\n", id,
        __builtin_arm_rsr64("MPIDR_EL1") & 0xffffULL);
}

kern_return_t B2LCPU::startCPU(vm_offset_t start, vm_offset_t arg)
{
    if (start || arg || getCPUNumber() == 0 || getCPUNumber() >= 6 ||
        __atomic_load_n(&cpuInitialized, __ATOMIC_ACQUIRE))
        panic("B2L CPU: unsupported CPU_ON request");
    vm_offset_t entry = ml_static_vtop((vm_offset_t)&b2l_secondary_entry);
    if (!entry || (entry & 3)) panic("B2L CPU: invalid secondary entry");
    /* Secondaries start with caches off. Publish every reset input explicitly to
     * PoC rather than relying on a coherent-IO flush wrapper being active. */
    unsigned target = getCPUNumber();
    if (!b2l_prepare_cpu_start(target))
        panic("B2L CPU: secondary reset data incomplete");
    if (!secondary_page || !b2l_secondary_template(secondary_page) ||
        (secondary_page[B2L_SECONDARY_TARGET_WORD] && secondary_page[B2L_SECONDARY_TARGET_WORD]!=entry))
        panic("B2L SMP: secondary trampoline changed");
    secondary_page[B2L_SECONDARY_TARGET_WORD]=entry;
    secondary_page[B2L_SECONDARY_STAGE_WORD]=0;
    __asm__ volatile("dsb sy":::"memory");
    if (secondary_page[B2L_SECONDARY_TARGET_WORD]!=entry)
        panic("B2L SMP: low entry target readback failed");
    b2l_early_trace("B2L SMP: CPU_ON begin target=%u entry=0x%llx destination=0x%llx\n",
        target,secondary_pa,(uint64_t)entry);
    startupContext("CPU_ON-enter", target, false);
    int32_t result = b2l_cpu_on(target, secondary_pa, 0);
    startupContext("CPU_ON-return", target, false);
    b2l_early_trace("B2L SMP: CPU_ON result=%d low-stage=%llu secondary-stage=%llu\n",
        result,secondary_page[B2L_SECONDARY_STAGE_WORD],b2l_smp_progress(target));
    /* XNU discards the startCPU return. Fail explicitly on real EL3 errors. */
    if (result != 0) panic("B2L CPU: CPU%u CPU_ON failed (%d)", target, result);
    return KERN_SUCCESS;
}

void B2LCPU::signalCPU(IOCPU *target)
{
    if (!target || target->getCPUNumber() >= 6)
        panic("B2L CPU: invalid IPI target");
    b2l_send_ipi(target->getCPUNumber());
}

void B2LCPU::finishStartup(void *, wait_result_t)
{
    if (cpu_number() != 0 || __atomic_load_n(&providers_ready, __ATOMIC_ACQUIRE) != 6)
        panic("B2L SMP: coordinator before complete CPU provider topology");
    /* Announce known topology, without claiming any secondary is online. */
    ml_set_max_cpus(6);
    prepareSecondaryPage();
    for (unsigned id = 0; id < 6; ++id) {
        B2LCPU *c = __atomic_load_n(&b2l_cpus[id], __ATOMIC_ACQUIRE);
        if (!c) panic("B2L SMP: missing provider %u", id);
        c->registerProcessor();
    }
    uint64_t deadline, last_stage[6];
    for (unsigned i = 0; i < 6; ++i) last_stage[i] = UINT64_MAX;
    nanoseconds_to_deadline(20 * NSEC_PER_SEC, &deadline);
    for (;;) {
        if (b2l_smp_rejected()) panic("B2L SMP: unexpected secondary affinity rejected before CPU-data lookup");
        for (unsigned i = 1; i < 6; ++i) {
            uint64_t stage = b2l_smp_progress(i);
            if (stage != last_stage[i]) {
                b2l_early_trace("B2L SMP: CPU%u secondary-stage=%llu\n", i, stage);
                last_stage[i] = stage;
            }
        }
        bool ready = __atomic_load_n(&registered_cpus, __ATOMIC_ACQUIRE) == 6;
        for (unsigned i = 0; ready && i < 6; ++i) {
            B2LCPU *c = __atomic_load_n(&b2l_cpus[i], __ATOMIC_ACQUIRE);
            ready = c && __atomic_load_n(&c->cpuInitialized, __ATOMIC_ACQUIRE) &&
                b2l_cpu_ipi_ready(i);
        }
        if (ready) break;
        if (mach_absolute_time() > deadline)
            panic("B2L CPU: CPU registration/reset/MMU/timer/SGI startup timed out");
        IOSleep(1);
    }
    /* Real timers on every CPU, plus CPU0 -> each secondary and each -> CPU0.
     * Only one xcall is outstanding; reset its acknowledgement before send. */
    for (unsigned sender = 0; sender < 6; ++sender) {
        processor_t previous = b2l_bind_cpu(b2l_cpus[sender]->machProcessor);
        thread_block(THREAD_CONTINUE_NULL);
        if ((unsigned)cpu_number() != sender) panic("B2L CPU: IPI proof thread not bound");
        IOSleep(20);
        nanoseconds_to_deadline(NSEC_PER_SEC, &deadline);
        while (!b2l_cpu_irq_count(sender, TRUE)) {
            if (mach_absolute_time() > deadline) panic("B2L CPU: per-CPU timer proof timeout");
            IOSleep(1);
        }
        unsigned first = sender ? 0 : 1, end = sender ? 1 : 6;
        for (unsigned target = first; target < end; ++target) {
            __atomic_store_n(&xcall_observed[target], 0, __ATOMIC_RELEASE);
            kern_return_t kr = cpu_xcall((int)target, (broadcastFunc)xcall_proof,
                (void *)(uintptr_t)(target + 1));
            if (kr != KERN_SUCCESS) panic("B2L CPU: remote xcall rejected (%d)", kr);
            nanoseconds_to_deadline(NSEC_PER_SEC, &deadline);
            while (!__atomic_load_n(&xcall_observed[target], __ATOMIC_ACQUIRE)) {
                if (mach_absolute_time() > deadline) panic("B2L CPU: remote xcall timeout");
                IOSleep(1);
            }
        }
        b2l_early_trace("B2L SMP: CPU%u timer and remote xcall verified\n", sender);
        b2l_bind_cpu(previous);
    }
    ml_cpu_init_completed();
    b2l_cpus[0]->cpusRunning();
    IOService::publishResource(gIOAllCPUInitializedKey, kOSBooleanTrue);
    b2l_early_trace("B2L CPU: 6/6 CPUs initialized, per-CPU timer and bidirectional SGI1 verified\n");
    IOLog("B2L CPU: 6/6 CPUs initialized, per-CPU timer and bidirectional SGI1 verified\n");
    thread_terminate(current_thread());
}

void B2LCPU::free(void) { OSSafeReleaseNULL(cpuName); IOCPU::free(); }
void B2LCPU::quiesceCPU(void) { panic("B2L CPU: suspend is not implemented"); }
void B2LCPU::haltCPU(void) { panic("B2L CPU: halt is not implemented"); }
const OSSymbol *B2LCPU::getCPUName(void) { return cpuName; }
#endif
