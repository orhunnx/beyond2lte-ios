/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_UFS_TRANSPORT_H
#define B2L_UFS_TRANSPORT_H
#ifdef KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#endif
#define B2L_UFS_MAX_TRANSFER_BYTES 65536U
#define B2L_UFS_MAX_READ_BYTES B2L_UFS_MAX_TRANSFER_BYTES
struct b2l_ufs_geometry {
    uint64_t block_count;
    uint32_t block_size;
    uint32_t lun;
};
/* Native device LBAs, not BSD 512-byte sectors. These kernel-internal APIs
 * are not raw device nodes. The BSD adapter must enforce the validated SYSTEM
 * extent before every write; no whole-LU or other-partition writes are allowed. */
int b2l_ufs_transport_init(struct b2l_ufs_geometry *out);
int b2l_ufs_transport_read(uint64_t lba, uint32_t blocks, void *out);
/* A successful write includes a successful device SYNCHRONIZE CACHE command. */
int b2l_ufs_transport_write(uint64_t lba, uint32_t blocks, const void *in);
int b2l_ufs_transport_sync(void);
#endif
