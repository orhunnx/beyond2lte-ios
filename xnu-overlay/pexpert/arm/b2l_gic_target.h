/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_GIC_TARGET_H
#define B2L_GIC_TARGET_H
/* GICv2 CPU targets are discovered through banked ITARGETSR0, not assumed
 * equal to MPIDR Aff0. Reject zero, non-one-hot and duplicate target masks. */
static inline int b2l_gic_valid_target(unsigned mask, unsigned other)
{
    return mask && mask <= 255 && !(mask & (mask - 1)) && !(mask & other);
}
static inline unsigned b2l_gic_sgir(unsigned mask)
{
    return b2l_gic_valid_target(mask, 0) ? (mask << 16) | 1U : 0U;
}
#endif
