/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_NATIVE_CACHE_H
#define B2L_NATIVE_CACHE_H
#include <stdint.h>
#include "CacheCore.h"
void b2l_native_cache_storage(const struct b2l_io*,const uint8_t[12]);
void b2l_native_cache_before_init(void);
void b2l_native_cache_observe(uint64_t,uint32_t);
#endif
