#!/usr/bin/env python3
"""Run the actual CPU presentation path with native iOS 26 client-wire fixtures."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'xnu-overlay/iokit/Kernel/arm/B2LFramebuffer.cpp').read_text()
get=s[s.index('static uint32_t\nget32('):s.index('\nbool\nIOMobileFramebufferUserClient::initWithTask')]
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <climits>
#include "B2LBlit.h"
#define B2L_FB_LAYERS 4
#define B2L_FB_FORMAT_BGRA 0x42475241
struct Memory {uint64_t length;uint64_t getLength(){return length;}};
struct B2LSurface {
 uint32_t width=4,height=3,bytesPerRow=24,offset=8,pixelFormat=B2L_FB_FORMAT_BGRA;
 uint32_t bytesPerElement=4,elementWidth=1,elementHeight=1;
 uint8_t data[80]{};Memory backing{80};Memory *memory=&backing;int refs=0;
 uintptr_t kernelAddress(){return (uintptr_t)data;}
 void release(){--refs;}
};
static B2LSurface surfaces[5];
static B2LSurface *b2l_surface_copy(uint32_t id){
 if(id<1||id>4)return nullptr;++surfaces[id].refs;return &surfaces[id];
}
#define OSSafeReleaseNULL(p) do{if(p){p->release();p=nullptr;}}while(0)
static unsigned locks,shown;
static void IOLockLock(void*){++locks;}
static void IOLockUnlock(void*){assert(locks);--locks;}
static void b2l_early_gui_present(){++shown;}
static void IOLog(const char*,...){}
static void b2l_diag_record(const char*,...){}
class AppleCLCD {
 public:
 uintptr_t fPixels;uint32_t fWidth=4,fHeight=3,fRowBytes=20;
 B2LSurface *fSurface=nullptr;void *fLock=nullptr;bool fConsolePaused=false;
 void swap(const uint8_t*);
};
''' + get.replace('__asm__ volatile("dsb sy":::"memory");','/* host: no ARM barrier */') + r'''
static void put(uint8_t *p,unsigned off,uint32_t v){memcpy(p+off,&v,4);}
// Build the userland client object, not a kernel-layout struct. Native
// SwapEnd sends exactly client+0x18 through client+0x59f (0x588 bytes).
static void layer(uint8_t *client,unsigned i,unsigned id,unsigned mask=0) {
 put(client,0x164,1u<<i);put(client,0x168,mask);put(client,0xb4+i*4,id);
 put(client,0xc4+i*16,0);put(client,0xc8+i*16,0);
 put(client,0xcc+i*16,4);put(client,0xd0+i*16,3);
 put(client,0x124+i*16,0);put(client,0x128+i*16,0);
 put(client,0x12c+i*16,4);put(client,0x130+i*16,3);
}
int main(){
 uint8_t client[0x5a0]{};uint8_t panel[68];
 AppleCLCD fb;fb.fPixels=(uintptr_t)(panel+4);
 for(unsigned id=1;id<=4;++id)
  for(unsigned y=0;y<3;++y)for(unsigned x=0;x<4;++x)
   put(surfaces[id].data,8+y*24+x*4,0xff000000|id*0x10000|y*16|x);
 auto check=[&](unsigned id){
  for(unsigned y=0;y<3;++y){
   assert(!memcmp(panel+4+y*20,surfaces[id].data+8+y*24,16));
   for(unsigned x=16;x<20;++x)assert(panel[4+y*20+x]==0xa5);
  }
  for(unsigned x=0;x<4;++x)assert(panel[x]==0xa5&&panel[64+x]==0xa5);
  for(unsigned i=1;i<=4;++i)assert(surfaces[i].refs==0);
  assert(locks==0);
 };
 for(unsigned i=0;i<4;++i){
  memset(client,0,sizeof(client));memset(panel,0xa5,sizeof(panel));
  layer(client,i,i+1);fb.swap(client+0x18);check(i+1);
 }
 assert(shown==1&&fb.fConsolePaused);
 // Removed layers may retain a stale surface ID; disable wins over update.
 memset(panel,0xa5,sizeof(panel));layer(client,3,4,8);fb.swap(client+0x18);
 for(auto v:panel)assert(v==0xa5);
 // Former wrong mask location must never activate a layer.
 memset(client,0,sizeof(client));layer(client,0,1);put(client,0x164,0);put(client,0x194,1);
 fb.swap(client+0x18);for(auto v:panel)assert(v==0xa5);
 // Invalid ID, unsupported format, short real backing and outside rects.
 for(unsigned bad=0;bad<4;++bad){
  memset(client,0,sizeof(client));layer(client,0,bad==0?4096:1);
  if(bad==1)surfaces[1].pixelFormat=0;
  if(bad==2)surfaces[1].backing.length=4;
  if(bad==3)put(client,0xc4,UINT_MAX);
  fb.swap(client+0x18);for(auto v:panel)assert(v==0xa5);
  surfaces[1].pixelFormat=B2L_FB_FORMAT_BGRA;surfaces[1].backing.length=80;
 }
 // All four enabled layers are read, copied in order and released.
 memset(client,0,sizeof(client));for(unsigned i=0;i<4;++i)layer(client,i,i+1);
 put(client,0x164,15);fb.swap(client+0x18);check(4);
 assert(b2l_fb_received==11&&b2l_fb_copied==8);
 assert(b2l_fb_disabled==1&&b2l_fb_missing==1&&b2l_fb_unsupported==1&&b2l_fb_bounds==2);
 assert(b2l_fb_last_mask==15&&b2l_fb_last_disabled==0&&b2l_fb_last_id==4);
 b2l_fb_progress(600); // diagnostics leave scanout, masks and references unchanged
 check(4);
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-fb-swap-') as tmp:
 tmp=Path(tmp);src=tmp/'check.cpp';src.write_text(code);binary=tmp/'check'
 subprocess.run(['clang++','-std=c++17','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'xnu-overlay/iokit/Kernel/arm'),str(src),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('PASS: native client-relative swap fixture reaches actual CPU blit; disable masks, padded rows, bounds and surface releases (ASan/UBSan)')
