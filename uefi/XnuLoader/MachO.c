#include "MachO.h"
#define U64_MAX_VALUE ((b2l_u64)~(b2l_u64)0)
static b2l_u32 read32(const b2l_u8 *p) {
    return (b2l_u32)p[0] | (b2l_u32)p[1] << 8 | (b2l_u32)p[2] << 16 | (b2l_u32)p[3] << 24;
}
static b2l_u64 read64(const b2l_u8 *p) { return read32(p) | (b2l_u64)read32(p + 4) << 32; }
static int within(b2l_u64 off, b2l_u64 size, b2l_u64 length) { return off <= length && size <= length - off; }
int b2l_parse_macho(const void *data, b2l_size length, struct b2l_macho *out) {
    const b2l_u8 *p = data;
    b2l_u64 offset = 32, commands_end, lowest = U64_MAX_VALUE, highest = 0, entry = 0;
    b2l_u32 i, j, count, command_size, cmd, size, n = 0;
    int have_entry = 0, executable_entry = 0, mapped_header = 0;
    if (!p || !out || length < 32 || read32(p) != 0xfeedfacf ||
        read32(p + 4) != 0x0100000c || (read32(p + 8) & 0x00ffffff) != 0 ||
        read32(p + 12) != 2) return -1;
    count = read32(p + 16); command_size = read32(p + 20);
    if (!count || count > 4096 || !within(32, command_size, length)) return -2;
    commands_end = 32 + (b2l_u64)command_size;
    for (i = 0; i < count; ++i) {
        if (!within(offset, 8, commands_end)) return -3;
        cmd = read32(p + offset); size = read32(p + offset + 4);
        if (size < 8 || (size & 7) || !within(offset, size, commands_end)) return -4;
        if (cmd == 0x19) {
            b2l_u64 va, vs, fo, fs;
            b2l_u32 sections;
            if (size < 72) return -5;
            va = read64(p + offset + 24); vs = read64(p + offset + 32);
            fo = read64(p + offset + 40); fs = read64(p + offset + 48);
            sections = read32(p + offset + 64);
            if (sections > (size - 72) / 80 || size != 72 + (b2l_u64)sections * 80 ||
                fs > vs || (read32(p + offset + 68) & 1) ||
                !within(fo, fs, length) || va > U64_MAX_VALUE - vs) return -6;
            if (vs) {
                if (n == B2L_MAX_SEGMENTS || (va & 4095) || (vs & 4095)) return -7;
                for (j = 0; j < n; ++j) {
                    const struct b2l_segment *s = &out->segments[j];
                    if (va < s->va + s->size && s->va < va + vs) return -8;
                }
                out->segments[n].va = va; out->segments[n].size = vs;
                out->segments[n].file_offset = fo; out->segments[n].file_size = fs;
                out->segments[n].protection = read32(p + offset + 60);
                if (fo == 0 && fs >= commands_end) mapped_header = 1;
                ++n;
                if (va < lowest) lowest = va;
                if (va + vs > highest) highest = va + vs;
            }
        } else if (cmd == 5) {
            if (have_entry || size != 288 || read32(p + offset + 8) != 6 || read32(p + offset + 12) != 68) return -9;
            entry = read64(p + offset + 16 + 32 * 8); have_entry = 1;
        } else if (cmd == 0x80000034) {
            if (size != 16 || read32(p + offset + 12) != 0) return -10;
        } else if (cmd == 0x22 || cmd == 0x80000022) {
            /* Rebase records need no changes at slide zero. Imported symbols do. */
            if (size != 48 || read32(p + offset + 20) || read32(p + offset + 28) || read32(p + offset + 36)) return -11;
        } else if (cmd == 0xc || cmd == 0x18 || cmd == 0x80000018 || cmd == 0x8000001f || cmd == 0x80000023) {
            return -12; /* The kernel must not depend on a userspace dynamic loader. */
        }
        offset += size;
    }
    if (offset != commands_end || !n || !have_entry || !mapped_header || (entry & 3)) return -13;
    for (j = 0; j < n; ++j) {
        const struct b2l_segment *s = &out->segments[j];
        if ((s->protection & 4) && within(entry - s->va, 4, s->file_size) && entry >= s->va) executable_entry = 1;
    }
    if (!executable_entry || highest > U64_MAX_VALUE - 4095 || lowest < 0xffff000000000000ULL) return -14;
    out->virtual_base = lowest & ~0x1fffffULL;
    out->virtual_end = (highest + 4095) & ~4095ULL;
    out->entry = entry; out->segment_count = n;
    return 0;
}
