/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_RAM_H
#define B2L_RAM_H
#include <pexpert/arm64/b2l_ram_ranges.h>
extern struct b2l_ram_range b2l_owned_ram[B2L_RAM_MAX_RANGES];
extern unsigned b2l_owned_ram_count;
extern unsigned long long b2l_owned_ram_bytes;
static inline int b2l_managed_pa(unsigned long long pa)
{return b2l_ram_contains_range(b2l_owned_ram,b2l_owned_ram_count,pa,1);}
static inline unsigned b2l_managed_index(unsigned long long pa)
{return b2l_ram_pai(b2l_owned_ram,b2l_owned_ram_count,pa);}
static inline unsigned long long b2l_managed_pa_for_index(unsigned idx)
{return b2l_ram_pa(b2l_owned_ram,b2l_owned_ram_count,idx);}
#endif
