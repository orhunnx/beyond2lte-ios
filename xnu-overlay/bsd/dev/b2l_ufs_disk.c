/* SPDX-License-Identifier: MIT
 * Minimal native BSD view over the inherited Samsung UFS transport.
 * Only the selected, validated HFS root view is writable; whole-LU and other views
 * remain read-only even when they overlap the authorized physical range.
 */
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>
#include <miscfs/devfs/devfs.h>
#include "b2l_ufs_disk.h"
#include "b2l_ufs_core.h"
#include "b2l_ufs_transport.h"
#include "b2l_ufs_aux.h"
#include <arm64/b2l_persistent_log.h>
#include "../kern/b2l_native_cache.h"
#include "../kern/b2l_cache_core.h"

struct b2l_ufs_view {
	uint64_t base, size;
	uint32_t sector_size;
	int ready, writable;
	void *block_node, *char_node;
};

static LCK_GRP_DECLARE(b2l_ufs_disk_group, "b2l-ufs-disk");
static LCK_MTX_DECLARE(b2l_ufs_disk_lock, &b2l_ufs_disk_group);
static struct b2l_ufs_reader b2l_reader;
static unsigned char b2l_read_scratch[B2L_UFS_MAX_BLOCK_SIZE];
static struct b2l_gpt b2l_partitions;
static struct b2l_ufs_view b2l_views[B2L_GPT_MAX_ENTRIES + 1];
static int b2l_block_major = -1, b2l_char_major = -1;
static unsigned b2l_root_index, b2l_aux_cache_index;
static uint64_t b2l_aux_cache_size;

static struct b2l_ufs_view *
b2l_view(dev_t dev)
{
	unsigned n = (unsigned)minor(dev);
	return n <= B2L_GPT_MAX_ENTRIES && b2l_views[n].ready ? &b2l_views[n] : NULL;
}

static int
b2l_open(dev_t dev, int flags, __unused int type, __unused struct proc *p)
{
	if (!b2l_view(dev)) return ENXIO;
	return (flags & FWRITE) && !b2l_view(dev)->writable ? EROFS : 0;
}

static int
b2l_close(__unused dev_t dev, __unused int flags, __unused int type,
    __unused struct proc *p)
{
	return 0;
}

static void
b2l_strategy(struct buf *bp)
{
	struct b2l_ufs_view *v = b2l_view(buf_device(bp));
	uint32_t count = buf_count(bp);
	size_t done = 0;
	caddr_t data = NULL;
	int error = 0;
	buf_setresid(bp, count);
	if (!v) {
		error = ENXIO;
	} else if (!(buf_flags(bp) & B_READ) && !v->writable) {
		error = EROFS;
	} else if (buf_blkno(bp) < 0) {
		error = EINVAL;
	} else {
		lck_mtx_lock(&b2l_ufs_disk_lock);
		uint64_t block = (uint64_t)buf_blkno(bp);
		if (block > UINT64_MAX / v->sector_size) {
			error = EINVAL;
		} else if ((error = buf_map(bp, &data)) == 0) {
			if (buf_flags(bp) & B_READ)
				error = b2l_ufs_read_view(&b2l_reader, v->base, v->size,
				    block * v->sector_size, count, 0, data, &done);
			else
				error = b2l_ufs_write_view(&b2l_reader, v->base, v->size,
				    block * v->sector_size, count, data, &done);
			buf_unmap(bp);
		}
		lck_mtx_unlock(&b2l_ufs_disk_lock);
	}
	buf_setresid(bp, count - (uint32_t)done);
	if (error) buf_seterror(bp, error);
	buf_biodone(bp);
}

static int
b2l_read(dev_t dev, struct uio *uio, __unused int flags)
{
	struct b2l_ufs_view *v = b2l_view(dev);
	if (!v) return ENXIO;
	if (uio_rw(uio) != UIO_READ) return EROFS;
	if (uio_offset(uio) < 0 || uio_resid(uio) < 0) return EINVAL;
	unsigned char scratch[B2L_UFS_MAX_BLOCK_SIZE];
	int error = 0;
	while (uio_resid(uio) > 0) {
		size_t n = (size_t)uio_resid(uio) > sizeof(scratch) ?
		    sizeof(scratch) : (size_t)uio_resid(uio);
		size_t done = 0;
		lck_mtx_lock(&b2l_ufs_disk_lock);
		error = b2l_ufs_read_view(&b2l_reader, v->base, v->size,
		    (uint64_t)uio_offset(uio), n, 0, scratch, &done);
		lck_mtx_unlock(&b2l_ufs_disk_lock);
		/* Preserve completed bytes even if the next physical read failed. */
		if (done) {
			/* No disk mutex across copyout/user faults. */
			int move_error = uiomove((caddr_t)scratch, (int)done, uio);
			if (move_error) { error = move_error; break; }
		}
		if (error || !done) break;
	}
	return error;
}

static int
b2l_write(dev_t dev, struct uio *uio, __unused int flags)
{
	struct b2l_ufs_view *v = b2l_view(dev);
	if (!v) return ENXIO;
	if (!v->writable) return EROFS;
	if (!uio || uio_rw(uio) != UIO_WRITE || uio_offset(uio) < 0 ||
	    uio_resid(uio) < 0) return EINVAL;
	lck_mtx_lock(&b2l_ufs_disk_lock);
	int error = b2l_ufs_write_bounds(&b2l_reader, v->base, v->size,
	    (uint64_t)uio_offset(uio), (size_t)uio_resid(uio));
	lck_mtx_unlock(&b2l_ufs_disk_lock);
	if (error || !uio_resid(uio)) return error;
	struct uio *copy = uio_duplicate(uio);
	if (!copy) return ENOMEM;
	unsigned char scratch[B2L_UFS_MAX_BLOCK_SIZE];
	while (uio_resid(uio) > 0) {
		/* uio_update advances one iovec, not an arbitrary writev span. */
		uio_update(uio, 0);
		size_t current = (size_t)uio_curriovlen(uio);
		if (!current) { error = EINVAL; break; }
		size_t n = (size_t)uio_resid(uio) > sizeof(scratch) ? sizeof(scratch) : (size_t)uio_resid(uio);
		if (n > current) n = current;
		size_t done = 0;
		/* copyin and user faults must never run while holding the disk mutex. */
		error = uiomove((caddr_t)scratch, (int)n, copy);
		if (error) break;
		lck_mtx_lock(&b2l_ufs_disk_lock);
		error = b2l_ufs_write_view(&b2l_reader, v->base, v->size,
		    (uint64_t)uio_offset(uio), n, scratch, &done);
		lck_mtx_unlock(&b2l_ufs_disk_lock);
		if (done) uio_update(uio, done);
		if (error || !done) break;
	}
	uio_free(copy);
	return error;
}

static int
b2l_ioctl_common(dev_t dev, u_long cmd, caddr_t data, int is_char)
{
	struct b2l_ufs_view *v = b2l_view(dev);
	if (!v) return ENXIO;
	int error = 0;
	lck_mtx_lock(&b2l_ufs_disk_lock);
	/* The no-argument SYNCHRONIZECACHE form legitimately passes NULL. */
	if (cmd == DKIOCSYNCHRONIZE || cmd == DKIOCSYNCHRONIZECACHE) {
		error = v->writable ? b2l_ufs_transport_sync() : EROFS;
		lck_mtx_unlock(&b2l_ufs_disk_lock);
		return error;
	}
	if (!data) {
		lck_mtx_unlock(&b2l_ufs_disk_lock);
		return EINVAL;
	}
	switch (cmd) {
	case DKIOCGETBLOCKSIZE:
		*(uint32_t *)data = v->sector_size; break;
	case DKIOCGETPHYSICALBLOCKSIZE:
		/* Minimum native UFS I/O granule, not the NAND erase-page geometry. */
		*(uint32_t *)data = b2l_reader.block_size; break;
	case DKIOCGETBLOCKCOUNT:
		*(uint64_t *)data = v->size / v->sector_size; break;
	case DKIOCSETBLOCKSIZE:
		if (is_char) error = ENODEV;
		else if (*(uint32_t *)data != 512 && *(uint32_t *)data != b2l_reader.block_size)
			error = EINVAL;
		else v->sector_size = *(uint32_t *)data;
		break;
	case DKIOCISWRITABLE:
		*(uint32_t *)data = v->writable ? 1 : 0; break;
	case DKIOCISSOLIDSTATE:
		*(uint32_t *)data = 1; break;
	case DKIOCGETBASE:
		*(uint64_t *)data = v->base; break;
	case DKIOCGETFEATURES:
		*(uint32_t *)data = 0; break;
	case DKIOCGETMAXBLOCKCOUNTREAD:
		*(uint64_t *)data = B2L_UFS_MAX_READ_BYTES / v->sector_size; break;
	case DKIOCGETMAXBYTECOUNTREAD:
		*(uint64_t *)data = B2L_UFS_MAX_READ_BYTES; break;
	case DKIOCGETMAXSEGMENTCOUNTREAD:
		*(uint64_t *)data = 1; break;
	case DKIOCGETMAXBLOCKCOUNTWRITE:
		*(uint64_t *)data = v->writable ? B2L_UFS_MAX_IO_BYTES / v->sector_size : 0; break;
	case DKIOCGETMAXBYTECOUNTWRITE:
		*(uint64_t *)data = v->writable ? B2L_UFS_MAX_IO_BYTES : 0; break;
	case DKIOCGETMAXSEGMENTCOUNTWRITE:
		*(uint64_t *)data = v->writable ? 1 : 0; break;
	default:
		/* Unknown controls include all media modification operations. */
		error = ENOTTY; break;
	}
	lck_mtx_unlock(&b2l_ufs_disk_lock);
	return error;
}

static int
b2l_bioctl(dev_t dev, u_long cmd, caddr_t data, __unused int flags,
    __unused struct proc *p)
{
	return b2l_ioctl_common(dev, cmd, data, 0);
}

static int
b2l_cioctl(dev_t dev, u_long cmd, caddr_t data, __unused int flags,
    __unused struct proc *p)
{
	return b2l_ioctl_common(dev, cmd, data, 1);
}

static const struct bdevsw b2l_bdevsw = {
	.d_open = b2l_open, .d_close = b2l_close, .d_strategy = b2l_strategy,
	.d_ioctl = b2l_bioctl, .d_dump = eno_dump, .d_psize = eno_psize,
	.d_type = D_DISK,
};
static const struct cdevsw b2l_cdevsw = {
	.d_open = b2l_open, .d_close = b2l_close, .d_read = b2l_read,
	.d_write = b2l_write, .d_ioctl = b2l_cioctl, .d_stop = eno_stop,
	.d_reset = eno_reset, .d_ttys = NULL, .d_select = eno_select,
	.d_mmap = eno_mmap, .d_strategy = eno_strat, .d_reserved_1 = eno_getc,
	.d_reserved_2 = eno_putc, .d_type = D_DISK,
};

static int
b2l_publish(unsigned index, uint64_t base, uint64_t size)
{
	struct b2l_ufs_view *v = &b2l_views[index];
	if (index && index == b2l_aux_cache_index) size = b2l_aux_cache_size;
	v->base = base; v->size = size; v->sector_size = 512;
	v->writable = index && index == b2l_root_index;
	if (!index) {
		v->block_node = devfs_make_node(makedev(b2l_block_major, index), DEVFS_BLOCK,
		    UID_ROOT, GID_OPERATOR, 0400, "b2lufs0");
		v->char_node = devfs_make_node(makedev(b2l_char_major, index), DEVFS_CHAR,
		    UID_ROOT, GID_OPERATOR, 0400, "rb2lufs0");
	} else {
		v->block_node = devfs_make_node(makedev(b2l_block_major, index), DEVFS_BLOCK,
		    UID_ROOT, GID_OPERATOR, v->writable ? 0600 : 0400, "b2lufs0p%u", index);
		v->char_node = devfs_make_node(makedev(b2l_char_major, index), DEVFS_CHAR,
		    UID_ROOT, GID_OPERATOR, v->writable ? 0600 : 0400, "rb2lufs0p%u", index);
	}
	if (!v->block_node || !v->char_node) {
		if (v->block_node) devfs_remove(v->block_node);
		if (v->char_node) devfs_remove(v->char_node);
		memset(v, 0, sizeof(*v));
		return ENOMEM;
	}
	v->ready = 1;
	return 0;
}

static void
b2l_probe_selected(void)
{
	const struct b2l_gpt_partition *selected = NULL;
	unsigned matches = 0;
	int container = 0;
	b2l_root_index = 0;
	b2l_reader.writable_base = b2l_reader.writable_size = 0;
	char root_arg[64] = { 0 };
	(void)PE_parse_boot_argn("rd", root_arg, sizeof(root_arg));
	root_arg[sizeof(root_arg) - 1] = '\0';
	const int userdata = !strcmp(root_arg, "b2l-userdata");
	const char *root_label = userdata ? "USERDATA" : "SYSTEM";
	/* USERDATA is explicit and never falls back to SYSTEM or super. */
	for (unsigned pass = 0; pass < (userdata ? 1U : 2U) && !matches; pass++) {
		for (unsigned i = 0; i < b2l_partitions.partition_count; i++) {
			const struct b2l_gpt_partition *p = &b2l_partitions.partitions[i];
			if (b2l_gpt_label_equal(p->name, userdata ? "userdata" : (pass ? "super" : "SYSTEM"))) {
				selected = p; matches++;
			}
		}
	}
	if (matches != 1) {
		printf("B2L UFS: %s%s probe unavailable (matches=%u)\n",
		    root_label, userdata ? "" : "/super", matches);
		return;
	}
	unsigned char signature[4096];
	size_t done = 0;
	uint64_t base = selected->first_lba * b2l_reader.block_size;
	uint64_t size = selected->block_count * b2l_reader.block_size;
	int error = b2l_ufs_read_view(&b2l_reader, base, size, 0,
	    sizeof(signature), 0, signature, &done);
	if (error || done != sizeof(signature)) {
		printf("B2L UFS: partition probe read failed error=%d\n", error ? error : EIO);
		return;
	}
	const char *kind = "unrecognized";
	if (signature[1024] == 'H' && (signature[1025] == '+' || signature[1025] == 'X')) {
		kind = "HFS+/HFSX";
		const unsigned char *vh = signature + 1024;
		unsigned version = (unsigned)vh[2] << 8 | vh[3];
		uint32_t block_size = (uint32_t)vh[40] << 24 | (uint32_t)vh[41] << 16 |
		    (uint32_t)vh[42] << 8 | vh[43];
		uint32_t blocks = (uint32_t)vh[44] << 24 | (uint32_t)vh[45] << 16 |
		    (uint32_t)vh[46] << 8 | vh[47];
		uint32_t free_blocks = (uint32_t)vh[48] << 24 | (uint32_t)vh[49] << 16 |
		    (uint32_t)vh[50] << 8 | vh[51];
		if (((vh[1] == '+' && version == 4) || (vh[1] == 'X' && version == 5)) &&
		    block_size >= 512 && !(block_size & (block_size - 1)) && blocks >= 2 &&
		    blocks <= size / block_size && free_blocks <= blocks && b2l_reader.write) {
			b2l_root_index = selected->index;
			b2l_reader.writable_base = base;
			b2l_reader.writable_size = size;
		} else {
			printf("B2L UFS: HFS %s write authorization denied: invalid volume geometry\n", root_label);
		}
	} else if (signature[1080] == 0x53 && signature[1081] == 0xef) kind = "ext4-family";
	else {
		unsigned char magic[4];
		error = b2l_ufs_read_view(&b2l_reader, base, size, 4096, 4, 0, magic, &done);
		if (!error && done == 4 && magic[0] == 0x67 && magic[1] == 0x44 &&
		    magic[2] == 0x6c && magic[3] == 0x61) {
			kind = "Android LP geometry";
			container = 1;
		}
	}
	printf("B2L UFS: GPT %s p%u first=%llu blocks=%llu %s signature=%s %s\n",
	    selected->name, selected->index, selected->first_lba, selected->block_count,
	    container ? "container" : "partition", kind,
	    b2l_root_index ? (userdata ? "USERDATA read-write authorized" : "SYSTEM read-write authorized") : "read-only");
}

int
b2l_ufs_root_device(dev_t *dev)
{
	if (!dev) return EINVAL;
	int error = ENXIO;
	lck_mtx_lock(&b2l_ufs_disk_lock);
	if (b2l_block_major >= 0 && b2l_root_index &&
	    b2l_views[b2l_root_index].ready && b2l_views[b2l_root_index].writable) {
		*dev = makedev(b2l_block_major, b2l_root_index);
		error = 0;
	}
	lck_mtx_unlock(&b2l_ufs_disk_lock);
	return error;
}

/* The auxiliary logger has its own exact tail view. It never changes the BSD
 * root/whole-disk write authorization or exposes a writable raw-tail node. */
static struct b2l_ufs_reader b2l_aux_log_reader;
static int b2l_aux_log_read(void *context,uint64_t off,void*out,size_t count)
{
 struct b2l_ufs_reader*r=context;size_t done=0;
 if(off>r->writable_size||count>r->writable_size-off)return EINVAL;
 lck_mtx_lock(&b2l_ufs_disk_lock);
 int error=b2l_ufs_read_view(r,r->writable_base,r->writable_size,off,count,0,out,&done);
 lck_mtx_unlock(&b2l_ufs_disk_lock);
 return error?error:(done==count?0:EIO);
}
static int b2l_aux_log_write(void *context,uint64_t off,const void*in,size_t count)
{
 struct b2l_ufs_reader*r=context;size_t done=0;
 lck_mtx_lock(&b2l_ufs_disk_lock);
 int error=b2l_ufs_write_view(r,r->writable_base,r->writable_size,off,count,in,&done);
 lck_mtx_unlock(&b2l_ufs_disk_lock);
 return error?error:(done==count?0:EIO);
}
static int b2l_aux_log_sync(void *context __unused)
{
 lck_mtx_lock(&b2l_ufs_disk_lock);int error=b2l_ufs_transport_sync();
 lck_mtx_unlock(&b2l_ufs_disk_lock);return error;
}
static void b2l_start_system_log(void)
{
 int enabled=0;
 if(!PE_parse_boot_argn("b2l_system_log",&enabled,sizeof(enabled))||enabled!=1)return;
 struct b2l_ufs_aux_layout layout;uint64_t capacity;unsigned char first[512],last[512];size_t done=0;
 int error=b2l_ufs_capacity(&b2l_reader,&capacity);
 if(!error)error=b2l_ufs_aux_layout(&b2l_partitions,b2l_root_index,b2l_reader.block_size,capacity,&layout);
 if(!error){
  lck_mtx_lock(&b2l_ufs_disk_lock);
  error=b2l_ufs_read_view(&b2l_reader,layout.partition_base,layout.hfs_size,1024,sizeof(first),0,first,&done);
  if(!error&&done!=sizeof(first))error=EIO;
  if(!error)error=b2l_ufs_read_view(&b2l_reader,layout.partition_base,layout.hfs_size,layout.hfs_size-1024,sizeof(last),0,last,&done);
  if(!error&&done!=sizeof(last))error=EIO;
  lck_mtx_unlock(&b2l_ufs_disk_lock);
 }
 if(!error)error=b2l_ufs_aux_hfs(first,last,layout.hfs_size);
 if(error){printf("B2L LOG: SYSTEM auxiliary layout rejected error=%d\n",error);return;}
 b2l_aux_log_reader=b2l_reader;
 b2l_aux_log_reader.writable_base=layout.log_base;
 b2l_aux_log_reader.writable_size=layout.log_size;
 const struct b2l_log_sink sink={b2l_aux_log_read,b2l_aux_log_write,b2l_aux_log_sync,&b2l_aux_log_reader,layout.log_size};
 error=b2l_log_start(&sink);
 if(error)printf("B2L LOG: provisioned region rejected error=%d\n",error);
 else {
  b2l_aux_cache_index=layout.index;b2l_aux_cache_size=layout.hfs_size;
  printf("B2L LOG: SYSTEM p%u persistent console enabled; HFS cache bytes=%llu; log bytes=%llu\n",layout.index,layout.hfs_size,layout.log_size);
 }
}

/* Private cache writer: only the validated, fully allocated data fork of the
 * provisioned cache file. SYSTEM BSD devices stay read-only. */
static struct b2l_ufs_reader b2l_cache_reader;
static struct b2l_hfs b2l_cache_hfs;
static struct b2l_cache_fork b2l_cache_fork;
static void b2l_start_native_cache(void)
{
 int enabled=0;if(!PE_parse_boot_argn("b2l_native_cache",&enabled,sizeof(enabled))||enabled!=1)return;
 struct b2l_ufs_aux_layout layout;uint64_t capacity;int error=b2l_ufs_capacity(&b2l_reader,&capacity);
 if(!error)error=b2l_ufs_aux_layout(&b2l_partitions,b2l_root_index,b2l_reader.block_size,capacity,&layout);
 if(error)return;
 b2l_cache_reader=b2l_reader;b2l_cache_reader.writable_base=layout.partition_base;b2l_cache_reader.writable_size=layout.hfs_size;
 struct b2l_io disk={b2l_aux_log_read,b2l_aux_log_write,b2l_aux_log_sync,&b2l_cache_reader,layout.hfs_size},sink;
 struct b2l_hfs_file file;
 error=b2l_hfs_open(&b2l_cache_hfs,&disk);
 if(!error&&b2l_cache_hfs.bytes!=layout.hfs_size)error=EINVAL;
 if(!error)error=b2l_hfs_find(&b2l_cache_hfs,"b2l-cache.bin",&file);
 if(!error)error=b2l_hfs_cache_view(&b2l_cache_hfs,&file,&b2l_cache_fork,&sink);
 uint8_t vh[512],id[12];size_t done=0;
 if(!error){lck_mtx_lock(&b2l_ufs_disk_lock);
  error=b2l_ufs_read_view(&b2l_reader,b2l_reader.writable_base,b2l_reader.writable_size,1024,sizeof(vh),0,vh,&done);
  lck_mtx_unlock(&b2l_ufs_disk_lock);if(!error&&done!=sizeof(vh))error=EIO;
 }
 if(error){printf("B2L NCACHE: provisioned file unavailable error=%d; emulation retained\n",error);return;}
 memcpy(id,vh+104,8);memcpy(id+8,vh+16,4);
 b2l_native_cache_storage(&sink,id);
 printf("B2L NCACHE: bounded SYSTEM file storage ready (%llu MiB); log tail unchanged\n",file.data.size/(1024*1024));
}

void
b2l_ufs_disk_init(void)
{
	struct b2l_ufs_geometry geometry;
	int error = b2l_ufs_transport_init(&geometry);
	if (error) {
		printf("B2L UFS: transport unavailable error=%d; root search unchanged\n", error);
		return;
	}
	b2l_reader.block_count = geometry.block_count;
	b2l_reader.block_size = geometry.block_size;
	b2l_reader.read = b2l_ufs_transport_read;
	b2l_reader.scratch = b2l_read_scratch;
	b2l_reader.write = b2l_ufs_transport_write;
	uint64_t capacity;
	if ((error = b2l_ufs_capacity(&b2l_reader, &capacity))) {
		printf("B2L UFS: invalid geometry error=%d\n", error); return;
	}
	printf("B2L UFS: geometry blocks=%llu block-size=%u; read-only\n",
	    geometry.block_count, geometry.block_size);
	b2l_block_major = bdevsw_add(-1, &b2l_bdevsw);
	if (b2l_block_major < 0) {
		printf("B2L UFS: no block device slot\n"); return;
	}
	b2l_char_major = cdevsw_add_with_bdev(-1, &b2l_cdevsw, b2l_block_major);
	if (b2l_char_major < 0) {
		bdevsw_remove(b2l_block_major, &b2l_bdevsw);
		printf("B2L UFS: no character device slot\n"); return;
	}
	if ((error = b2l_publish(0, 0, capacity))) {
		cdevsw_remove(b2l_char_major, &b2l_cdevsw);
		bdevsw_remove(b2l_block_major, &b2l_bdevsw);
		printf("B2L UFS: device node failed error=%d\n", error); return;
	}
	printf("B2L UFS: /dev/b2lufs0 and /dev/rb2lufs0 ready (read-only)\n");
	lck_mtx_lock(&b2l_ufs_disk_lock);
	error = b2l_gpt_read(&b2l_reader, &b2l_partitions);
	lck_mtx_unlock(&b2l_ufs_disk_lock);
	if (error) {
		printf("B2L UFS: GPT validation failed error=%d; no partition nodes\n", error);
		return;
	}
	printf("B2L UFS: primary+backup GPT CRC verified entries=%u partitions=%u\n",
	    b2l_partitions.entry_count, b2l_partitions.partition_count);
	lck_mtx_lock(&b2l_ufs_disk_lock);
	b2l_probe_selected();
	lck_mtx_unlock(&b2l_ufs_disk_lock);
	b2l_start_system_log();
	b2l_start_native_cache();
	for (unsigned i = 0; i < b2l_partitions.partition_count; i++) {
		const struct b2l_gpt_partition *p = &b2l_partitions.partitions[i];
		error = b2l_publish(p->index, p->first_lba * geometry.block_size,
		    p->block_count * geometry.block_size);
		if (error) printf("B2L UFS: partition node p%u failed error=%d\n", p->index, error);
	}
	printf("B2L UFS: native BSD devices ready; only the selected validated HFS partition can be a writable root\n");
}
