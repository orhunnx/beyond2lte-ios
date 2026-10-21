/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_ARENA_H
#define B2L_ARENA_H
#include "MachO.h"
#define B2L_MIN_ARENA (1024ULL * 1024 * 1024)
#define B2L_MAX_ARENA (2ULL * 1024 * 1024 * 1024)
#define B2L_ALIGNMENT (2ULL * 1024 * 1024)
#define B2L_EFI_HEADROOM (32ULL * 1024 * 1024)
/* EFI descriptor wire layout is stable at its first 40 bytes; firmware may
 * extend descriptor stride. Return an aligned span in EfiConventionalMemory. */
int b2l_choose_arena(const void *map, b2l_size map_size, b2l_size descriptor_size,
                     b2l_u64 *start, b2l_u64 *size);
#endif
