/* SPDX-License-Identifier: BSD-2-Clause */
/* Exact data admission for SecCodeMapMemory/F_ADDSIGS in unsigned B2L mode.
 * No cs_blob is attached, no VM page or Apple trust-chain claim is made. */
#include "CacheCore.h"
static const struct { size_t bytes; uint8_t sha256[32]; } b2l_detached_pins[] = {
    {668, {0xc4, 0x46, 0x62, 0x93, 0xe2, 0xae, 0x41, 0xb3, 0xa1, 0xdf, 0x24, 0xc6, 0xb2, 0x1f, 0x0c, 0x9d, 0xa0, 0x93, 0x6b, 0xf7, 0x1f, 0xd1, 0xc4, 0x72, 0xde, 0x43, 0xdb, 0x25, 0x63, 0x5a, 0x11, 0x15}},
    {980, {0x34, 0x81, 0xd5, 0x07, 0xe8, 0x58, 0x75, 0x8e, 0x8b, 0xed, 0xd8, 0xb5, 0x68, 0x8b, 0xdc, 0x6c, 0x14, 0x93, 0xd7, 0x24, 0x92, 0x7b, 0x0e, 0x63, 0x75, 0xb7, 0x0e, 0xdc, 0x25, 0xcb, 0x7e, 0x96}},
};
static const uint8_t b2l_extension_plist_sha256[32] = {0x8f, 0x4d, 0x7b, 0x15, 0x76, 0x73, 0x1c, 0x56, 0xeb, 0x8e, 0x67, 0xb3, 0xa9, 0x02, 0x94, 0x17, 0xaa, 0xce, 0x6c, 0x66, 0x10, 0x02, 0x44, 0xc9, 0xba, 0x96, 0xa7, 0x8e, 0xb0, 0xf4, 0x8c, 0xd1 };
static bool
b2l_detached_pin(const void *bytes, size_t length)
{
    uint8_t digest[32];
    for (unsigned i = 0; i < sizeof(b2l_detached_pins) / sizeof(b2l_detached_pins[0]); i++) {
        if (length != b2l_detached_pins[i].bytes) continue;
        b2l_sha256(bytes, length, digest);
        return !memcmp(digest, b2l_detached_pins[i].sha256, sizeof(digest));
    }
    return false;
}
#ifndef B2L_DETACHED_HOST_TEST
static int
b2l_detached_cache_map(proc_t process, vnode_t vnode, const struct user_fsignatures *fs)
{
    if (!b2l_amfi_cache_entitled(process) || !IS_64BIT_PROCESS(process) ||
        vnode_vtype(vnode) != VREG || fs->fs_file_start != 0 ||
        (fs->fs_blob_size != 668 && fs->fs_blob_size != 980)) return ENOTSUP;
    char path[MAXPATHLEN]; int path_length = sizeof(path);
    if (vn_getpath(vnode, path, &path_length) ||
        strcmp(path, "/System/Library/xpc/extensions.plist")) return ENOTSUP;
    uint8_t directory[980];
    int error = copyin(fs->fs_blob_start, directory, fs->fs_blob_size);
    if (error) return error;
    if (!b2l_detached_pin(directory, fs->fs_blob_size)) return EAUTH;
    off_t size = 0;
    error = vnode_size(vnode, &size, vfs_context_current());
    if (error) return error;
    if (size != 98192) return EAUTH;
    /* Hash actual vnode bytes, not the caller's plist pointer. */
    uint8_t *buffer = kalloc_data(4096, Z_WAITOK);
    if (!buffer) return ENOMEM;
    struct b2l_sha hash; b2l_sha_init(&hash);
    for (off_t at = 0; at < size; at += 4096) {
        int take = (int)MIN(size - at, 4096), remaining = 0;
        error = vn_rdwr(UIO_READ, vnode, (caddr_t)buffer, take, at,
            UIO_SYSSPACE, 0, kauth_cred_get(), &remaining, process);
        if (error || remaining) { if (!error) error = EIO; break; }
        b2l_sha_update(&hash, buffer, take);
    }
    kfree_data(buffer, 4096);
    if (error) return error;
    uint8_t digest[32]; b2l_sha_final(&hash, digest);
    if (memcmp(digest, b2l_extension_plist_sha256, sizeof(digest))) return EAUTH;
    static unsigned reports;
    if (os_atomic_inc_orig(&reports, relaxed) < 4)
        printf("B2L AMFI: exact extension plist F_ADDSIGS admission; no trusted VM pages\n");
    return 0;
}
#endif
