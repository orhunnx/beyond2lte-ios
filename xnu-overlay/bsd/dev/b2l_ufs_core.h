/* SPDX-License-Identifier: MIT */
#ifndef B2L_UFS_CORE_H
#define B2L_UFS_CORE_H
#include <stddef.h>
#include <stdint.h>

#define B2L_UFS_MAX_BLOCK_SIZE 4096U
#define B2L_UFS_MAX_IO_BYTES 65536U
#define B2L_GPT_MAX_ENTRIES 256U
#define B2L_GPT_NAME_SIZE 37U

struct b2l_ufs_reader {
	uint64_t block_count;
	uint32_t block_size;
	/* Physical logical-block units; callback must fill all blocks or fail. */
	int (*read)(uint64_t lba, uint32_t blocks, void *out);
	unsigned char *scratch; /* Caller-owned block_size bytes; serialize use. */
	/* Writes are authorized for one validated, block-aligned SYSTEM view only. */
	int (*write)(uint64_t lba, uint32_t blocks, const void *in);
	uint64_t writable_base;
	uint64_t writable_size;
};

struct b2l_gpt_partition {
	uint64_t first_lba;
	uint64_t block_count;
	uint32_t index; /* One-based original entry index, not compacted index. */
	char name[B2L_GPT_NAME_SIZE]; /* Printable ASCII, '?' for other code units. */
};

struct b2l_gpt {
	uint32_t entry_count;
	uint32_t partition_count;
	struct b2l_gpt_partition partitions[B2L_GPT_MAX_ENTRIES];
};

int b2l_ufs_capacity(const struct b2l_ufs_reader *, uint64_t *);
int b2l_ufs_read_bytes(struct b2l_ufs_reader *, uint64_t, size_t, void *, size_t *);
int b2l_ufs_read_view(struct b2l_ufs_reader *, uint64_t, uint64_t,
    uint64_t, size_t, int, void *, size_t *);
int b2l_ufs_write_view(struct b2l_ufs_reader *, uint64_t, uint64_t,
    uint64_t, size_t, const void *, size_t *);
int b2l_ufs_write_bounds(const struct b2l_ufs_reader *, uint64_t, uint64_t,
    uint64_t, size_t);
int b2l_gpt_read(struct b2l_ufs_reader *, struct b2l_gpt *);
int b2l_gpt_label_equal(const char *, const char *);
#endif
