/* SPDX-License-Identifier: BSD-2-Clause
 * Opt-in, stripped-pointer ARM64e development ABI. This does not authenticate
 * pointers, claim hardware PAC, or change kernel pointer/security contracts.
 */
#ifndef B2L_COMPAT_HOST_TEST
#include <arm64/b2l_arm64e_compat.h>
#include <arm64/b2l_early.h>
#include <arm64/proc_reg.h>
#include <kern/locks.h>
#include <kern/startup.h>
#include <kern/misc_protos.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <mach/mach_time.h>
#include <string.h>
#include <mach/machine.h>
#include <mach/arm/thread_status.h>
#include <mach/arm/vm_param.h>
#include <mach/vm_region.h>
#include <pexpert/pexpert.h>
/* Source-public sys/random.h contract, without mixing BSD and Mach types. */
extern void read_frandom(void *, unsigned int);
#include <vm/vm_map_internal.h>
#include <vm/vm_map_xnu.h>
#include <vm/vm_map_lock_internal.h>
#include <vm/vm_object_internal.h>
#include <vm/memory_object_internal.h>
#include <vm/vm_protos_internal.h>
#include <sys/errno.h>
extern void b2l_native_cache_observe(uint64_t,uint32_t);
#endif
#include "b2l_compat.h"
#include "b2l_pacga_qarma5.h"
extern uint64_t b2l_pacga_qarma5_asm(uint64_t, uint64_t, uint64_t, uint64_t);
extern uint64_t b2l_pacga_siphash(uint64_t, uint64_t, const uint64_t[2]);

static bool b2l_compat_requested;
/* Written only at TUNABLES, before any userspace object can be signed. */
static bool b2l_compat_siphash;
static bool b2l_compat_pair;
/* Samples cover successful wrapper work only, not exception entry/return. */
static uint64_t b2l_compat_perf[6];
struct b2l_compat_measurement { bool sample; uint64_t vm_ticks, hash_ticks; };
static uint64_t b2l_compat_telemetry[4];
#ifndef B2L_COMPAT_HOST_TEST
extern int proc_selfpid(void);
#endif
void b2l_arm64e_compat_snapshot(uint64_t values[4])
{
    for(unsigned i=0;i<4;i++)values[i]=__atomic_load_n(&b2l_compat_telemetry[i],__ATOMIC_RELAXED);
}

void
b2l_arm64e_perf_snapshot(uint64_t values[6])
{
    for (unsigned i=0;i<6;i++) values[i]=__atomic_load_n(&b2l_compat_perf[i],__ATOMIC_RELAXED);
#ifndef B2L_COMPAT_HOST_TEST
    for (unsigned i=1;i<=3;i++) {
        uint64_t ns=0;absolutetime_to_nanoseconds(values[i],&ns);values[i]=ns;
    }
#endif
}

static void
b2l_compat_init(void)
{
    int requested = 0;
    b2l_compat_requested =
        PE_parse_boot_argn("b2l_arm64e_compat", &requested, sizeof(requested)) && requested == 1;
    int siphash = 0;
    b2l_compat_siphash = b2l_compat_requested &&
        PE_parse_boot_argn("b2l_pacga_siphash", &siphash, sizeof(siphash)) && siphash == 1;
    int pair = 0;
    b2l_compat_pair = b2l_compat_requested &&
        PE_parse_boot_argn("b2l_pacga_pair", &pair, sizeof(pair)) && pair == 1;
    if (b2l_compat_requested) {
        /* TUNABLES runs before log_lock initialization. Use only the
         * already mapped, lock-free early framebuffer sink here. */
        const char *message = "B2L ARM64e: development stripped-pointer runtime enabled; hardware PAC unchanged\n";
        while (*message) b2l_early_putc(*message++);
        message = b2l_compat_siphash ?
            "B2L PACGA: SipHash-2-4 experimental boot ABI; immutable boot key\n" :
            "B2L PACGA: QARMA5; immutable boot key\n";
        while (*message) b2l_early_putc(*message++);
        if (b2l_compat_pair) {
            message="B2L PACGA: bounded adjacent pair enabled; execute checks retained\n";
            while (*message) b2l_early_putc(*message++);
        }
    }
}
STARTUP(TUNABLES, STARTUP_RANK_MIDDLE, b2l_compat_init);

bool
b2l_arm64e_compat_enabled(void)
{
    return b2l_compat_requested;
}

const char *
b2l_arm64e_pacga_name(void)
{
    if (!b2l_compat_requested) return "disabled";
    return b2l_compat_siphash ? "siphash24" : "qarma5";
}

LCK_GRP_DECLARE(b2l_compat_lck_grp, "b2l_arm64e_compat");
LCK_MTX_DECLARE(b2l_compat_key_lock, &b2l_compat_lck_grp);
static uint64_t b2l_compat_key[2];
static bool b2l_compat_key_ready;

/* No top-byte/PAC guessing. Every access stays in the current EL0 address
 * space and remains faultable through copyin. End is exclusive. */
static bool
b2l_user_range(uint64_t address, unsigned width)
{
    return width && address && address < MACH_VM_MAX_ADDRESS &&
           width <= MACH_VM_MAX_ADDRESS - address;
}

static int
b2l_compat_read(uint64_t address, unsigned width, uint64_t *out, void *opaque __unused)
{
    uint64_t value = 0;
    if (!out || (width != 1 && width != 2 && width != 4 && width != 8) ||
        !b2l_user_range(address, width) || copyin(address, &value, width)) return -1;
    *out = value; /* Both kernel and target are little endian. */
    return 0;
}

static int
b2l_compat_read_acquire(uint64_t address, unsigned width, uint64_t *out, void *opaque)
{
    /* Fault-recoverable scalar atomics, followed by a stronger acquire
     * barrier. Byte/halfword use their containing aligned user word; it cannot
     * cross a page and remains inside the same process mapping. */
    if (!out || (width != 1 && width != 2 && width != 4 && width != 8) ||
        !b2l_user_range(address, width) || (address & (width - 1u))) return -1;
    int error;
    if (width == 8) {
        error = copyin_atomic64(address, out);
    } else {
        uint32_t word = 0;
        error = copyin_atomic32(address & ~UINT64_C(3), &word);
        if (!error) *out = (word >> ((address & 3u) * 8u)) &
                          (width == 4 ? UINT32_MAX : ((1u << (width * 8u)) - 1u));
    }
    if (!error) {
#ifndef B2L_COMPAT_HOST_TEST
        __asm__ volatile("dmb ish" ::: "memory");
#else
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
#endif
    }
    return error;
}

static int
b2l_compat_validate_pc(uint64_t address, void *opaque)
{
    uint32_t instruction;
    static unsigned check_samples;
    bool trace = check_samples < 2;
    if (trace) { ++check_samples; b2l_early_trace("B2L COMPAT: execute-map check begin pc=0x%llx\n", address); }
    if ((address & 3u) || !b2l_user_range(address, sizeof(instruction))) return -1;
    /* Inspect only the exact instruction range under shared entry locks.
     * Constant/transparent submaps must resolve to the executable leaf, with
     * bounds translated/clamped back into the caller's parent address space.
     * The region-info API also gathers object/share/refcount metadata, taking
     * exclusive VM object locks even for SHORT_INFO. None of that metadata is
     * needed for this permission check. Never cache an unlocked permission. */
    struct b2l_compat_measurement *measurement=opaque;
    uint64_t began=measurement && measurement->sample ? mach_absolute_time() : 0;
    VM_MAP_LOCK_CTX_DECLARE(ctx);
    vm_map_t map=current_map();
    kern_return_t kr=vm_map_range_sh_lock(ctx,&map,address,address+sizeof(instruction),
        VMRL_SH_STREAM_NO_HOLES | VMRL_SH_DESCEND_INTO_CONSTANT);
    bool executable=false;
    if (kr==KERN_SUCCESS) {
        vm_map_entry_t entry=vm_map_range_stream_next_with_error(ctx,&kr);
        if (kr==KERN_SUCCESS && entry && !entry->is_sub_map &&
            (entry->protection & VM_PROT_EXECUTE)) {
            vm_map_address_t start=0,end=0;
            vm_map_lock_ctx_bounds_in_parent(ctx,&start,&end,NULL);
            executable=start<=address && end>=address+sizeof(instruction);
        }
        vm_map_range_sh_unlock(ctx,&map);
    }
    if (measurement && measurement->sample) measurement->vm_ticks += mach_absolute_time()-began;
    if (!executable) return -1;
    /* Release all map locks BEFORE faultable instruction copyin. */
    if (trace) b2l_early_trace("B2L COMPAT: execute-map check passed; copyin begin\n");
    int error = copyin_atomic32(address, &instruction);
    if (trace) b2l_early_trace("B2L COMPAT: execute-PC copyin result=%d\n", error);
    return error ? -1 : 0;
}

static int
b2l_compat_pacga(uint64_t a, uint64_t b, uint64_t *out, void *opaque)
{
    if (!out || !b2l_compat_requested) return -1;
    struct b2l_compat_measurement *measurement=opaque;
    uint64_t began=measurement && measurement->sample ? mach_absolute_time() : 0;
    /* First invocation is an EL0 trap after BSD startup and PRNG registration.
     * One key and one algorithm cover every task and CPU for the entire boot.
     * Release publishes the completed key; acquire readers never need a mutex.
     * The key is never replaced, persisted or printed. */
    if (!__atomic_load_n(&b2l_compat_key_ready, __ATOMIC_ACQUIRE)) {
        lck_mtx_lock(&b2l_compat_key_lock);
        if (!__atomic_load_n(&b2l_compat_key_ready, __ATOMIC_RELAXED)) {
            read_frandom(b2l_compat_key, sizeof(b2l_compat_key));
            __atomic_store_n(&b2l_compat_key_ready, true, __ATOMIC_RELEASE);
        }
        lck_mtx_unlock(&b2l_compat_key_lock);
    }
    if (b2l_compat_siphash) {
        /* BSD owns the SipHash context; do not mix its types into Mach VM.
         * This experimental result ABI does not reproduce QARMA outputs. */
        *out = b2l_pacga_siphash(a, b, b2l_compat_key);
    } else {
        *out = b2l_pacga_qarma5_asm(a, b,
            b2l_compat_key[0], b2l_compat_key[1]);
    }
    if (measurement && measurement->sample) measurement->hash_ticks += mach_absolute_time()-began;
    return 0;
}

static bool
b2l_compat_is_pacga(uint32_t instruction)
{
    return (instruction & UINT32_C(0xffe0fc00)) == UINT32_C(0x9ac03000);
}

bool
b2l_arm64e_compat_emulate(arm_saved_state_t *state, uint32_t instruction)
{
    static unsigned wrapper_samples;
    bool trace = wrapper_samples < 2;
    if (trace) { ++wrapper_samples; b2l_early_trace("B2L COMPAT: wrapper entered insn=0x%x\n", instruction); }
    if (!b2l_compat_requested || !state || !is_saved_state64(state) ||
        !PSR64_IS_USER64(get_saved_state_cpsr(state)) ||
        !b2l_arm64e_current_task()) return false;
    if (trace) b2l_early_trace("B2L COMPAT: process and EL0 gates passed\n");
    uint64_t ticket=__atomic_fetch_add(&b2l_compat_telemetry[0],1,__ATOMIC_RELAXED);
    struct b2l_compat_measurement measurement={.sample=(ticket & 1023u)==0};
    uint64_t began=measurement.sample ? mach_absolute_time() : 0;
#ifndef B2L_COMPAT_HOST_TEST
    __atomic_store_n(&b2l_compat_telemetry[2],(uint64_t)proc_selfpid(),__ATOMIC_RELAXED);
#endif
    __atomic_store_n(&b2l_compat_telemetry[3],get_saved_state_pc(state),__ATOMIC_RELAXED);
    struct b2l_compat_ctx context = {0};
    for (unsigned i = 0; i < 29; ++i) context.x[i] = get_saved_state_reg(state, i);
    context.x[29] = get_saved_state_fp(state);
    context.x[30] = get_saved_state_lr(state);
    context.sp = get_saved_state_sp(state);
    context.pc = get_saved_state_pc(state);
    context.cpsr = get_saved_state_cpsr(state);
    if (b2l_compat_validate_pc(context.pc, &measurement)) return false;
    const struct b2l_compat_ops operations = {
        .mode = B2L_COMPAT_MODE_STRIPPED,
        .read = b2l_compat_read, .read_acquire = b2l_compat_read_acquire,
        .validate_pc = b2l_compat_validate_pc, .pacga = b2l_compat_pacga,
        .opaque = &measurement
    };
    if (trace) b2l_early_trace("B2L COMPAT: decoder begin\n");
    enum b2l_compat_result result = b2l_compat_emulate(instruction, &context, &operations);
    if (trace) b2l_early_trace("B2L COMPAT: decoder result=%u\n", result);
    if (result != B2L_COMPAT_OK) {
        b2l_early_trace("B2L COMPAT: rejected result=%u pc=0x%llx x8=0x%llx x16=0x%llx lr=0x%llx\n",
            result, context.pc, context.x[8], context.x[16], context.x[30]);
        return false;
    }
    if (b2l_compat_pair && !(context.cpsr & PSR64_SS) &&
        current_thread()->machine.DebugData == NULL && b2l_compat_is_pacga(instruction)) {
        /* The first decoder has already validated its next PC as executable.
         * Fetch only one following instruction through recoverable EL0 copyin.
         * Reuse the actual decoder for all operand/ZR/SP and next-PC checks.
         * Failure to consume the second instruction commits ONLY the first;
         * the next EL0 instruction then executes/traps normally. No loop,
         * branch, data operation or speculative cache entry is consumed.
         * Debug contexts and software single-step retain one-instruction traps. */
        uint32_t following=0;
        if (copyin_atomic32(context.pc,&following)) {
            __atomic_fetch_add(&b2l_compat_perf[5],1,__ATOMIC_RELAXED);
        } else if (b2l_compat_is_pacga(following)) {
            struct b2l_compat_ctx candidate=context;
            if (b2l_compat_emulate(following,&candidate,&operations)==B2L_COMPAT_OK) {
                context=candidate;
                __atomic_fetch_add(&b2l_compat_perf[4],1,__ATOMIC_RELAXED);
            } else {
                __atomic_fetch_add(&b2l_compat_perf[5],1,__ATOMIC_RELAXED);
            }
        }
    }
#ifndef B2L_COMPAT_HOST_TEST
    /* Enqueue only after successful emulation, using the original PC. */
    b2l_native_cache_observe(get_saved_state_pc(state),instruction);
#endif
    __atomic_fetch_add(&b2l_compat_telemetry[1],1,__ATOMIC_RELAXED);
    for (unsigned i = 0; i < 29; ++i) set_user_saved_state_reg(state, i, context.x[i]);
    set_saved_state_fp(state, context.x[29]);
    set_user_saved_state_lr(state, context.x[30]);
    set_saved_state_sp(state, context.sp);
    set_user_saved_state_pc(state, context.pc);
    if (measurement.sample) {
        __atomic_fetch_add(&b2l_compat_perf[0],1,__ATOMIC_RELAXED);
        __atomic_fetch_add(&b2l_compat_perf[1],mach_absolute_time()-began,__ATOMIC_RELAXED);
        __atomic_fetch_add(&b2l_compat_perf[2],measurement.vm_ticks,__ATOMIC_RELAXED);
        __atomic_fetch_add(&b2l_compat_perf[3],measurement.hash_ticks,__ATOMIC_RELAXED);
    }
    /* Decoder preserves CPSR; never let emulation modify mode/masks/NZCV. */
    return true;
}

#ifndef B2L_COMPAT_HOST_TEST

#if defined(B2L)
/* Read-only address->file resolution for the background cache learner.
 * Parent-address translation handles transparent and constant submaps; shadow
 * and backing-pager offsets are accumulated exactly as code-signing mapping.
 * The caller owns a map reference. No VFS I/O occurs while a VM lock is held.
 */
extern int vnode_getwithvid(struct vnode *,uint32_t);
extern uint32_t vnode_vid(struct vnode *);
int
b2l_cache_resolve(vm_map_t source,uint64_t address,struct vnode **out,uint64_t *file_offset)
{
 VM_MAP_LOCK_CTX_DECLARE(ctx);
 vm_map_t map=source;vm_map_entry_t entry;vm_object_t object,next;
 uint64_t offset;struct vnode *vp=NULL;uint32_t vid=0;int error=EINVAL;
 *out=NULL;*file_offset=0;
 if((address&3)||address>UINT64_MAX-4)return EINVAL;
 if(vm_map_range_sh_lock(ctx,&map,address,address+4,
    VMRL_SH_STREAM_NO_HOLES|VMRL_SH_DESCEND_INTO_CONSTANT)!=KERN_SUCCESS)return EFAULT;
 entry=vm_map_range_stream_next(ctx);
 if(!entry||entry->is_sub_map||!(entry->protection&VM_PROT_EXECUTE)||VME_OBJECT(entry)==VM_OBJECT_NULL)goto unlock;
 offset=vm_map_lock_ctx_offset_for_parent_address(ctx,address);
 object=VME_OBJECT(entry);vm_object_lock_shared(object);
 for(unsigned depth=0;depth<128;depth++){
  if(object->shadow!=VM_OBJECT_NULL){
   if(offset>UINT64_MAX-object->vo_shadow_offset)break;
   offset+=object->vo_shadow_offset;next=object->shadow;
   vm_object_lock_shared(next);vm_object_unlock(object);object=next;continue;
  }
  if(object->internal||!object->pager_ready||object->terminating||!object->alive||object->pager==MEMORY_OBJECT_NULL||offset>UINT64_MAX-object->paging_offset)break;
  offset+=object->paging_offset;
  vm_object_offset_t backing_offset;
  if(memory_object_backing_object(object->pager,offset,&next,&backing_offset)){
   if(next==VM_OBJECT_NULL||next==object)break;
   vm_object_lock_shared(next);vm_object_unlock(object);object=next;offset=backing_offset;continue;
  }
  vp=vnode_pager_lookup_vnode(object->pager);
  if(vp){vid=vnode_vid(vp);error=0;}
  break;
 }
 vm_object_unlock(object);
 unlock:vm_map_range_sh_unlock(ctx,&map);
 if(error||!vp)return EINVAL;
 if(vnode_getwithvid(vp,vid))return EAGAIN;
 *out=vp;*file_offset=offset;return 0;
}
#endif

#endif
