/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_BOOT_ARGS_H
#define B2L_BOOT_ARGS_H
#include "MachO.h"
struct b2l_boot_video {
    b2l_u64 base, display, row_bytes, width, height, depth;
};
/* XNU rel/xnu-12377 pexpert/pexpert/arm64/boot.h. Natural AArch64 alignment. */
struct b2l_boot_args {
    unsigned short revision, version;
    b2l_u64 virtual_base, physical_base, memory_size, top_of_kernel_data;
    struct b2l_boot_video video;
    b2l_u32 machine_type;
    b2l_u64 device_tree;
    b2l_u32 device_tree_length;
    char command_line[1024];
    b2l_u64 flags, actual_memory_size;
};
#ifdef B2L_UEFI_BUILD
STATIC_ASSERT(sizeof(struct b2l_boot_args) == 1152, "XNU boot_args size");
STATIC_ASSERT(OFFSET_OF(struct b2l_boot_args, command_line) == 108, "XNU command line offset");
STATIC_ASSERT(OFFSET_OF(struct b2l_boot_args, video) == 40, "XNU Boot_Video offset");
#else
_Static_assert(sizeof(struct b2l_boot_args) == 1152, "XNU boot_args size");
_Static_assert(offsetof(struct b2l_boot_args, command_line) == 108, "XNU command line offset");
#endif
#endif
