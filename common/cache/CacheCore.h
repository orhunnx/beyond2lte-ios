/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_CACHE_CORE_H
#define B2L_CACHE_CORE_H
#ifdef B2L_UEFI_BUILD
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
typedef UINT8 uint8_t;typedef UINT16 uint16_t;typedef UINT32 uint32_t;typedef UINT64 uint64_t;typedef UINTN size_t;
#define memcpy(d,s,n) CopyMem((d),(s),(n))
#define memset(d,v,n) SetMem((d),(n),(v))
#define memcmp(a,b,n) CompareMem((a),(b),(n))
#define strcmp(a,b) AsciiStrCmp((a),(b))
#elif defined(KERNEL)
#include <stdint.h>
#include <stddef.h>
#include <libkern/libkern.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#endif
#define B2L_HFS_NODE_MAX 32768u
#define B2L_CACHE_MAX_FILE (16u*1024u*1024u)
struct b2l_io {int (*read)(void*,uint64_t,void*,size_t);int (*write)(void*,uint64_t,const void*,size_t);int (*sync)(void*);void *ctx;uint64_t bytes;};
struct b2l_fork {uint64_t size;uint32_t start[8],count[8];};
struct b2l_hfs {struct b2l_io io;uint64_t bytes;uint32_t block_size,blocks,create_date,jib;uint8_t uuid[8];struct b2l_fork catalog;uint8_t node[B2L_HFS_NODE_MAX];};
struct b2l_hfs_file {struct b2l_fork data;uint32_t id;};
struct b2l_cache_fork {struct b2l_io volume;struct b2l_fork data;uint32_t block_size;};
struct b2l_sha {uint32_t h[8];uint64_t bytes;uint8_t block[64];unsigned used;};
void b2l_sha_init(struct b2l_sha*);
void b2l_sha_update(struct b2l_sha*,const void*,size_t);
void b2l_sha_final(struct b2l_sha*,uint8_t out[32]);
void b2l_sha256(const void*,size_t,uint8_t out[32]);
int b2l_hfs_open(struct b2l_hfs*,const struct b2l_io*);
int b2l_hfs_journal_empty(struct b2l_hfs*);
int b2l_hfs_find(struct b2l_hfs*,const char*,struct b2l_hfs_file*);
int b2l_hfs_read(struct b2l_hfs*,const struct b2l_hfs_file*,uint64_t,void*,size_t);
int b2l_hfs_cache_view(struct b2l_hfs*,const struct b2l_hfs_file*,struct b2l_cache_fork*,struct b2l_io*);
int b2l_hfs_cache_extent(struct b2l_hfs*,const struct b2l_hfs_file*,uint64_t*);
#endif
