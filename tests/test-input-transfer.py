#!/usr/bin/env python3
"""Execute production I2C message loop against a timed MMIO/FIFO model."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'xnu-overlay/iokit/Kernel/arm/B2LInput.cpp').read_text()
body=s[s.index('    void noteTransfer('):s.index('    bool writeCommand(')]
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <cstdio>
static uint64_t tick=1,ioCost=1,delayCost=1,delays=0;
static void IODelay(unsigned us){tick+=us+delayCost;++delays;}
struct B2LInput{
 unsigned timeoutUS=20000,resetCount=0,quiesceCount=0,failCount=0;
 uint64_t transferTimeUS=0,maxTransferUS=0,failedElapsed=0;
 unsigned failedBudget=0,failedAt=0;
 uint8_t lastCommand=0x60;
 bool run=false,reading=false,stuckDone=false,empty=false,full=false;
 unsigned length=0,at=0,completionDelay=0,byteInterval=0;
 uint64_t runAt=0;
 std::vector<uint8_t> bytes;
 static uint64_t uptimeUS(){return tick++;}
 uint32_t r(unsigned off){
  tick+=ioCost;
  if(off==0x30){
   if(reading)return (empty||at>=bytes.size()||tick<runAt+uint64_t(at+1)*byteInterval)?(1u<<24):0;
   return full?16:0x100;
  }
  if(off==0x38){assert(reading&&at<bytes.size());return bytes[at++];}
  if(off==0x24)return run&&!stuckDone&&at>=length&&tick>=runAt+completionDelay?0x80:0;
  return 0;
 }
 void w(unsigned off,uint32_t val){
  tick+=ioCost;
  if(off==0x44&&(val&(1u<<31))){run=true;reading=val&0x10000;length=val&0xffff;runAt=tick;}
  if(off==0x34){assert(!reading&&at<length);bytes.push_back((uint8_t)val);++at;}
 }
 void resetController(){++resetCount;}
 void quiesce(){++quiesceCount;run=false;}
 void transferFailure(bool,unsigned,unsigned done,unsigned budget,uint64_t elapsed){
  ++failCount;failedAt=done;failedBudget=budget;failedElapsed=elapsed;
 }
'''
post=r'''
};
static void resetTime(){tick=1;ioCost=1;delayCost=1;delays=0;}
int main(){
 // No idle stall when a whole FIFO is already ready, including max legal input.
 for(unsigned n: {1u,8u,16u,248u,256u}){
  resetTime();delayCost=100;B2LInput s;s.bytes.resize(n);
  for(unsigned i=0;i<n;++i)s.bytes[i]=(uint8_t)i;
  std::array<uint8_t,258> out={};out[0]=0xaa;out[n+1]=0xbb;
  assert(s.message(out.data()+1,n,true));assert(!memcmp(out.data()+1,s.bytes.data(),n));
  assert(!delays&&s.at==n&&s.quiesceCount==1&&!s.resetCount&&!s.failCount);
  assert(out[0]==0xaa&&out[n+1]==0xbb&&s.maxTransferUS<2000);
 }
 // Count all MMIO costs toward the deadline; old iteration counter overruns.
 {resetTime();ioCost=35;B2LInput s;s.empty=true;uint8_t out[8]={};
  assert(!s.message(out,8,true));assert(s.failedBudget==20000&&s.failedAt==0);
  assert(s.failedElapsed>=20000&&s.failedElapsed<20200&&s.resetCount==1&&s.quiesceCount==1);
  assert(delays<1000&&s.maxTransferUS==s.transferTimeUS);
 }
 // Clock jump / scheduling interruption terminates instead of spending more iterations.
 {resetTime();delayCost=750000;B2LInput s;s.empty=true;uint8_t out[8]={};
  assert(!s.message(out,8,true)&&delays==1&&s.failedAt==0&&s.resetCount==1);
 }
 // A slow but valid 248-byte bulk (~50ms) retains the 100ms transport budget.
 {resetTime();B2LInput s;s.byteInterval=200;s.bytes.resize(248,0x37);uint8_t out[248]={};
  assert(s.message(out,248,true)&&s.at==248&&s.maxTransferUS>=49600&&s.maxTransferUS<100000);
 }
 // Bulk stall is bounded to its real 100ms budget, not100000 MMIO iterations.
 {resetTime();ioCost=35;B2LInput s;s.empty=true;uint8_t out[248]={};
  assert(!s.message(out,248,true)&&s.failedBudget==100000&&s.failedElapsed<100200);
 }
 // Initialization retains its original100ms budget even for a single event.
 {resetTime();B2LInput s;s.timeoutUS=100000;s.empty=true;uint8_t out[8]={};
  assert(!s.message(out,8,true)&&s.failedBudget==100000&&s.failedElapsed>=100000);
 }
 // All bytes are insufficient without TRANSFER_DONE; wait and time out cleanly.
 {resetTime();B2LInput s;s.stuckDone=true;s.bytes.resize(8,0x55);uint8_t out[8]={};
  assert(!s.message(out,8,true)&&s.failedAt==8&&s.failedElapsed>=20000&&s.failedElapsed<20020);
 }
 // TX exact bytes + completion, and blocked TX deadline without writing data.
 {resetTime();B2LInput s;uint8_t in[]={0x30,0x61,0};assert(s.message(in,3,false));
  assert(s.bytes==std::vector<uint8_t>(in,in+3)&&!delays&&s.quiesceCount==1);
 }
 {resetTime();B2LInput s;s.full=true;uint8_t in=0x60;
  assert(!s.message(&in,1,false)&&s.bytes.empty()&&s.failedAt==0&&s.failedElapsed<20020);
 }
 // Invalid bounds never access MMIO, caller memory, or diagnostic/cleanup paths.
 {resetTime();B2LInput s;uint8_t x=0;
  assert(!s.message(nullptr,8,true)&&!s.message(&x,0,true)&&!s.message(&x,257,true));
  assert(tick==1&&!s.run&&!s.failCount&&!s.quiesceCount);
 }
 puts("production I2C loop: real deadlines, MMIO costs, scheduling jump, FIFO drain, bulk allowance, RX/TX/bounds PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-i2c-transfer-') as td:
 p=Path(td);(p/'test.cpp').write_text(pre+body+post)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
