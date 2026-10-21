/* SPDX-License-Identifier: MIT */
#ifndef B2L_UFS_DISK_H
#define B2L_UFS_DISK_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called once after BSD pseudo devices and before root discovery. */
void b2l_ufs_disk_init(void);
/* Only the selected unique HFS SYSTEM/USERDATA node can become a native root. */
int b2l_ufs_root_device(dev_t *dev);
#ifdef __cplusplus
}
#endif
#endif
