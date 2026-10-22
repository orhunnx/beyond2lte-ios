/* SPDX-License-Identifier: BSD-2-Clause
 * Automatic observed-instruction cache. Trap path: fixed queue, no filesystem
 * access. Worker: resolve referenced VM map/pager to vnode/file offset, persist
 * original+native page in SYSTEM. Next boot: apply through normal HFS VFS only
 * after root journal replay and before the first userspace instruction.
 */
#include "b2l_native_cache.h"
#include "b2l_cache_core.h"
#include "b2l_cache_exclusion.h"
#include <arm64/b2l_arm64e_compat.h>
#include <arm64/b2l_early.h>
#include <machine/machine_routines.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
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
static struct b2l_io cache_io;
static uint8_t root_identity[12],*snapshot;
static uint32_t storage_ready,ready,queue_lock;
struct request {vm_map_t map;uint64_t pc;uint32_t instruction;};
static struct request queue[QSIZE];static unsigned head,tail;
static uint64_t hints[HINTS];
static uint64_t queued,dropped,learned,applied,already,skipped,failures;
static uint8_t record[B2L_NC_COPY],verify[B2L_NC_COPY],page[4096];
struct slot_key {uint64_t inode,offset;};
static struct slot_key keys[B2L_NC_SLOTS];
static struct b2l_nc_image integrity_image;
struct integrity_file {uint8_t uuid[16];uint64_t size;int result;struct b2l_nc_exclusion range;};
static struct integrity_file integrity_files[128];static unsigned integrity_count;
static uint8_t target_page[4096];
static uint64_t integrity_excluded,integrity_restored;

static int enabled(void){int v=0;return b2l_arm64e_compat_enabled()&&PE_parse_boot_argn("b2l_native_cache",&v,sizeof(v))&&v==1;}
void b2l_native_cache_storage(const struct b2l_io*io,const uint8_t id[12]){
 if(!io||!io->read||!io->write||!io->sync||io->bytes!=B2L_NC_BYTES)return;
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
static int identity(vnode_t vp,uint64_t*ino,uint64_t*bytes,uint8_t uuid[16],unsigned*kind){
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
static const uint8_t*slot(unsigned i){const uint8_t*out=NULL;
 if(b2l_nc_select(snapshot+B2L_NC_HEADER+(uint64_t)i*B2L_NC_CELL,&out))return NULL;return out;}
static int same_file(vnode_t vp,const uint8_t*r){uint64_t ino,size;uint8_t uuid[16];unsigned kind;
 return identity(vp,&ino,&size,uuid,&kind)||ino!=b2l_nc_u64(r+16)||size!=b2l_nc_u64(r+24)||
 kind!=b2l_nc_u32(r+56)||memcmp(uuid,r+40,16)?-1:0;}
static int persist(unsigned index,uint8_t*r,const uint8_t*old){
 uint64_t seq=old?b2l_nc_u64(old+8):0;if(seq==UINT64_MAX)return -1;
 b2l_nc_p64(r+8,seq+1);b2l_nc_seal(r);if(b2l_nc_record(r))return -1;
 uint8_t*cell=snapshot+B2L_NC_HEADER+(uint64_t)index*B2L_NC_CELL;
 unsigned which=old==cell?1:0;uint64_t at=B2L_NC_HEADER+(uint64_t)index*B2L_NC_CELL+which*B2L_NC_COPY;
 /* Never overwrite the previous valid copy. Publish RAM only after flush
  * and exact readback; on any persistence error stop learning for this boot. */
 if(cache_io.write(cache_io.ctx,at,r,B2L_NC_COPY)||cache_io.sync(cache_io.ctx)||
    cache_io.read(cache_io.ctx,at,verify,B2L_NC_COPY)||memcmp(r,verify,B2L_NC_COPY))return -1;
 memcpy(cell+which*B2L_NC_COPY,r,B2L_NC_COPY);keys[index].inode=b2l_nc_u64(r+16);keys[index].offset=b2l_nc_u64(r+32);return 0;
}
static void learn(struct request*q){
 vnode_t vp=NULL;uint64_t off=0,ino=0,size=0;uint8_t uuid[16];unsigned kind=0;uint32_t native;
 if(!b2l_nc_native(q->instruction,&native)||b2l_cache_resolve(q->map,q->pc,&vp,&off)||!vp){skipped++;return;}
 if(identity(vp,&ino,&size,uuid,&kind)||off>size||size-off<4||(off&3)){skipped++;goto out;}
 if(kind==2){
  struct integrity_file*f=integrity_file(vp,size,uuid);
  if(!f||f->result<0){skipped++;goto out;}
  if(f->result==1&&off<f->range.offset+f->range.bytes&&f->range.offset<off+4){skipped++;goto out;}
 }
 unsigned index=B2L_NC_SLOTS;uint64_t po=off&~4095ULL;
 for(unsigned i=0;i<B2L_NC_SLOTS;i++)if(keys[i].inode==ino&&keys[i].offset==po){index=i;break;}
 if(index==B2L_NC_SLOTS)for(unsigned i=0;i<B2L_NC_SLOTS;i++)if(!keys[i].inode){index=i;break;}
 if(index==B2L_NC_SLOTS){__atomic_fetch_add(&dropped,1,__ATOMIC_RELAXED);goto out;}
 unsigned len=size-po>=4096?4096:(unsigned)(size-po);memset(page,0,4096);
 if(rd(vp,po,page,len)||b2l_nc_u32(page+(off-po))!=q->instruction){skipped++;goto out;}
 const uint8_t*old=slot(index);
 if(old){
  if(b2l_nc_u64(old+16)!=ino||b2l_nc_u64(old+24)!=size||memcmp(old+40,uuid,16)||b2l_nc_u32(old+56)!=kind||b2l_nc_page_state(old,page,len)<0){skipped++;goto out;}
  /* A new image page may match original or fully applied native; both are
   * admissible. Preserve the first original bytes across all generations. */
  memcpy(record,old,B2L_NC_COPY);
 }else{
  memset(record,0,B2L_NC_COPY);memcpy(record,"B2LNCREC",8);b2l_nc_p64(record+16,ino);b2l_nc_p64(record+24,size);b2l_nc_p64(record+32,po);
  memcpy(record+40,uuid,16);b2l_nc_p32(record+56,kind);b2l_nc_p32(record+60,len);
  memcpy(record+B2L_NC_DATA,page,4096);memcpy(record+B2L_NC_DATA+4096,page,4096);
 }
 unsigned w=(unsigned)(off-po);uint32_t original=b2l_nc_u32(record+B2L_NC_DATA+w),expected;
 if(!b2l_nc_native(original,&expected)||expected!=native){skipped++;goto out;}
 if(b2l_nc_u32(record+B2L_NC_DATA+4096+w)==native)goto out;
 b2l_nc_p32(record+B2L_NC_DATA+4096+w,native);
 if(persist(index,record,old)){failures++;__atomic_store_n(&ready,0,__ATOMIC_RELEASE);}else learned++;
 out:vnode_put(vp);
}
void b2l_native_cache_observe(uint64_t pc,uint32_t insn){
 uint32_t replacement;if(!__atomic_load_n(&ready,__ATOMIC_RELAXED)||!b2l_nc_native(insn,&replacement))return;
 /* Try-lock: the exception path never waits for the worker or storage. */
 disable_preemption();
 if(__atomic_exchange_n(&queue_lock,1,__ATOMIC_ACQUIRE)){enable_preemption();__atomic_fetch_add(&dropped,1,__ATOMIC_RELAXED);return;}
 vm_map_t map=current_map();uint64_t tag=((uint64_t)(uintptr_t)map>>4)^pc^((uint64_t)insn<<17);if(!tag)tag=1;
 unsigned h=(unsigned)((tag^(tag>>19)^(tag>>37))&(HINTS-1));
 if(hints[h]!=tag){unsigned next=(head+1)%QSIZE;if(next!=tail){vm_map_reference(map);queue[head]=(struct request){map,pc,insn};head=next;hints[h]=tag;__atomic_fetch_add(&queued,1,__ATOMIC_RELAXED);}else __atomic_fetch_add(&dropped,1,__ATOMIC_RELAXED);}
 __atomic_store_n(&queue_lock,0,__ATOMIC_RELEASE);enable_preemption();
}
static int cache_dequeue(struct request*q){
 disable_preemption();
 if(__atomic_exchange_n(&queue_lock,1,__ATOMIC_ACQUIRE)){enable_preemption();return 0;}
 int found=head!=tail;if(found){*q=queue[tail];tail=(tail+1)%QSIZE;}
 __atomic_store_n(&queue_lock,0,__ATOMIC_RELEASE);enable_preemption();return found;
}
static void worker(void*arg __unused,wait_result_t why __unused){unsigned reports=0;
 for(;;){struct request q;unsigned budget=128;
  while(budget--&&cache_dequeue(&q)){if(__atomic_load_n(&ready,__ATOMIC_ACQUIRE))learn(&q);vm_map_deallocate(q.map);}
  if(++reports==30){reports=0;b2l_early_trace("B2L NCACHE: queued=%llu learned=%llu applied=%llu skipped=%llu dropped=%llu errors=%llu\n",__atomic_load_n(&queued,__ATOMIC_RELAXED),learned,applied,skipped,__atomic_load_n(&dropped,__ATOMIC_RELAXED),failures);}
  assert_wait_timeout((event_t)&cache_io,THREAD_UNINT,1,NSEC_PER_SEC);thread_block(THREAD_CONTINUE_NULL);
 }
}
void b2l_native_cache_before_init(void){
 if(!enabled()||!storage_ready){printf("B2L NCACHE: unavailable; emulation retained\n");return;}
 DTEntry dt;const void*prop;unsigned n;uint64_t range[2];
 if(SecureDTLookupEntry(NULL,"/chosen",&dt)!=kSuccess||SecureDTGetProperty(dt,"b2l,native-cache",&prop,&n)!=kSuccess||n!=16)return;
 memcpy(range,prop,16);boot_args*args=(boot_args*)PE_state.bootArgs;
 uint64_t base=range[0];if(range[1]!=B2L_NC_BYTES||(base&4095)||base<args->physBase||base>args->topOfKernelData||B2L_NC_BYTES>args->topOfKernelData-base)return;
 snapshot=(uint8_t*)ml_static_ptovirt(base);
 if(!snapshot||b2l_nc_header(snapshot,root_identity)||b2l_nc_header(snapshot+4096,root_identity)||memcmp(snapshot,snapshot+4096,4096))return;
 if(cache_io.read(cache_io.ctx,0,verify,8192)||memcmp(snapshot,verify,8192))return;
 /* Validate all cells and reject duplicate source-page keys before any write. */
 for(unsigned i=0;i<B2L_NC_SLOTS;i++){
  const uint8_t*r=NULL;if(b2l_nc_select(snapshot+B2L_NC_HEADER+(uint64_t)i*B2L_NC_CELL,&r))return;
  if(!r)continue;keys[i].inode=b2l_nc_u64(r+16);keys[i].offset=b2l_nc_u64(r+32);
  for(unsigned j=0;j<i;j++)if(keys[j].inode==keys[i].inode&&keys[j].offset==keys[i].offset){printf("B2L NCACHE: duplicate page rejected\n");return;}
 }
 /* Resolve the measured module before applying any native bytes. Failure is
  * explicit: never guess an exclusion or continue into a known dirty module. */
 int restore=0;(void)PE_parse_boot_argn("b2l_cache_restore",&restore,sizeof(restore));
 if(restore!=1&&integrity_open())panic("B2L native cache: corecrypto image metadata unavailable");
 for(unsigned i=0;i<B2L_NC_SLOTS;i++){
  if(!keys[i].inode)continue;const uint8_t*r=slot(i);vnode_t vp=NULL;
  if(!r||VFS_VGET(vnode_mount(rootvnode),(ino64_t)keys[i].inode,&vp,vfs_context_current())||!vp){skipped++;continue;}
  unsigned len=b2l_nc_u32(r+60);uint64_t off=keys[i].offset;
  if(same_file(vp,r)||rd(vp,off,page,len)||b2l_nc_page_state(r,page,len)<0){skipped++;vnode_put(vp);continue;}
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
  if(!memcmp(page,target,len)){already++;vnode_put(vp);continue;}
  int resid=0;int e=vn_rdwr(UIO_WRITE,vp,(caddr_t)target,(int)len,(off_t)off,UIO_SYSSPACE,IO_SYNC,kauth_cred_get(),&resid,current_proc());
  if(!e&&!resid)e=VNOP_FSYNC(vp,MNT_WAIT,vfs_context_current());
  if(e||resid||rd(vp,off,page,len)||memcmp(page,target,len)){failures++;vnode_put(vp);printf("B2L NCACHE: apply I/O failure; aborting before userspace\n");panic("B2L native cache apply failed; recovery required");}
  applied++;vnode_put(vp);
 }
 printf("B2L NCACHE: pre-init %s applied=%llu already=%llu skipped=%llu; journal replay completed by HFS\n",restore==1?"restore":"apply",applied,already,skipped);
 printf("B2L NCACHE: corecrypto measured text original; excluded=%llu restored=%llu words; other translations retained\n",integrity_excluded,integrity_restored);
 if(restore==1)return;
 thread_t t;if(kernel_thread_start(worker,NULL,&t)==KERN_SUCCESS){thread_deallocate(t);__atomic_store_n(&ready,1,__ATOMIC_RELEASE);printf("B2L NCACHE: automatic learning enabled; original files change on next boot\n");}
}
