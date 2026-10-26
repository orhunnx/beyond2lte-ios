#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the actual bounded payload writer with interruptions and bad files."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'xnu-overlay/bsd/kern/b2l_userspace_seed.h').read_text()
a=s.index('static int b2l_apps_sync_payload');b=s.index('static int b2l_apps_publish',a)
actual=s[a:b]
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <stdio.h>
typedef char *caddr_t;typedef int vfs_context_t;
enum vtype{VREG,VDIR};
struct node {unsigned char data[12000];off_t bytes;unsigned owner,mode,links;int type;};
typedef struct node *vnode_t;
struct vnode_attr{unsigned va_uid,va_mode,va_nlink;off_t va_data_size;};
static bool root_type(vnode_t vp,enum vtype t){return vp&&vp->type==(int)t;}
#define VATTR_INIT(p) memset(p,0,sizeof(*(p)))
#define VATTR_WANTED(p,x) ((void)0)
#define VATTR_IS_SUPPORTED(p,x) 1
#define UIO_READ 0
#define UIO_WRITE 1
#define UIO_SYSSPACE 0
#define IO_SYNC 1
#define MNT_WAIT 1
static const unsigned char b2l_apps_executable[1]={0};
static bool b2l_apps_payload_equal(const unsigned char*s,size_t bytes,size_t off,const unsigned char*a,size_t n,bool native){
 assert(!native&&off<=bytes&&n<=bytes-off);return !memcmp(s+off,a,n);
}
static int fail_write,fail_read,fail_sync,writes,reads,syncs;
static int vnode_getattr(vnode_t p,struct vnode_attr *v,int ctx){(void)ctx;*v=(struct vnode_attr){p->owner,p->mode,p->links,p->bytes};return 0;}
static int kauth_cred_get(void){return 0;}static int current_proc(void){return 0;}
static int vn_rdwr(int op,vnode_t p,caddr_t data,int n,off_t at,int space,int flags,int cred,int *resid,int proc){
 (void)space;(void)flags;(void)cred;(void)proc;assert(n>=0&&at>=0&&(size_t)(at+n)<=sizeof(p->data));*resid=0;
 if(op==UIO_READ){reads++;if(fail_read&&reads==fail_read)return EIO;assert(at+n<=p->bytes);memcpy(data,p->data+at,n);return 0;}
 writes++;if(fail_write&&writes==fail_write){int partial=n/2;memcpy(p->data+at,data,partial);p->bytes=at+partial;return EIO;}
 memcpy(p->data+at,data,n);p->bytes=at+n;return 0;
}
static int VNOP_FSYNC(vnode_t p,int flags,int ctx){(void)p;(void)flags;(void)ctx;syncs++;return fail_sync?EIO:0;}
'''
post=r'''
static unsigned char payload[10013];static struct node n;
static void reset(void){memset(&n,0,sizeof(n));n.type=VREG;n.mode=0755;n.links=1;fail_write=fail_read=fail_sync=writes=reads=syncs=0;}
int main(void){
 for(size_t i=0;i<sizeof(payload);i++)payload[i]=(unsigned char)(i*37+13);
 size_t prefixes[]={0,1,255,256,4095,4096,sizeof(payload)};
 for(unsigned i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++){
  reset();n.bytes=prefixes[i];memcpy(n.data,payload,prefixes[i]);assert(!b2l_apps_sync_payload(&n,payload,sizeof(payload),0755,true,0));
  assert(n.bytes==sizeof(payload)&&!memcmp(n.data,payload,sizeof(payload))&&syncs==1);
 }
 reset();fail_write=2;assert(b2l_apps_sync_payload(&n,payload,sizeof(payload),0755,true,0)==EIO&&n.bytes==6144&&!syncs);
 fail_write=0;assert(!b2l_apps_sync_payload(&n,payload,sizeof(payload),0755,true,0)&&!memcmp(n.data,payload,sizeof(payload)));
 for(unsigned fault=0;fault<8;fault++){
  reset();n.bytes=20;memcpy(n.data,payload,20);
  switch(fault){case 0:n.owner=501;break;case 1:n.mode=0777;break;case 2:n.links=2;break;case 3:n.type=VDIR;break;case 4:n.bytes=-1;break;case 5:n.bytes=11000;break;case 6:n.data[0]^=1;break;case 7:fail_read=1;break;}
  assert(b2l_apps_sync_payload(&n,payload,sizeof(payload),0755,true,0)&&!writes&&!syncs);
 }
 reset();fail_sync=1;assert(b2l_apps_sync_payload(&n,payload,sizeof(payload),0755,true,0)==EIO);
 reset();n.bytes=sizeof(payload);n.links=2;memcpy(n.data,payload,sizeof(payload));assert(!b2l_apps_sync_payload(&n,payload,sizeof(payload),0755,false,0)&&!writes&&!syncs);
 n.data[9999]^=1;assert(b2l_apps_sync_payload(&n,payload,sizeof(payload),0755,false,0)==EIO&&!writes);
 reset();assert(b2l_apps_sync_payload(&n,payload,0,0755,true,0)==EINVAL);assert(b2l_apps_sync_payload(&n,payload,4*1024*1024+1,0755,true,0)==EINVAL);
 puts("actual bootstrap payload writer: bounded/prefix-resume/corruption/metadata/I-O refusal PASS");
}
'''
# The runtime gate is actual user-space C, tested against token lookalikes.
u=(r/'userspace/system-app-bootstrap.m').read_text();a=u.index('static BOOL bootOption');b=u.index('static id call0',a)
gate='#include <string.h>\n#include <stddef.h>\n#include <assert.h>\ntypedef int BOOL;\n#define YES 1\n#define NO 0\n'+u[a:b]+r'''
int main(void){assert(bootOption("a=0 b2l_userspace_bootstrap=1 c=2","b2l_userspace_bootstrap=1"));
assert(!bootOption("xxb2l_userspace_bootstrap=1","b2l_userspace_bootstrap=1"));
assert(!bootOption("b2l_userspace_bootstrap=10","b2l_userspace_bootstrap=1"));
assert(!bootOption("b2l_userspace_bootstrap=0","b2l_userspace_bootstrap=1"));
assert(!bootOption("","b2l_userspace_bootstrap=1"));}
'''
with tempfile.TemporaryDirectory(prefix='b2l-bootstrap-test-') as td:
 p=Path(td)
 for name,code in [('writer',pre+actual+post),('gate',gate)]:
  (p/(name+'.c')).write_text(code)
  subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/(name+'.c')),'-o',str(p/name)],check=True)
  subprocess.run([str(p/name)],check=True)
