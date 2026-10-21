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
#endif
#include "b2l_compat.h"
#include "b2l_pacga_qarma5.h"
extern uint64_t b2l_pacga_qarma5_asm(uint64_t, uint64_t, uint64_t, uint64_t);

static bool b2l_compat_requested;
static void
b2l_compat_init(void)
{
    int requested = 0;
    b2l_compat_requested =
        PE_parse_boot_argn("b2l_arm64e_compat", &requested, sizeof(requested)) && requested == 1;
    if (b2l_compat_requested) {
        /* TUNABLES runs before log_lock initialization. Use only the
         * already mapped, lock-free early framebuffer sink here. */
        const char *message = "B2L ARM64e: development stripped-pointer runtime enabled; hardware PAC unchanged\n";
        while (*message) b2l_early_putc(*message++);
    }
}
STARTUP(TUNABLES, STARTUP_RANK_MIDDLE, b2l_compat_init);

bool
b2l_arm64e_compat_enabled(void)
{
    return b2l_compat_requested;
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
b2l_compat_validate_pc(uint64_t address, void *opaque __unused)
{
    uint32_t instruction;
    static unsigned check_samples;
    bool trace = check_samples < 2;
    if (trace) { ++check_samples; b2l_early_trace("B2L COMPAT: execute-map check begin pc=0x%llx\n", address); }
    if ((address & 3u) || !b2l_user_range(address, sizeof(instruction))) return -1;
    /* Shared-cache parent entries are intentionally r--. Descend to the
     * actual constant-submap leaf before checking execute permission. The
     * short-info query avoids scanning resident pages on every trap. */
    vm_map_offset_ut region = vm_sanitize_wrap_addr(address);
    vm_map_size_ut size = vm_sanitize_wrap_size(0);
    natural_t depth = UINT32_MAX;
    vm_region_submap_short_info_data_64_t info = {0};
    mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;
    kern_return_t kr = vm_map_region_recurse_64(current_map(), &region, &size,
        &depth, (vm_region_submap_info_64_t)&info, &count);
    uint64_t region_start = VM_SANITIZE_UNSAFE_UNWRAP(region);
    uint64_t region_size = VM_SANITIZE_UNSAFE_UNWRAP(size);
    if (kr != KERN_SUCCESS || count != VM_REGION_SUBMAP_SHORT_INFO_COUNT_64 ||
        info.is_submap || !(info.protection & VM_PROT_EXECUTE) ||
        region_start > address || region_size < sizeof(instruction) ||
        address - region_start > region_size - sizeof(instruction)) return -1;
    if (trace) b2l_early_trace("B2L COMPAT: execute-map check passed; copyin begin\n");
    int error = copyin_atomic32(address, &instruction);
    if (trace) b2l_early_trace("B2L COMPAT: execute-PC copyin result=%d\n", error);
    return error ? -1 : 0;
}

static int
b2l_compat_pacga(uint64_t a, uint64_t b, uint64_t *out, void *opaque __unused)
{
    if (!out) return -1;
    uint64_t key[2];
    /* First invocation is an EL0 trap after BSD startup and PRNG registration.
     * A boot-shared software key is required for cross-process shared objects.
     * This is software PACGA hashing, not pointer authentication enforcement. */
    lck_mtx_lock(&b2l_compat_key_lock);
    if (!b2l_compat_key_ready) {
        read_frandom(b2l_compat_key, sizeof(b2l_compat_key));
        b2l_compat_key_ready = true;
    }
    memcpy(key, b2l_compat_key, sizeof(key));
    lck_mtx_unlock(&b2l_compat_key_lock);
    *out = b2l_pacga_qarma5_asm(a, b, key[0], key[1]);
    (void)memset_s(key, sizeof(key), 0, sizeof(key));
    return 0;
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
    struct b2l_compat_ctx context = {0};
    for (unsigned i = 0; i < 29; ++i) context.x[i] = get_saved_state_reg(state, i);
    context.x[29] = get_saved_state_fp(state);
    context.x[30] = get_saved_state_lr(state);
    context.sp = get_saved_state_sp(state);
    context.pc = get_saved_state_pc(state);
    context.cpsr = get_saved_state_cpsr(state);
    if (b2l_compat_validate_pc(context.pc, NULL)) return false;
    const struct b2l_compat_ops operations = {
        .mode = B2L_COMPAT_MODE_STRIPPED,
        .read = b2l_compat_read, .read_acquire = b2l_compat_read_acquire,
        .validate_pc = b2l_compat_validate_pc, .pacga = b2l_compat_pacga
    };
    if (trace) b2l_early_trace("B2L COMPAT: decoder begin\n");
    enum b2l_compat_result result = b2l_compat_emulate(instruction, &context, &operations);
    if (trace) b2l_early_trace("B2L COMPAT: decoder result=%u\n", result);
    if (result != B2L_COMPAT_OK) {
        b2l_early_trace("B2L COMPAT: rejected result=%u pc=0x%llx x8=0x%llx x16=0x%llx lr=0x%llx\n",
            result, context.pc, context.x[8], context.x[16], context.x[30]);
        return false;
    }
    for (unsigned i = 0; i < 29; ++i) set_user_saved_state_reg(state, i, context.x[i]);
    set_saved_state_fp(state, context.x[29]);
    set_user_saved_state_lr(state, context.x[30]);
    set_saved_state_sp(state, context.sp);
    set_user_saved_state_pc(state, context.pc);
    /* Decoder preserves CPSR; never let emulation modify mode/masks/NZCV. */
    return true;
}
