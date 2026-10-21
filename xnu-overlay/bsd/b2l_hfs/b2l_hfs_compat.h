/* SPDX-License-Identifier: BSD-2-Clause
 * HFS is built with the public/private KPI view used by its upstream kext,
 * not XNU's private vnode/mount layouts. The firmware target remains iPhoneOS.
 */
#ifndef B2L_HFS_COMPAT_H
#define B2L_HFS_COMPAT_H
#undef BSD_KERNEL_PRIVATE
#undef XNU_KERNEL_PRIVATE
#undef CONFIG_PROTECT
#undef CONFIG_SEARCHFS
#undef CONFIG_HFS_DIRLINK
#undef CONFIG_SECLUDED_RENAME
#undef CONFIG_MACF
#undef CONFIG_HFS_RBTREE
#undef QUOTA
#undef NAMEDSTREAMS
#undef FIFO
#undef HFS_COMPRESSION
#include "upstream/core/kext-config.h"
/* This root is deliberately unencrypted; no key service is emulated. */
#undef CONFIG_PROTECT
#define CONFIG_PROTECT 0
#define OSKextGetCurrentLoadTag b2l_hfs_current_load_tag
#endif
