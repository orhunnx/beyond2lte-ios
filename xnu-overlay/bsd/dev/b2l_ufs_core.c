/* SPDX-License-Identifier: MIT
 * Bounded byte views and GPT decoding. No firmware or kernel pointers
 * are taken from the disk. GUIDs and sector contents are never printed.
 */
#include "b2l_ufs_core.h"
#ifdef B2L_UFS_HOST_TEST
#include <errno.h>
#include <string.h>
#else
#include <sys/errno.h>
#include <libkern/libkern.h>
#endif

static uint32_t
le32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	       (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t
le64(const unsigned char *p)
{
	return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

static uint32_t
crc32_update(uint32_t crc, const unsigned char *p, size_t n)
{
	while (n--) {
		crc ^= *p++;
		for (unsigned bit = 0; bit < 8; bit++) {
			crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
		}
	}
	return crc;
}

int
b2l_ufs_capacity(const struct b2l_ufs_reader *r, uint64_t *bytes)
{
	if (!r || !bytes || !r->read || !r->scratch ||
	    r->block_size < 512 || r->block_size > B2L_UFS_MAX_BLOCK_SIZE ||
	    (r->block_size & (r->block_size - 1)) || !r->block_count ||
	    r->block_count > UINT64_MAX / r->block_size) {
		return EINVAL;
	}
	*bytes = r->block_count * r->block_size;
	return 0;
}

int
b2l_ufs_read_bytes(struct b2l_ufs_reader *r, uint64_t offset, size_t count,
    void *out, size_t *done)
{
	uint64_t capacity;
	int error;
	if (!done) {
		return EINVAL;
	}
	*done = 0;
	if ((error = b2l_ufs_capacity(r, &capacity)) != 0) {
		return error;
	}
	if ((!out && count) || offset > capacity || count > capacity - offset) {
		return EINVAL;
	}
	while (*done < count) {
		uint64_t pos = offset + *done;
		size_t within = (size_t)(pos % r->block_size);
		size_t n = r->block_size - within;
		if (n > count - *done) {
			n = count - *done;
		}
		error = r->read(pos / r->block_size, 1, r->scratch);
		if (error) {
			return error;
		}
		memcpy((unsigned char *)out + *done, r->scratch + within, n);
		*done += n;
	}
	return 0;
}

int
b2l_ufs_read_view(struct b2l_ufs_reader *r, uint64_t base, uint64_t size,
    uint64_t offset, size_t count, int write, void *out, size_t *done)
{
	uint64_t capacity;
	int error;
	if (!done) {
		return EINVAL;
	}
	*done = 0;
	/* Reject writes before any read or geometry callback. */
	if (write) {
		return EROFS;
	}
	if ((error = b2l_ufs_capacity(r, &capacity)) != 0) {
		return error;
	}
	if (base > capacity || size > capacity - base || offset > size) {
		return EINVAL;
	}
	if (count > size - offset) {
		count = (size_t)(size - offset);
	}
	return b2l_ufs_read_bytes(r, base + offset, count, out, done);
}

int
b2l_ufs_write_bounds(const struct b2l_ufs_reader *r, uint64_t base, uint64_t size,
    uint64_t offset, size_t count)
{
	uint64_t capacity;
	int error;
	/* An enclosing whole-LU or another partition view is never a write alias. */
	if (!r || !r->write || !r->writable_size || base != r->writable_base ||
	    size != r->writable_size) return EROFS;
	if ((error = b2l_ufs_capacity(r, &capacity))) return error;
	/* Validate the ENTIRE request before the first read-modify-write operation. */
	if (base > capacity || size > capacity - base ||
	    base % r->block_size || size % r->block_size || offset > size ||
	    count > size - offset) return EINVAL;
	return 0;
}

int
b2l_ufs_write_view(struct b2l_ufs_reader *r, uint64_t base, uint64_t size,
    uint64_t offset, size_t count, const void *in, size_t *done)
{
	int error;
	if (!done) return EINVAL;
	*done = 0;
	if ((error = b2l_ufs_write_bounds(r, base, size, offset, count))) return error;
	if (!in && count) return EINVAL;
	while (*done < count) {
		uint64_t pos = base + offset + *done;
		size_t within = (size_t)(pos % r->block_size);
		size_t remaining = count - *done;
		if (within || remaining < r->block_size) {
			size_t n = r->block_size - within;
			if (n > remaining) n = remaining;
			error = r->read(pos / r->block_size, 1, r->scratch);
			if (error) return error;
			memcpy(r->scratch + within, (const unsigned char *)in + *done, n);
			error = r->write(pos / r->block_size, 1, r->scratch);
			if (error) return error;
			*done += n;
		} else {
			size_t bytes = remaining > B2L_UFS_MAX_IO_BYTES ?
			    B2L_UFS_MAX_IO_BYTES : remaining;
			uint32_t blocks = (uint32_t)(bytes / r->block_size);
			error = r->write(pos / r->block_size, blocks,
			    (const unsigned char *)in + *done);
			if (error) return error;
			*done += (size_t)blocks * r->block_size;
		}
	}
	return 0;
}

struct gpt_header {
	uint64_t first, last, table;
	uint32_t entries, entry_size, table_crc;
	unsigned char disk_guid[16];
};

static int
read_header(struct b2l_ufs_reader *r, uint64_t lba, uint64_t other,
    struct gpt_header *h, unsigned char *block)
{
	size_t done;
	int error = b2l_ufs_read_bytes(r, lba * r->block_size,
	    r->block_size, block, &done);
	if (error) {
		return error;
	}
	uint32_t size = le32(block + 12), expected = le32(block + 16);
	if (memcmp(block, "EFI PART", 8) || le32(block + 8) != 0x00010000 ||
	    size < 92 || size > r->block_size || le32(block + 20) != 0 ||
	    le64(block + 24) != lba || le64(block + 32) != other) {
		return EINVAL;
	}
	memset(block + 16, 0, 4);
	if ((crc32_update(UINT32_MAX, block, size) ^ UINT32_MAX) != expected) {
		return EINVAL;
	}
	h->first = le64(block + 40);
	h->last = le64(block + 48);
	h->table = le64(block + 72);
	h->entries = le32(block + 80);
	h->entry_size = le32(block + 84);
	h->table_crc = le32(block + 88);
	memcpy(h->disk_guid, block + 56, 16);
	if (h->first < 2 || h->first > h->last || h->last >= r->block_count - 1 ||
	    !h->entries || h->entries > B2L_GPT_MAX_ENTRIES ||
	    h->entry_size < 128 || h->entry_size > 1024 ||
	    (h->entry_size & (h->entry_size - 1))) {
		return EINVAL;
	}
	uint64_t bytes = (uint64_t)h->entries * h->entry_size;
	uint64_t blocks = (bytes + r->block_size - 1) / r->block_size;
	if (h->table >= r->block_count || blocks > r->block_count - h->table) {
		return EINVAL;
	}
	if (lba == 1) {
		if (h->table < 2 || h->table >= h->first || blocks > h->first - h->table) {
			return EINVAL;
		}
	} else if (h->table <= h->last || h->table >= lba || blocks > lba - h->table) {
		return EINVAL;
	}
	return 0;
}

static int
table_crc(struct b2l_ufs_reader *r, const struct gpt_header *h,
    unsigned char *scratch)
{
	uint64_t offset = h->table * r->block_size;
	size_t remaining = (size_t)h->entries * h->entry_size;
	uint32_t crc = UINT32_MAX;
	while (remaining) {
		size_t n = remaining < B2L_UFS_MAX_BLOCK_SIZE ? remaining : B2L_UFS_MAX_BLOCK_SIZE;
		size_t done;
		int error = b2l_ufs_read_bytes(r, offset, n, scratch, &done);
		if (error) {
			return error;
		}
		crc = crc32_update(crc, scratch, n);
		offset += n;
		remaining -= n;
	}
	return (crc ^ UINT32_MAX) == h->table_crc ? 0 : EINVAL;
}

static int
nonzero(const unsigned char *p, size_t n)
{
	unsigned char any = 0;
	while (n--) {
		any |= *p++;
	}
	return any != 0;
}

int
b2l_gpt_read(struct b2l_ufs_reader *r, struct b2l_gpt *out)
{
	/* Independent of reader scratch: partial reads copy from that scratch. */
	unsigned char scratch[B2L_UFS_MAX_BLOCK_SIZE];
	struct gpt_header a, b;
	uint64_t capacity;
	int error;
	if (!out) {
		return EINVAL;
	}
	memset(out, 0, sizeof(*out));
	if ((error = b2l_ufs_capacity(r, &capacity)) != 0 || r->block_count < 6) {
		return error ? error : EINVAL;
	}
	if ((error = read_header(r, 1, r->block_count - 1, &a, scratch)) ||
	    (error = read_header(r, r->block_count - 1, 1, &b, scratch))) {
		return error;
	}
	if (a.first != b.first || a.last != b.last || a.entries != b.entries ||
	    a.entry_size != b.entry_size || a.table_crc != b.table_crc ||
	    memcmp(a.disk_guid, b.disk_guid, 16)) {
		return EINVAL;
	}
	if ((error = table_crc(r, &a, scratch)) || (error = table_crc(r, &b, scratch))) {
		return error;
	}
	for (uint32_t i = 0; i < a.entries; i++) {
		size_t done;
		error = b2l_ufs_read_bytes(r, a.table * r->block_size +
		    (uint64_t)i * a.entry_size, 128, scratch, &done);
		if (error) {
			goto fail;
		}
		if (!nonzero(scratch, 16)) {
			continue;
		}
		uint64_t first = le64(scratch + 32), last = le64(scratch + 40);
		if (!nonzero(scratch + 16, 16) || first < a.first || last > a.last || last < first) {
			error = EINVAL;
			goto fail;
		}
		for (uint32_t j = 0; j < out->partition_count; j++) {
			const struct b2l_gpt_partition *p = &out->partitions[j];
			if (first < p->first_lba + p->block_count && p->first_lba <= last) {
				error = EINVAL;
				goto fail;
			}
		}
		struct b2l_gpt_partition *p = &out->partitions[out->partition_count++];
		p->first_lba = first;
		p->block_count = last - first + 1;
		p->index = i + 1;
		for (unsigned j = 0; j < 36; j++) {
			unsigned c = scratch[56 + j * 2] | (unsigned)scratch[57 + j * 2] << 8;
			if (!c) {
				break;
			}
			p->name[j] = c >= 32 && c <= 126 ? (char)c : '?';
		}
	}
	out->entry_count = a.entries;
	return 0;
fail:
	memset(out, 0, sizeof(*out));
	return error;
}

int
b2l_gpt_label_equal(const char *a, const char *b)
{
	for (unsigned i = 0; i < B2L_GPT_NAME_SIZE; i++) {
		unsigned ca = (unsigned char)a[i], cb = (unsigned char)b[i];
		if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
		if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
		if (ca != cb) return 0;
		if (!ca) return 1;
	}
	return 0;
}
