/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_ARENA_H
#define B2L_ARENA_H
#include "MachO.h"
#include "b2l_ram_ranges.h"
#define B2L_MIN_ARENA (1024ULL * 1024 * 1024)
#define B2L_MAX_ARENA (4ULL * 1024 * 1024 * 1024)
/* Live 8GiB board: preserve first upper bank and top40MiB EL2 early memory. */
#define B2L_RAM_START 0x900000000ULL
#define B2L_RAM_END   0x9FD800000ULL
#define B2L_ALIGNMENT (2ULL * 1024 * 1024)
#define B2L_EFI_HEADROOM (32ULL * 1024 * 1024)
/* EFI descriptor wire layout is stable at its first 40 bytes; firmware may
 * extend descriptor stride. Return an aligned span in EfiConventionalMemory. */
int b2l_choose_arena(const void *map, b2l_size map_size, b2l_size descriptor_size,
                     b2l_u64 *start, b2l_u64 *size);
/* Validate final allocated span in an unsorted EFI map (type2 LoaderData). */
int b2l_validate_arena(const void *map,b2l_size map_size,b2l_size descriptor_size,
                       b2l_u64 start,b2l_u64 size,b2l_u32 expected_type);
/* Firmware-owned 16KiB secondary trampoline, selected only from real low
 * EFI conventional pages; validate ownership again before ExitBootServices. */
int b2l_choose_secondary(const void *,b2l_size,b2l_size,b2l_u64 *);
int b2l_validate_secondary(const void *,b2l_size,b2l_size,b2l_u64,b2l_u32);
int b2l_collect_extra_ram(const void *,b2l_size,b2l_size,b2l_u64,b2l_u64,
                          struct b2l_ram_range *,unsigned *);
int b2l_validate_owned_ram(const void *,b2l_size,b2l_size,const struct b2l_ram_range *,unsigned);
#endif
