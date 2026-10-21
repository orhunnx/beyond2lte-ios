/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>

extern int b2l_ufs_root_device(dev_t *);

/* Development opt-in, not a signature verification or authenticated-root ioctl. */
int
b2l_hfs_root_unsealed(dev_t root, vnode_t vnode)
{
	dev_t system;
	int enabled = 0;
	if (!vnode || !PE_parse_boot_argn("b2l_unsealed_root", &enabled, sizeof(enabled)) ||
	    enabled != 1 || b2l_ufs_root_device(&system) || system != root) {
		return 0;
	}
	mount_t mount = vnode_mount(vnode);
	return mount && !strcmp(vfs_statfs(mount)->f_fstypename, "hfs");
}
