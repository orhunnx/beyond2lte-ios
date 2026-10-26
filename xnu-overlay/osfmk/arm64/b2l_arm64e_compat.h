/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_ARM64E_COMPAT_H
#define B2L_ARM64E_COMPAT_H
#include <stdbool.h>
#include <stdint.h>
struct arm_saved_state;
#ifdef __cplusplus
extern "C" {
#endif
bool b2l_arm64e_compat_enabled(void);
/* Boot-stable algorithm label; contains no key or operand data. */
const char *b2l_arm64e_pacga_name(void);
/* Best-effort relaxed telemetry: traps, completed, last pid and last PC. */
void b2l_arm64e_compat_snapshot(uint64_t values[4]);
/* samples, successful wrapper/VM/hash nanoseconds, paired extras, fallbacks */
void b2l_arm64e_perf_snapshot(uint64_t values[6]);
bool b2l_arm64e_current_task(void);
bool b2l_arm64e_compat_emulate(struct arm_saved_state *state, uint32_t instruction);
#ifdef __cplusplus
}
#endif
#endif
