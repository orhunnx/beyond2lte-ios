/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_AMFI_METADATA_H
#define B2L_AMFI_METADATA_H
#include <stddef.h>
#include <stdint.h>
#define B2L_AMFI_XML_MAX 65536u
#define B2L_AMFI_IDENTITY_MAX 1024u
#define B2L_AMFI_SIGNATURE_MAX (2u * 1024u * 1024u)
struct b2l_amfi_reader {
    uint64_t bytes;
    int (*read)(void *opaque, uint64_t offset, void *out, size_t length);
    void *opaque;
};
enum b2l_amfi_metadata_result {
    B2L_AMFI_XML_PRESENT = 0, B2L_AMFI_XML_ABSENT = 1,
    B2L_AMFI_XML_INVALID = 2, B2L_AMFI_XML_IO = 3
};
/* Metadata only: no CodeDirectory/hash/signature validation. XML buffer includes
 * the8-byte CS_GenericBlob header; *length is zero on every non-PRESENT path. */
enum b2l_amfi_metadata_result b2l_amfi_read_xml(
    const struct b2l_amfi_reader *, uint64_t slice_offset,
    void *xml, size_t capacity, size_t *length);
/* Owned metadata string producer; includes its terminating NUL. Only the
 * primary CodeDirectory slot is considered. No signature/hash validation. */
enum b2l_amfi_metadata_result b2l_amfi_read_identity(
    const struct b2l_amfi_reader *, uint64_t slice_offset,
    void *identity, size_t capacity, size_t *length);
#endif
