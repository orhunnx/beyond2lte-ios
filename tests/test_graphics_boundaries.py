#!/usr/bin/env python3
"""Execute the actual blitter, IOSurface reply writer and HFS fallback gates.

Host ASan/UBSan checks validate memory and dispatch boundaries, not display or
input hardware. Reply offsets are from the local iOS 26.6.2 client ABI.
"""
from pathlib import Path
import re, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SURFACE = ROOT / "xnu-overlay/iokit/Kernel/arm/B2LSurface.cpp"

def function(source, name):
    start = source.index(name + "(")
    start = source.rfind("static void", 0, start)
    end = source.index("\n}", start) + 2
    return source[start:end]

s = SURFACE.read_text()
reply = "\n".join(function(s, n) for n in ("put32", "put64", "b2l_surface_fill_result"))
struct_out = '\nenum {kIODirectionIn=1,kIODirectionOut=2};\n'+(ROOT/'xnu-overlay/iokit/Kernel/arm/B2LMemoryIO.h').read_text()+'\n'+s[s.index("static IOReturn\nb2l_struct_out("):s.index("/* Map s for this client")]
limit_defines = "\n".join(re.findall(r"^#define B2L_SURFACE_(?:MAX_DIM|MAX_ALLOC).*", s, re.M))
# Execute the exact case body too, catching an old limits ABI reply.
limits = s.split("case kB2LSurfaceLimits: {", 1)[1].split("\n\t}", 1)[0]
patch = (ROOT / "patches/0037-b2l-hfs-regular-file-swap.patch").read_text()
added = "\n".join(line[1:] for line in patch.splitlines() if line.startswith("+") and not line.startswith("+++"))
hfs = added[added.index("            if (_err == ENOTSUP"):added.rindex("#endif")]
code = r'''#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <climits>
#include "B2LBlit.h"
#define B2L_SURFACE_RESULT_SIZE 0xc68
#define B2L_SURFACE_TIMESTAMPS 0x800
#define B2L_SURFACE_ROW_ALIGN 64
using IOReturn=int;
enum{kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNoSpace=2,kIOReturnVMError=3};
struct Map { uint64_t a; uint64_t getAddress() { return a; } };
struct B2LSurfaceRef { Map *data, *state; };
struct B2LSurface {
 uint32_t id,width,height,bytesPerRow,offset,pixelFormat,allocSize;
 uint16_t bytesPerElement; uint8_t elementWidth,elementHeight;
 uint32_t cacheMode,parentID;
};
''' + reply + r'''
struct Descriptor { int prepare(int){return 0;} int complete(int){return 0;}
 uint8_t data[42];uint64_t length=40;bool short_write=false;
 Descriptor(){memset(data,0xa5,sizeof(data));}
 uint64_t getLength(){return length;}
 uint64_t writeBytes(uint64_t off,const void*p,uint64_t n){
  assert(off==0&&n<=40);if(short_write)return n-1;
  memcpy(data+1,p,n);return n;
 }
};
struct IOExternalMethodArguments {
 unsigned scalarInputCount=0,scalarOutputCount=0;
 uint64_t structureInputSize=0,structureOutputSize=0;
 void*structureInputDescriptor=nullptr;
 Descriptor*structureOutputDescriptor=nullptr;
 void*structureOutput=nullptr;uint64_t structureOutputDescriptorSize=0;
};
''' + limit_defines + '\n' + struct_out + r'''
static void b2l_diag_record(const char*,...) {}
static int limits_test(IOExternalMethodArguments *args) {
''' + limits + r'''
}
struct Mount { const char *f_fstypename; };
struct Vnode { bool platform, regular; Mount *mount; };
#define NULLVP nullptr
#define ENOTSUP 45
#define VFS_RENAME_SWAP 2
static int calls, exchangeResult;
static bool b2l_amfi_vnode_is_platform(Vnode *v) {return v->platform && v->regular;}
static bool vnode_isreg(Vnode *v) {return v->regular;}
static Mount *vnode_mount(Vnode *v) {return v->mount;}
static Mount *vfs_statfs(Mount *m) {return m;}
static int VNOP_EXCHANGE(Vnode*,Vnode*,int,void*) {++calls; return exchangeResult;}
static void b2l_early_trace(const char*,int) {}
static int swap_test(int _err,unsigned flags,Vnode *a,Vnode *b) {
 Vnode **fvpp=&a, **tvpp=&b;void *ctx=nullptr;
''' + hfs + r'''
 return _err;
}
static uint64_t u64(const uint8_t*p,unsigned o) {uint64_t v;memcpy(&v,p+o,8);return v;}
int main() {
 uint32_t src[16],dst[16];for(unsigned i=0;i<16;++i)src[i]=i+1;
 memset(dst,0,sizeof(dst));
 assert(b2l_blit(dst,64,4,4,16,src,64,4,4,16,0,1,1,2,2,INT_MAX,INT_MAX));
 assert(dst[10]==src[5]&&dst[11]==src[6]&&dst[14]==src[9]&&dst[15]==src[10]);
 for(int bad=0;bad<7;++bad) {
  uint32_t old[16];memcpy(old,dst,64);
  assert(!b2l_blit(dst,bad==0?3:64,4,4,bad==1?1:16,
   src,bad==2?3:64,4,4,16,bad==3?UINT_MAX:0,
   bad==4?-1:0,0,0,0,bad==5?0:4,bad==6?-1:4));
  assert(!memcmp(old,dst,64));
 }
 // Deterministic malformed rectangles and actual short mapped allocations.
 uint32_t rng=5;for(unsigned i=0;i<100000;++i) {
  rng=rng*1664525+1013904223;int32_t x=(int32_t)rng;
  b2l_blit(dst,64,4,4,16,src,64,4,4,16,0,x,x,x,x,x,x);
 }
 Map data{0x12345678000},state{0x98765432000};B2LSurfaceRef ref{&data,&state};
 B2LSurface surf{19,1080,2400,4352,64,0x42475241,10444864,4,1,1,0,0};
 uint8_t *out=(uint8_t*)malloc(B2L_SURFACE_RESULT_SIZE);memset(out,0xa5,B2L_SURFACE_RESULT_SIZE);
 b2l_surface_fill_result(&surf,&ref,out);
 assert(u64(out,0)==data.a&&u64(out,8)==state.a&&u64(out,16)==state.a+0x800);
 assert(u64(out,0x20)==surf.allocSize&&u64(out,0x28)==1080&&u64(out,0x30)==2400);
 assert(u64(out,0x38)==4352&&u64(out,0x40)==64&&out[0x58]==0);
 for(unsigned i=0x74;i<B2L_SURFACE_RESULT_SIZE;++i)assert(out[i]==0);
 free(out);
 uint8_t guarded[42];memset(guarded,0xa5,sizeof(guarded));
 IOExternalMethodArguments q;q.structureOutput=guarded+1;q.structureOutputSize=40;
 assert(limits_test(&q)==0&&q.structureOutputSize==40);
 auto*lim=guarded+1;
 assert(guarded[0]==0xa5&&guarded[41]==0xa5);
 assert(u64(lim,8)==(256u<<20)&&u64(lim,0x10)==16384&&u64(lim,0x18)==16384);
 assert(lim[0]==63&&lim[4]==63&&lim[0x22]==1&&lim[0x23]==0);
 for(unsigned i=0x20;i<40;++i)assert(lim[i]==(i==0x22?1:0));
 // Native IOSurfaceGetPropertyMaximum reads width/height as 64-bit values
 // at reply+0x10/+0x18. CI::SWContext uses width for output/texture limits.
 // The old version/alignment-only reply admitted a service but rejected
 // even the observed 12x12 render. The corrected reply admits real sizes.
 auto ci_accepts=[](const uint8_t*p,uint64_t w,uint64_t h){
  uint64_t max=u64(p,0x10);return w<=max&&h<=max;
 };
 uint8_t old[40]{};old[0]=old[4]=63;old[0x22]=1;
 assert(!ci_accepts(old,12,12));
 assert(ci_accepts(lim,12,12)&&ci_accepts(lim,1440,3040));
 assert(ci_accepts(lim,16384,16384)&&!ci_accepts(lim,16385,1));
 assert(!ci_accepts(lim,1,16385));
 // MIG can supply non-null backing for absent inputs. Count zero is valid.
 for(unsigned bad=0;bad<6;++bad){
  auto a=q;memset(guarded,0xa5,sizeof(guarded));
  if(bad==0)a.scalarInputCount=1;if(bad==1)a.scalarOutputCount=1;
  if(bad==2)a.structureInputSize=1;if(bad==3)a.structureInputDescriptor=(void*)1;
  if(bad==4)a.structureOutputSize=39;if(bad==5)a.structureOutput=nullptr;
  assert(limits_test(&a)!=0);
  for(auto x:guarded)assert(x==0xa5);
 }
 Descriptor desc;auto dq=q;dq.structureOutput=nullptr;dq.structureOutputSize=0;
 dq.structureOutputDescriptor=&desc;assert(limits_test(&dq)==0&&dq.structureOutputDescriptorSize==40);
 assert(desc.data[0]==0xa5&&desc.data[41]==0xa5&&u64(desc.data+1,0x10)==16384);
 desc.length=39;memset(desc.data,0xa5,sizeof(desc.data));
 assert(limits_test(&dq)==kIOReturnNoSpace);for(auto x:desc.data)assert(x==0xa5);
 desc.length=40;desc.short_write=true;assert(limits_test(&dq)==kIOReturnVMError);

 Mount hfsMount{"hfs"},otherMount{"hfs"},apfsMount{"apfs"};
 Vnode a{true,true,&hfsMount},b{false,true,&hfsMount};exchangeResult=0;
 assert(swap_test(ENOTSUP,2,&a,&b)==0&&calls==1);
 exchangeResult=5;assert(swap_test(ENOTSUP,2,&a,&b)==5&&calls==2);
 for(unsigned i=0;i<7;++i) {
  Vnode aa=a,bb=b;unsigned flags=2;int err=ENOTSUP;
  if(i==0)aa.platform=false;if(i==1)aa.regular=false;if(i==2)bb.regular=false;
  if(i==3)bb.mount=&otherMount;if(i==4)aa.mount=bb.mount=&apfsMount;
  if(i==5)flags=3;if(i==6)err=22;
  assert(swap_test(err,flags,&aa,&bb)==err&&calls==2);
 }
 assert(swap_test(ENOTSUP,2,nullptr,&b)==ENOTSUP&&calls==2);
 assert(swap_test(ENOTSUP,2,&a,nullptr)==ENOTSUP&&calls==2);
}
'''
with tempfile.TemporaryDirectory(prefix="b2l-boundaries-") as td:
    td=Path(td); src=td/"checks.cpp";src.write_text(code);binary=td/"checks"
    subprocess.run(["clang++","-std=c++17","-g","-O1","-fsanitize=address,undefined",
        "-fno-omit-frame-pointer","-I",str(ROOT/"xnu-overlay/iokit/Kernel/arm"),str(src),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("PASS: actual blitter bounds, iOS 26 reply/limits ABI and HFS swap gates (ASan/UBSan)")
