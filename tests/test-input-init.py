#!/usr/bin/env python3
"""Exercise production touch initialization with scripted bus failures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'xnu-overlay/iokit/Kernel/arm/B2LInput.cpp').read_text()
body=s[s.index('void B2LInput::initializeTouch()'):s.index('bool B2LInput::start(')]
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <initializer_list>
#include <cstdarg>
static uint64_t tick=123;
void b2l_diag_record(const char*,...){}
struct Op {bool write;unsigned cmd;std::vector<uint8_t> bytes;bool ok;};
struct B2LInput {
 unsigned initAttempts=0,timeoutUS=20000,quiesced=0;bool touch=false,initPending=false;
 uint64_t nextInitUS=0;bool timing=true;std::vector<Op> ops;unsigned next=0;
 uint32_t r(unsigned off){assert(off==0x60||off==0x64);return timing?1:0;}
 static uint64_t uptimeUS(){return tick;}
 void quiesce(){++quiesced;}
 bool command(uint8_t cmd,uint8_t *p,unsigned n){assert(next<ops.size());auto &op=ops[next++];
  assert(!op.write&&op.cmd==cmd&&op.bytes.size()==n&&timeoutUS==100000);
  if(op.ok)memcpy(p,op.bytes.data(),n);return op.ok;}
 bool writeCommand(uint8_t *p,unsigned n){assert(next<ops.size());auto &op=ops[next++];
  assert(op.write&&op.cmd==p[0]&&op.bytes.size()==n&&timeoutUS==100000);
  assert(!memcmp(p,op.bytes.data(),n));return op.ok;}
 void initializeTouch();
 void good(){ops={{false,0x22,{0x53,0x45,0x37,0x71,0},true},
  {false,0x21,{0x80},true},{false,0x55,{0x20},true},
  {true,0x62,{0x62},true},{true,0x30,{0x30,0x61,0},true},{true,0x10,{0x10},true}};next=0;}
};
'''
post=r'''
int main(){
 // A failed startup probe becomes eligible one second later, then real setup succeeds.
 {B2LInput s;s.ops={{false,0x22,{0,0,0,0,0},false}};s.initializeTouch();
  assert(!s.touch&&s.initPending&&s.initAttempts==1&&s.next==1&&s.timeoutUS==20000);
  assert(s.nextInitUS==tick+1000000&&s.quiesced==1);
  tick=s.nextInitUS;s.good();s.initializeTouch();
  assert(s.touch&&!s.initPending&&s.initAttempts==2&&s.next==6&&s.timeoutUS==20000&&s.quiesced==2);}
 // Four failed attempts stop further scheduling; no unbounded retry loop.
 {B2LInput s;for(unsigned i=1;i<=4;++i){s.next=0;s.ops={{false,0x22,{0,0,0,0,0},false}};
  s.initializeTouch();assert(s.initAttempts==i&&!s.touch&&s.initPending==(i<4));tick=s.nextInitUS;}}
 // Missing inherited timing never touches sensor and never schedules another attempt.
 {B2LInput s;s.timing=false;s.initializeTouch();assert(!s.touch&&!s.initPending&&s.next==0&&s.timeoutUS==20000);}
 // Every read/write failure aborts setup and cannot report ready.
 for(unsigned fail=0;fail<6;++fail){B2LInput s;s.good();s.ops[fail].ok=false;s.initializeTouch();
  assert(!s.touch&&s.initPending&&s.next==fail+1&&s.timeoutUS==20000);}
 // Valid bus reads with wrong integrity or boot status must not enable sensing.
 for(unsigned which=0;which<2;++which){B2LInput s;s.good();s.ops[which?2:1].bytes[0]=0;
  s.initializeTouch();assert(!s.touch&&s.initPending&&s.next==3);}
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-input-init-') as td:
 p=Path(td);(p/'test.cpp').write_text(pre+body+post)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('actual initializeTouch: retry deadline, recovery, four-attempt cap, missing timings, read/write/integrity/boot failures PASS')
