/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_LOG_CORE_H
#define B2L_LOG_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifndef B2L_LOG_TEXT_CAPACITY
#define B2L_LOG_TEXT_CAPACITY (4u*1024u*1024u)
#endif
#define B2L_LOG_PAGE_BYTES 4096u
#define B2L_LOG_REGION_BYTES (16u*1024u*1024u)
#define B2L_LOG_DATA_OFFSET (2u*B2L_LOG_PAGE_BYTES)
#define B2L_LOG_RECORD_COUNT ((B2L_LOG_REGION_BYTES-B2L_LOG_DATA_OFFSET)/B2L_LOG_PAGE_BYTES)
#define B2L_LOG_RECORD_SPAN 3584u
#define B2L_LOG_BITMAP_BYTES 448u
#define B2L_LOG_SEQUENCE_LIMIT UINT64_C(0x00ffffffffffffff)
/* 4 MiB of retained text occupies 32 MiB of atomic sequence-tagged storage. */
struct b2l_log_ring {
    uint64_t next;
    uint64_t dropped;
    uint64_t slots[B2L_LOG_TEXT_CAPACITY];
};
struct b2l_log_sink {
    int (*read)(void*,uint64_t,void*,size_t);
    int (*write)(void*,uint64_t,const void*,size_t);
    int (*sync)(void*);
    void *context;
    uint64_t region_bytes;
};
struct b2l_log_writer {
    struct b2l_log_sink sink;
    uint64_t epoch,cursor,record_index,overwritten;
    uint8_t page[B2L_LOG_PAGE_BYTES];
    uint8_t active;
};
enum {B2L_LOG_BAD_ARGUMENT=-1,B2L_LOG_INVALID_HEADER=-2,B2L_LOG_EXHAUSTED=-3};
void b2l_log_ring_append(struct b2l_log_ring*,uint8_t);
/* Pure bounded publication operation exposed to test delayed writer ordering. */
int b2l_log_ring_publish(struct b2l_log_ring*,uint64_t zero_based_sequence,uint8_t);
uint64_t b2l_log_ring_end(const struct b2l_log_ring*);
uint64_t b2l_log_ring_drops(const struct b2l_log_ring*);
int b2l_log_ring_read(const struct b2l_log_ring*,uint64_t,uint8_t*);
uint32_t b2l_log_page_crc(const uint8_t page[B2L_LOG_PAGE_BYTES]);
void b2l_log_make_header(uint8_t page[B2L_LOG_PAGE_BYTES],uint64_t epoch);
int b2l_log_parse_header(const uint8_t page[B2L_LOG_PAGE_BYTES],uint64_t*epoch);
int b2l_log_writer_open(struct b2l_log_writer*,const struct b2l_log_sink*);
/* Single worker only. Cursor/index commit only after the entire batch syncs. */
int b2l_log_writer_drain(struct b2l_log_writer*,const struct b2l_log_ring*,unsigned max_records);
#endif
