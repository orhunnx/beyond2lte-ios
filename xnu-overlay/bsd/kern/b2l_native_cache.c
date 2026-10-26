/* SPDX-License-Identifier: BSD-2-Clause
 * Automatic observed-instruction cache. Trap path: fixed queue, no filesystem
 * access. Worker: resolve referenced VM map/pager to vnode/file offset, persist
 * original+native page in SYSTEM. Next boot: apply through normal HFS VFS only
 * after root journal replay and before the first userspace instruction.
 */
#include "b2l_native_cache.h"
#include "b2l_cache_core.h"
#include "b2l_nc_store.h"
#include "b2l_nc_undo.h"
#include <sys/fcntl.h>
#include <kern/kalloc.h>
#include "b2l_cache_exclusion.h"
#include <arm64/b2l_arm64e_compat.h>
#include <arm64/b2l_early.h>
#include <machine/machine_routines.h>
#include <kern/thread.h>
#include <mach/thread_info.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/cpu_data.h>
#include <vm/vm_map.h>
#include <vm/vm_map_internal.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <pexpert/arm64/boot.h>
#include <sys/errno.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/vnode_internal.h>
#include <sys/vnode_if.h>
#include <sys/uio.h>
#include <sys/kauth.h>
#include <sys/proc.h>
#include <sys/systm.h>
extern int b2l_cache_resolve(vm_map_t,uint64_t,struct vnode**,uint64_t*);
extern vnode_t rootvnode;
#define QSIZE 128u
#define HINTS 32768u
#define CACHE_WORKERS 4u
static struct b2l_io cache_io;
static uint8_t root_identity[12];
#define HOT_CELLS 64u
static struct b2l_nc_store store;
static uint8_t hot_cells[HOT_CELLS*B2L_NC_CELL];
static uint32_t hot_tags[HOT_CELLS];
#define SLOT_COUNT store.slots
static uint32_t storage_ready,ready,queue_lock,pending,active,peak_active;
static unsigned worker_count;
static LCK_GRP_DECLARE(cache_lck_grp,"B2L native cache");
static LCK_MTX_DECLARE(commit_lock,&cache_lck_grp);
struct request {vm_map_t map;uint64_t pc;uint32_t instruction;};
static struct request queue[QSIZE];static unsigned head,tail;
static uint64_t hints[HINTS];
static uint64_t queued,dropped,learned,applied,already,skipped,failures;
/* Pre-init buffers have no concurrent users. Each runtime worker owns its I/O
 * scratch; only the slot index, hot cells and generation commit are shared. */
static uint8_t verify[B2L_NC_COPY],page[4096];
struct cache_worker {
 uint8_t record[B2L_NC_COPY],verify[B2L_NC_COPY],page[4096];
 uint64_t processed;uint32_t cpu_mask;unsigned id;
};
static struct cache_worker workers[CACHE_WORKERS];
static struct b2l_nc_image integrity_image;
struct integrity_file {uint8_t uuid[16];uint64_t size;int result;struct b2l_nc_exclusion range;};
static struct integrity_file integrity_files[128];static unsigned integrity_count;
static uint8_t target_page[4096];
static uint64_t integrity_excluded,integrity_restored;
static struct b2l_nc_undo undo;
static vnode_t undo_vp;
static uint8_t undo_record[B2L_NC_COPY],reclaim_zero[B2L_NC_COPY];
static uint64_t reclaimed,undo_restored;
static int reclaim_disabled;


static int enabled(void){int v=0;return b2l_arm64e_compat_enabled()&&PE_parse_boot_argn("b2l_native_cache",&v,sizeof(v))&&v==1;}
void b2l_native_cache_storage(const struct b2l_io*io,const uint8_t id[12]){
 if(!io||!io->read||!io->write||!io->sync||io->bytes<B2L_NC_HEADER+B2L_NC_CELL)return;
 cache_io=*io;memcpy(root_identity,id,12);storage_ready=1;
}
static int rd(vnode_t vp,uint64_t off,void*out,unsigned n){int resid=0;
 if(off>INT64_MAX||n>INT_MAX)return EINVAL;
 int e=vn_rdwr(UIO_READ,vp,(caddr_t)out,(int)n,(off_t)off,UIO_SYSSPACE,0,kauth_cred_get(),&resid,current_proc());return e?e:(resid?EIO:0);}
static int integrity_read(void*vp,uint64_t off,void*out,size_t n){
 if(n>INT_MAX)return EINVAL;return rd((vnode_t)vp,off,out,(unsigned)n);
}
static int integrity_open(void){
 vnode_t vp=NULL;struct vnode_attr va;
 if(vnode_lookup("/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e",0,&vp,vfs_context_current())||!vp)return -1;
 VATTR_INIT(&va);VATTR_WANTED(&va,va_data_size);
 int e=-1;
 if(vnode_vtype(vp)==VREG&&vnode_mount(vp)==vnode_mount(rootvnode)&&
    !vnode_getattr(vp,&va,vfs_context_current())&&VATTR_IS_SUPPORTED(&va,va_data_size)&&va.va_data_size>0){
  struct b2l_io io={.read=integrity_read,.ctx=vp,.bytes=(uint64_t)va.va_data_size};
  e=b2l_ximage(&io,&integrity_image);
 }
 vnode_put(vp);return e;
}
static struct integrity_file*integrity_file(vnode_t vp,uint64_t size,const uint8_t uuid[16]){
 for(unsigned i=0;i<integrity_count;i++)if(integrity_files[i].size==size&&!memcmp(integrity_files[i].uuid,uuid,16))return &integrity_files[i];
 if(integrity_count==128)return NULL;
 struct integrity_file*f=&integrity_files[integrity_count++];memcpy(f->uuid,uuid,16);f->size=size;
 struct b2l_io io={.read=integrity_read,.ctx=vp,.bytes=size};
 f->result=b2l_xresolve(&io,&integrity_image,&f->range);return f;
}
static int identity(vnode_t vp,uint64_t*ino,uint64_t*bytes,uint8_t uuid[16],unsigned*kind,uint8_t*verify){
 struct vnode_attr va;VATTR_INIT(&va);VATTR_WANTED(&va,va_fileid);VATTR_WANTED(&va,va_data_size);
 if(!vp||vnode_vtype(vp)!=VREG||vnode_mount(vp)!=vnode_mount(rootvnode)||vnode_getattr(vp,&va,vfs_context_current())||
    !VATTR_IS_SUPPORTED(&va,va_fileid)||!VATTR_IS_SUPPORTED(&va,va_data_size)||!va.va_fileid||va.va_data_size<4096)return -1;
 *ino=va.va_fileid;*bytes=va.va_data_size;
 if(rd(vp,0,verify,4096))return -1;
 if(!memcmp(verify,"dyld_v1",7)){memcpy(uuid,verify+88,16);*kind=2;return 0;}
 if(b2l_nc_u32(verify)!=0xfeedfacfu||b2l_nc_u32(verify+4)!=0x100000cu)return -1;
 unsigned cmds=b2l_nc_u32(verify+16),bytes_cmds=b2l_nc_u32(verify+20);uint64_t at=32;
 if(cmds>2048||bytes_cmds>65536||bytes_cmds>*bytes-32)return -1;
 for(unsigned i=0;i<cmds;i++){
  uint8_t lc[24];if(at>32+bytes_cmds||32+bytes_cmds-at<8||rd(vp,at,lc,8))return -1;
  unsigned cmd=b2l_nc_u32(lc),n=b2l_nc_u32(lc+4);if(n<8||(n&7)||n>32+bytes_cmds-at)return -1;
  if(cmd==0x1b){if(n!=24||rd(vp,at,lc,24))return -1;memcpy(uuid,lc+8,16);*kind=1;return 0;}at+=n;
 }
 return -1;
}
static int same_file(vnode_t vp,const uint8_t*r){uint64_t ino,size;uint8_t uuid[16];unsigned kind;
 return identity(vp,&ino,&size,uuid,&kind,verify)||ino!=b2l_nc_u64(r+16)||size!=b2l_nc_u64(r+24)||
 kind!=b2l_nc_u32(r+56)||memcmp(uuid,r+40,16)?-1:0;}
/* The journaled root owns this ordinary, root-only undo archive. Recovery
 * never mounts or writes it. No runtime worker modifies it or source code. */
static int undo_read(void*vp,uint64_t at,void*out,size_t n){return n>INT_MAX?EINVAL:rd(vp,at,out,(unsigned)n);}
static int undo_write(void*vp,uint64_t at,const void*in,size_t n){
 if(at>INT64_MAX||n>INT_MAX||n>(uint64_t)INT64_MAX-at)return EINVAL;
 int resid=0;int e=vn_rdwr(UIO_WRITE,vp,(caddr_t)in,(int)n,(off_t)at,UIO_SYSSPACE,IO_SYNC,kauth_cred_get(),&resid,current_proc());
 return e?e:(resid?EIO:0);
}
static int undo_sync(void*vp){return VNOP_FSYNC(vp,MNT_WAIT,vfs_context_current());}
static int undo_open(int create){
 struct vnode_attr va;vfs_context_t ctx=vfs_context_current();
 /* vnode_open maps NOFOLLOW_ANY to O_NOFOLLOW_ANY. Native vn_open_auth
  * rejects combining it with O_NOFOLLOW; ANY already covers the final link. */
 int e=vnode_open("/private/var/db/.b2l-native-undo-v1",FREAD|FWRITE|(create?O_CREAT:0),0600,
     VNODE_LOOKUP_NOFOLLOW_ANY,&undo_vp,ctx);
 if(e)return e;
 VATTR_INIT(&va);VATTR_WANTED(&va,va_data_size);VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_nlink);VATTR_WANTED(&va,va_mode);
 if(vnode_vtype(undo_vp)!=VREG||vnode_mount(undo_vp)!=vnode_mount(rootvnode)||vnode_getattr(undo_vp,&va,ctx)||
    !VATTR_IS_SUPPORTED(&va,va_uid)||!VATTR_IS_SUPPORTED(&va,va_nlink)||!VATTR_IS_SUPPORTED(&va,va_mode)||
    !VATTR_IS_SUPPORTED(&va,va_data_size)||va.va_uid||va.va_nlink!=1||(va.va_mode&0777)!=0600||va.va_data_size<0)return EINVAL;
 struct b2l_io io={undo_read,undo_write,undo_sync,undo_vp,(uint64_t)va.va_data_size};
 if(b2l_ncu_open(&undo,&io,root_identity,create,verify))return EIO;
 vnode_t parent=NULL;
 if(vnode_lookup("/private/var/db",VNODE_LOOKUP_NOFOLLOW_ANY,&parent,ctx))return EIO;
 e=vnode_mount(parent)==vnode_mount(rootvnode)?VNOP_FSYNC(parent,MNT_WAIT,ctx):EXDEV;vnode_put(parent);
 if(e)return e;
 /* Existing records are checked when restoring. Normal boots validate the
  * archive headers here and each newly appended record before slot erasure. */
 return 0;
}
static void undo_close(void){
 if(undo_vp){vnode_close(undo_vp,FREAD|FWRITE,vfs_context_current());undo_vp=NULL;}
}
/* Return 1 only for a matching file and fully synced/read-back target. */
static int apply_record(const uint8_t*r,int restore,const uint8_t**target_out){
 vnode_t vp=NULL;*target_out=NULL;
 if(VFS_VGET(vnode_mount(rootvnode),(ino64_t)b2l_nc_u64(r+16),&vp,vfs_context_current())||!vp){skipped++;return 0;}
 unsigned len=b2l_nc_u32(r+60);uint64_t off=b2l_nc_u64(r+32);
 if(same_file(vp,r)||rd(vp,off,page,len)||b2l_nc_page_state(r,page,len)<0){skipped++;vnode_put(vp);return 0;}
 const uint8_t*target=r+B2L_NC_DATA+(restore==1?0:4096);
 if(restore!=1&&b2l_nc_u32(r+56)==2){
  struct integrity_file*f=integrity_file(vp,b2l_nc_u64(r+24),r+40);
  if(!f||f->result<0){vnode_put(vp);panic("B2L native cache: corecrypto section metadata unsafe");}
  if(f->result==1){
   integrity_excluded+=b2l_xfilter(r,&f->range,target_page);target=target_page;
   uint64_t start=off>f->range.offset?off:f->range.offset;
   uint64_t end=off+len<f->range.offset+f->range.bytes?off+len:f->range.offset+f->range.bytes;
   for(uint64_t at=start;at<end;at+=4)if(memcmp(page+at-off,target+at-off,4))integrity_restored++;
  }
 }
 int e=0,resid=0,changed=memcmp(page,target,len)!=0;
 if(changed)e=vn_rdwr(UIO_WRITE,vp,(caddr_t)target,(int)len,(off_t)off,UIO_SYSSPACE,IO_SYNC,kauth_cred_get(),&resid,current_proc());
 if(!e&&!resid)e=VNOP_FSYNC(vp,MNT_WAIT,vfs_context_current());
 if(e||resid||rd(vp,off,page,len)||memcmp(page,target,len)){
  failures++;vnode_put(vp);panic("B2L native cache apply failed before userspace");
 }
 if(changed)applied++;else already++;
 *target_out=target;vnode_put(vp);return 1;
}
static void reclaim_record(unsigned index,const uint8_t*r,const uint8_t*target){
 /* A filtered record archives exactly the bytes actually applied, including
  * measured-text exclusions. No-op (restored original) pages need no new undo. */
 if(memcmp(r+B2L_NC_DATA,target,4096)){
  memcpy(undo_record,r,B2L_NC_COPY);memcpy(undo_record+B2L_NC_DATA+4096,target,4096);b2l_nc_seal(undo_record);
  if(b2l_ncu_append(&undo,undo_record,verify))goto failed;
 }
 if(b2l_ncs_reclaim(&store,index,reclaim_zero,verify))goto failed;
 reclaimed++;return;
failed:
 failures++;reclaim_disabled=1;
 printf("B2L NCACHE: undo/reclaim I/O failed; no further slot reuse this boot\n");
}
static int persist(unsigned index,uint8_t*r,const uint8_t*old,uint8_t*verify){
 return b2l_ncs_put(&store,index,r,old,verify);
}
static void learn(struct request*q,struct cache_worker*w){
 vnode_t vp=NULL;uint64_t off=0,ino=0,size=0;uint8_t uuid[16];unsigned kind=0;uint32_t native;
 uint8_t*record=w->record,*page=w->page;
 if(!b2l_nc_native(q->instruction,&native)||b2l_cache_resolve(q->map,q->pc,&vp,&off)||!vp){__atomic_fetch_add(&skipped,1,__ATOMIC_RELAXED);return;}
 /* VM lookup, identity and source reads run concurrently without the commit
  * mutex. Original files are changed only before userspace, never by workers. */
 if(identity(vp,&ino,&size,uuid,&kind,w->verify)||off>size||size-off<4||(off&3)){__atomic_fetch_add(&skipped,1,__ATOMIC_RELAXED);goto out;}
 uint64_t po=off&~4095ULL;unsigned len=size-po>=4096?4096:(unsigned)(size-po);
 memset(page,0,4096);
 if(rd(vp,po,page,len)||b2l_nc_u32(page+(off-po))!=q->instruction){__atomic_fetch_add(&skipped,1,__ATOMIC_RELAXED);goto out;}
 lck_mtx_lock(&commit_lock);
 /* A previous failed write disables every in-flight request, not just the
  * worker that saw it. Re-read the latest generation under this same mutex. */
 if(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE))goto unlock;
 if(kind==2){
  struct integrity_file*f=integrity_file(vp,size,uuid);
  if(!f||f->result<0){__atomic_fetch_add(&skipped,1,__ATOMIC_RELAXED);goto unlock;}
  if(f->result==1&&off<f->range.offset+f->range.bytes&&f->range.offset<off+4){__atomic_fetch_add(&skipped,1,__ATOMIC_RELAXED);goto unlock;}
 }
 unsigned index=b2l_ncs_find(&store,ino,po);
 if(index==SLOT_COUNT)index=store.next_free;
 if(index==SLOT_COUNT){__atomic_fetch_add(&dropped,1,__ATOMIC_RELAXED);goto unlock;}
 const uint8_t*old=NULL;
 if(b2l_ncs_read(&store,index,&old)){
  __atomic_fetch_add(&failures,1,__ATOMIC_RELAXED);__atomic_store_n(&ready,0,__ATOMIC_RELEASE);goto unlock;
 }
 if(old){
  if(b2l_nc_u64(old+16)!=ino||b2l_nc_u64(old+24)!=size||memcmp(old+40,uuid,16)||b2l_nc_u32(old+56)!=kind||b2l_nc_page_state(old,page,len)<0){__atomic_fetch_add(&skipped,1,__ATOMIC_RELAXED);goto unlock;}
  memcpy(record,old,B2L_NC_COPY);
 }else{
  memset(record,0,B2L_NC_COPY);memcpy(record,"B2LNCREC",8);b2l_nc_p64(record+16,ino);b2l_nc_p64(record+24,size);b2l_nc_p64(record+32,po);
  memcpy(record+40,uuid,16);b2l_nc_p32(record+56,kind);b2l_nc_p32(record+60,len);
  memcpy(record+B2L_NC_DATA,page,4096);memcpy(record+B2L_NC_DATA+4096,page,4096);
 }
 unsigned at=(unsigned)(off-po);uint32_t original=b2l_nc_u32(record+B2L_NC_DATA+at),expected;
 if(!b2l_nc_native(original,&expected)||expected!=native){__atomic_fetch_add(&skipped,1,__ATOMIC_RELAXED);goto unlock;}
 if(b2l_nc_u32(record+B2L_NC_DATA+4096+at)==native)goto unlock;
 b2l_nc_p32(record+B2L_NC_DATA+4096+at,native);
 /* A/B write + flush + readback stays serialized through RAM publication;
  * workers cannot lose same-page updates or overwrite the last valid copy. */
 if(persist(index,record,old,w->verify)){__atomic_fetch_add(&failures,1,__ATOMIC_RELAXED);__atomic_store_n(&ready,0,__ATOMIC_RELEASE);}
 else __atomic_fetch_add(&learned,1,__ATOMIC_RELAXED);
 unlock:lck_mtx_unlock(&commit_lock);
 out:vnode_put(vp);
}
void b2l_native_cache_observe(uint64_t pc,uint32_t insn){
 uint32_t replacement;if(!__atomic_load_n(&ready,__ATOMIC_RELAXED)||!b2l_nc_native(insn,&replacement))return;
 /* Try-lock: the exception path never waits for the worker or storage. */
 disable_preemption();
 if(__atomic_exchange_n(&queue_lock,1,__ATOMIC_ACQUIRE)){enable_preemption();__atomic_fetch_add(&dropped,1,__ATOMIC_RELAXED);return;}
 unsigned notify=0;vm_map_t map=current_map();uint64_t tag=((uint64_t)(uintptr_t)map>>4)^pc^((uint64_t)insn<<17);if(!tag)tag=1;
 unsigned h=(unsigned)((tag^(tag>>19)^(tag>>37))&(HINTS-1));
 if(hints[h]!=tag){unsigned next=(head+1)%QSIZE;if(next!=tail){vm_map_reference(map);queue[head]=(struct request){map,pc,insn};head=next;hints[h]=tag;notify=1;__atomic_store_n(&pending,(head+QSIZE-tail)%QSIZE,__ATOMIC_RELEASE);__atomic_fetch_add(&queued,1,__ATOMIC_RELAXED);}else __atomic_fetch_add(&dropped,1,__ATOMIC_RELAXED);}
 __atomic_store_n(&queue_lock,0,__ATOMIC_RELEASE);enable_preemption();
 if(notify)thread_wakeup_one((event_t)&queue);
}
static int cache_dequeue(struct request*q){
 disable_preemption();
 if(__atomic_exchange_n(&queue_lock,1,__ATOMIC_ACQUIRE)){enable_preemption();return 0;}
 int found=head!=tail;if(found){*q=queue[tail];tail=(tail+1)%QSIZE;__atomic_store_n(&pending,(head+QSIZE-tail)%QSIZE,__ATOMIC_RELEASE);}
 __atomic_store_n(&queue_lock,0,__ATOMIC_RELEASE);enable_preemption();return found;
}
static void worker(void*arg,wait_result_t why __unused){
 /* Keep cache I/O in the existing dedicated storage group. The system
  * group also contains launchd/user services and must not be treated as
  * kernel-only for placement. Native join updates this current thread. */
 thread_group_join_io_storage();
 struct cache_worker*w=arg;uint64_t report_at;
 b2l_early_trace("B2L NCACHE: worker=%u joined IO-storage thread group (A75 preference)\n",w->id);
 nanoseconds_to_deadline(30*NSEC_PER_SEC,&report_at);
 for(;;){struct request q;unsigned budget=32;
  while(budget--&&cache_dequeue(&q)){
   unsigned n=__atomic_add_fetch(&active,1,__ATOMIC_RELAXED),peak=__atomic_load_n(&peak_active,__ATOMIC_RELAXED);
   while(n>peak&&!__atomic_compare_exchange_n(&peak_active,&peak,n,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){}
   unsigned cpu=(unsigned)cpu_number();if(cpu<32)__atomic_fetch_or(&w->cpu_mask,1u<<cpu,__ATOMIC_RELAXED);
   if(__atomic_load_n(&ready,__ATOMIC_ACQUIRE))learn(&q,w);
   vm_map_deallocate(q.map);__atomic_fetch_add(&w->processed,1,__ATOMIC_RELAXED);__atomic_fetch_sub(&active,1,__ATOMIC_RELAXED);
  }
  if(!w->id&&mach_absolute_time()>=report_at){
   uint32_t cpus=0;for(unsigned i=0;i<CACHE_WORKERS;i++)cpus|=__atomic_load_n(&workers[i].cpu_mask,__ATOMIC_RELAXED);
   b2l_early_trace("B2L NCACHE: queued=%llu learned=%llu applied=%llu skipped=%llu dropped=%llu errors=%llu pending=%u active=%u peak=%u workers=%u cpus=0x%x w0=%llu w1=%llu w2=%llu w3=%llu\n",
    __atomic_load_n(&queued,__ATOMIC_RELAXED),__atomic_load_n(&learned,__ATOMIC_RELAXED),applied,
    __atomic_load_n(&skipped,__ATOMIC_RELAXED),__atomic_load_n(&dropped,__ATOMIC_RELAXED),__atomic_load_n(&failures,__ATOMIC_RELAXED),
    __atomic_load_n(&pending,__ATOMIC_RELAXED),__atomic_load_n(&active,__ATOMIC_RELAXED),__atomic_load_n(&peak_active,__ATOMIC_RELAXED),
    __atomic_load_n(&worker_count,__ATOMIC_ACQUIRE),cpus,
    __atomic_load_n(&workers[0].processed,__ATOMIC_RELAXED),__atomic_load_n(&workers[1].processed,__ATOMIC_RELAXED),
    __atomic_load_n(&workers[2].processed,__ATOMIC_RELAXED),__atomic_load_n(&workers[3].processed,__ATOMIC_RELAXED));
   nanoseconds_to_deadline(30*NSEC_PER_SEC,&report_at);
  }
  /* Register the wait before rechecking published queue depth: an enqueue
   * either cancels this wait or is seen here. No one-second pause on backlog. */
  assert_wait_timeout((event_t)&queue,THREAD_UNINT,1,NSEC_PER_SEC);
  if(__atomic_load_n(&pending,__ATOMIC_ACQUIRE))clear_wait(current_thread(),THREAD_AWAKENED);
  thread_block(THREAD_CONTINUE_NULL);
 }
}
void b2l_native_cache_before_init(void){
 if(!enabled()||!storage_ready){printf("B2L NCACHE: unavailable; emulation retained\n");return;}
 DTEntry dt;const void*prop;unsigned n;uint64_t range[2];
 if(SecureDTLookupEntry(NULL,"/chosen",&dt)!=kSuccess||SecureDTGetProperty(dt,"b2l,native-cache",&prop,&n)!=kSuccess||n!=16)return;
 memcpy(range,prop,16);boot_args*args=(boot_args*)PE_state.bootArgs;
 uint64_t base=range[0];if(range[1]<B2L_NC_HEADER||(base&4095)||base<args->physBase||base>args->topOfKernelData||range[1]>args->topOfKernelData-base)return;
 const uint8_t*staged=(const uint8_t*)ml_static_ptovirt(base);
 if(!staged||b2l_ncs_open(&store,&cache_io,root_identity)||memcmp(staged,store.headers,B2L_NC_HEADER))return;
 /* Only an index and a bounded hot window occupy RAM, regardless of disk size.
  * Reject an unreasonable index allocation before any root-file write. */
 if(store.slots>0x20000000u)return;
 uint32_t buckets=1;while(buckets<(uint64_t)store.slots*2)buckets<<=1;
 size_t key_bytes=(size_t)store.slots*sizeof(*store.keys),bucket_bytes=(size_t)buckets*sizeof(uint32_t);
 if(key_bytes>128u*1024u*1024u||bucket_bytes>128u*1024u*1024u-key_bytes)return;
 store.keys=kalloc_data(key_bytes,Z_WAITOK|Z_ZERO);store.buckets=kalloc_data(bucket_bytes,Z_WAITOK|Z_ZERO);
 if(!store.keys||!store.buckets){
  if(store.keys)kfree_data(store.keys,key_bytes);if(store.buckets)kfree_data(store.buckets,bucket_bytes);return;
 }
 store.bucket_count=buckets;store.hot=hot_cells;store.tags=hot_tags;store.hot_count=HOT_CELLS;
 /* Read only the committed high-water range, not every preallocated empty cell.
  * Establish every key and reject duplicates before changing any root file. */
 for(unsigned i=0;i<store.used;i++){
  const uint8_t*r=NULL;
  if(b2l_ncs_read(&store,i,&r)||(r&&b2l_ncs_index(&store,i,r))){
   printf("B2L NCACHE: invalid/duplicate disk record; emulation retained\n");
   kfree_data(store.keys,key_bytes);kfree_data(store.buckets,bucket_bytes);return;
  }
 }
 printf("B2L NCACHE: disk=%llu MiB slots=%u used=%u hot=%u KiB index=%llu KiB; disk-backed records\n",
  cache_io.bytes/(1024*1024),store.slots,store.used,(unsigned)(sizeof(hot_cells)/1024),(uint64_t)(key_bytes+bucket_bytes)/1024);
 /* Resolve the measured module before applying any native bytes. Failure is
  * explicit: never guess an exclusion or continue into a known dirty module. */
 int restore=0;(void)PE_parse_boot_argn("b2l_cache_restore",&restore,sizeof(restore));
 if(restore!=1&&integrity_open())panic("B2L native cache: corecrypto image metadata unavailable");
 int recycle=1;(void)PE_parse_boot_argn("b2l_cache_reclaim",&recycle,sizeof(recycle));
 int archive_error=undo_open(restore!=1&&recycle==1);
 if(archive_error){
  undo_close();reclaim_disabled=1;
  if(restore==1&&archive_error!=ENOENT)panic("B2L native cache: undo archive invalid; restore incomplete");
  if(restore!=1)printf("B2L NCACHE: undo archive unavailable (%d); retaining SYSTEM records\n",archive_error);
 }
 if(b2l_nc_u32(store.headers+store.header_copy*4096+8)!=2)reclaim_disabled=1;
 for(unsigned i=0;i<store.used;i++){
  if(!store.keys[i].inode)continue;const uint8_t*r=NULL,*target=NULL;
  if(b2l_ncs_read(&store,i,&r)||!r)panic("B2L native cache: durable record read failed before apply");
  if(apply_record(r,restore,&target)&&restore!=1&&recycle==1&&!reclaim_disabled)
   reclaim_record(i,r,target);
 }
 if(restore==1&&undo_vp){
  /* Later records can start from an already translated page. Reverse append
   * order restores that chain all the way back to its original page. */
  for(uint64_t i=undo.count;i;i--){
   const uint8_t*target=NULL;
   if(b2l_ncu_read(&undo,i-1,undo_record))panic("B2L native cache: durable undo read failed");
   if(apply_record(undo_record,1,&target))undo_restored++;
  }
 }
 if(restore!=1&&reclaimed&&!reclaim_disabled&&b2l_ncs_trim(&store,undo.next,verify)){
  failures++;reclaim_disabled=1;printf("B2L NCACHE: scan-range trim failed; learning disabled this boot\n");
 }
 printf("B2L NCACHE: reclaimed=%llu live=%u free=%u scan=%u undo=%llu undo_restored=%llu reclaim_disabled=%d\n",
  reclaimed,store.live,store.slots-store.live,store.used,undo.count,undo_restored,reclaim_disabled);
 undo_close();
 printf("B2L NCACHE: pre-init %s applied=%llu already=%llu skipped=%llu; journal replay completed by HFS\n",restore==1?"restore":"apply",applied,already,skipped);
 printf("B2L NCACHE: corecrypto measured text original; excluded=%llu restored=%llu words; other translations retained\n",integrity_excluded,integrity_restored);
 if(restore==1||failures)return;
 int count=CACHE_WORKERS;(void)PE_parse_boot_argn("b2l_cache_workers",&count,sizeof(count));
 if(count<1||count>(int)CACHE_WORKERS)count=CACHE_WORKERS;
 for(unsigned i=0;i<(unsigned)count;i++){
  workers[i].id=i;thread_t t;if(kernel_thread_start(worker,&workers[i],&t)!=KERN_SUCCESS)break;
  thread_deallocate(t);__atomic_add_fetch(&worker_count,1,__ATOMIC_RELEASE);
 }
 unsigned started=__atomic_load_n(&worker_count,__ATOMIC_ACQUIRE);
 if(started){__atomic_store_n(&ready,1,__ATOMIC_RELEASE);printf("B2L NCACHE: automatic learning enabled; workers=%u; serialized durable commit; original files change on next boot\n",started);}
 else printf("B2L NCACHE: no worker started; emulation retained\n");
}
