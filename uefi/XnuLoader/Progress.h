/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_PROGRESS_H
#define B2L_PROGRESS_H
#include "MachO.h"
struct b2l_handoff_info {
    b2l_u64 panel_base, stride;
    b2l_u64 kernel_start, kernel_end;
    b2l_u64 image_start, image_end;
    b2l_u64 stack_start, stack_end;
};
#ifdef B2L_UEFI_BUILD
STATIC_ASSERT(sizeof(struct b2l_handoff_info)==64,"handoff register layout");
STATIC_ASSERT(OFFSET_OF(struct b2l_handoff_info,kernel_start)==16,"handoff kernel range offset");
STATIC_ASSERT(OFFSET_OF(struct b2l_handoff_info,stack_start)==48,"handoff stack range offset");
#else
_Static_assert(sizeof(struct b2l_handoff_info)==64,"handoff register layout");
#endif
void B2lProgressPrepare(b2l_u64 Base, b2l_u64 Stride, b2l_u64 Width, b2l_u64 Height,
                        struct b2l_handoff_info *Info);
void B2lProgressSet(const struct b2l_handoff_info *Info, b2l_size Stage, b2l_u32 Color);
#endif
