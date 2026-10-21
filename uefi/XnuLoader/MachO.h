#ifndef B2L_MACHO_H
#define B2L_MACHO_H
#ifdef B2L_UEFI_BUILD
#include <Base.h>
typedef UINT8 b2l_u8;
typedef UINT32 b2l_u32;
typedef UINT64 b2l_u64;
typedef UINTN b2l_size;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t b2l_u8;
typedef uint32_t b2l_u32;
typedef uint64_t b2l_u64;
typedef size_t b2l_size;
#endif
#define B2L_MAX_SEGMENTS 32
struct b2l_segment {
    b2l_u64 va, size, file_offset, file_size;
    b2l_u32 protection;
};
struct b2l_macho {
    b2l_u64 virtual_base, virtual_end, entry;
    b2l_u32 segment_count;
    struct b2l_segment segments[B2L_MAX_SEGMENTS];
};
/* Accept only a fully linked, plain ARM64 MH_EXECUTE with no chained fixups.
 * Every source/destination range is checked before the caller copies bytes. */
int b2l_parse_macho(const void *data, b2l_size length, struct b2l_macho *out);
#endif
