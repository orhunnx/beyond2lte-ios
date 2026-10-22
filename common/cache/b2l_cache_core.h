/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_NATIVE_CACHE_CORE_H
#define B2L_NATIVE_CACHE_CORE_H
#include "CacheCore.h"
#define B2L_NC_BYTES (128u*1024u*1024u)
#define B2L_NC_HEADER 8192u
#define B2L_NC_COPY 12288u
#define B2L_NC_CELL (2u*B2L_NC_COPY)
#define B2L_NC_SLOTS ((B2L_NC_BYTES-B2L_NC_HEADER)/B2L_NC_CELL)
#define B2L_NC_PAGE 4096u
#define B2L_NC_DATA 160u
/* Byte-serialized LE format; never cast persisted bytes to C structures. */
uint32_t b2l_nc_u32(const uint8_t*);
uint64_t b2l_nc_u64(const uint8_t*);
void b2l_nc_p32(uint8_t*,uint32_t);
void b2l_nc_p64(uint8_t*,uint64_t);
uint32_t b2l_nc_crc(const void*,size_t);
int b2l_nc_native(uint32_t,uint32_t*);
int b2l_nc_header(const uint8_t*,const uint8_t root_id[12]);
/* V2 separates uint64_t disk capacity from RAM; v1 remains readable. */
uint64_t b2l_nc_header_bytes(const uint8_t*);
uint32_t b2l_nc_header_used(const uint8_t*);
uint64_t b2l_nc_header_generation(const uint8_t*);
int b2l_nc_headers(const uint8_t*,const uint8_t[12],uint64_t,const uint8_t**);
int b2l_nc_header_v2(uint8_t*,const uint8_t[12],uint64_t,uint32_t,uint64_t);
int b2l_nc_record(const uint8_t*);
/* Returns selected copy, NULL for an unused/torn cell, -1 on ambiguous copies. */
int b2l_nc_select(const uint8_t*,const uint8_t**);
void b2l_nc_seal(uint8_t*);
int b2l_nc_page_state(const uint8_t*,const uint8_t*,unsigned);
#endif
