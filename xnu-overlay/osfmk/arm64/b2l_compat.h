#ifndef B2L_COMPAT_H
#define B2L_COMPAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Explicit development ABI: all pointer producers must already supply raw
 * pointers. No PAC verification, pointer masking or hardware-security claim. */
enum b2l_compat_mode { B2L_COMPAT_MODE_STRIPPED = 1 };
enum b2l_compat_result {
    B2L_COMPAT_UNHANDLED = 0,
    B2L_COMPAT_OK = 1,
    B2L_COMPAT_FAULT = 2
};

struct b2l_compat_ctx {
    uint64_t x[31];                 /* x0..x30; slot31 does not exist */
    uint64_t sp;
    uint64_t pc;
    uint32_t cpsr;                  /* preserved bit-for-bit */
};

/* All callbacks return zero on success. read/read_acquire produce a scalar in
 * target little-endian byte order, zero extended to uint64_t. They must perform
 * bounded EL0 access, support faults, and never access device/kernel mappings.
 * read_acquire must provide at least an architectural full acquire load, even
 * when destination is WZR/XZR. read is an ordinary data read.
 * validate_pc must reject unaligned/out-of-user-bounds/non-executable targets.
 * pacga returns a full64-bit architectural PACGA result (upper32 bits signature,
 * lower32 zero); it is required even for discarded XZR results.
 * Callbacks must not mutate ctx; callback-owned statistics may change on fault.
 */
struct b2l_compat_ops {
    enum b2l_compat_mode mode;
    int (*read)(uint64_t address, unsigned width, uint64_t *out, void *opaque);
    int (*read_acquire)(uint64_t address, unsigned width, uint64_t *out,
                        void *opaque);
    int (*validate_pc)(uint64_t address, void *opaque);
    int (*pacga)(uint64_t a, uint64_t b, uint64_t *out, void *opaque);
    void *opaque;
};

/* Does not fetch instructions. Caller must fetch insn from bounded executable
 * EL0 memory and enter only for an explicitly opted-in ARM64e task. On FAULT or
 * UNHANDLED ctx is unchanged. On OK pc is advanced/branched once, cpsr unchanged.
 * Constrained-unpredictable LDRA writeback overlap is conservatively rejected.
 */
enum b2l_compat_result b2l_compat_emulate(uint32_t insn,
    struct b2l_compat_ctx *ctx, const struct b2l_compat_ops *ops);

/* Independent C oracle also available for host differential testing. */
enum b2l_compat_result b2l_compat_emulate_reference(uint32_t insn,
    struct b2l_compat_ctx *ctx, const struct b2l_compat_ops *ops);

#ifdef __cplusplus
}
#endif
#endif
