#!/usr/bin/env python3
"""Run the actual B2LInput::poll body with a scripted sec_ts bus and real parser."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];arm=root/'xnu-overlay/iokit/Kernel/arm'
s=(arm/'B2LInput.cpp').read_text();body=s[s.index('void B2LInput::poll('):s.index('void B2LInput::stop(')]
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <tuple>
#include <algorithm>
#include <cstdarg>
extern "C" {
#include "B2LSecTsParser.h"
}
struct OSObject{};
struct IOTimerEventSource{unsigned ms=0;void setTimeoutMS(unsigned n){ms=n;}};
static uint64_t tick=1;
static std::vector<std::tuple<int,int,int,int>> delivered;
void b2l_hid_key(int,int){}
void b2l_hid_touch(int f,int d,int x,int y){delivered.emplace_back(f,d,x,y);}
void IOLog(const char*,...){}
void b2l_diag_record(const char*,...){}
using Event=std::array<uint8_t,8>;
struct Read{uint8_t cmd;std::vector<uint8_t> bytes;bool ok;};
static Event event(unsigned action,unsigned x,unsigned y,unsigned remaining){
 Event e={};e[0]=(action<<6)|4;e[1]=x>>4;e[2]=y>>4;e[3]=((x&15)<<4)|(y&15);e[6]=1;e[7]=remaining;return e;
}
class B2LInput:public OSObject{
public:
 alignas(uint32_t) uint8_t backing[128]={};uint8_t*gpio=backing;
 bool stable[4]={},candidate[4]={},touch=true;
 unsigned debounce[4]={},failures=0,keyLogs=0;
 B2L_SEC_TS_POINTER pointer={};
 uint64_t pollCount=0,eventCount=0,updateCount=0,bulkCount=0,transferFailures=0;
 uint64_t lastPollStart=0,lastDiagnostic=0,maxPollGapUS=0,maxPollWorkUS=0;
 unsigned maxPending=0,quiesced=0;
 uint64_t transferTimeUS=0,maxTransferUS=0;
 std::vector<Read> script;size_t next=0;
 B2LInput(){memset(backing,0xff,sizeof(backing));}
 static uint64_t uptimeUS(){return tick++;}
 void quiesce(){quiesced++;}
 bool command(uint8_t cmd,uint8_t*out,unsigned n){
  assert(n&&n<=256&&next<script.size());const auto&r=script[next++];assert(r.cmd==cmd&&r.bytes.size()==n);
  if(r.ok)memcpy(out,r.bytes.data(),n);return r.ok;
 }
 void add(uint8_t cmd,const std::vector<Event>&events,bool ok=true){
  Read r={cmd,{},ok};for(auto&e:events)r.bytes.insert(r.bytes.end(),e.begin(),e.end());script.push_back(r);
 }
 static void poll(OSObject*,IOTimerEventSource*);
};
'''
post=r'''
int main(){
 IOTimerEventSource timer;
 {B2LInput s;s.add(0x60,{Event{}});B2LInput::poll(&s,&timer);assert(s.next==1&&!s.eventCount&&timer.ms==20);}
 // Preserve a press, 30 moves and release in chronological order using 2 bus commands.
 {B2LInput s;delivered.clear();std::vector<Event> bulk;
  s.add(0x60,{event(1,100,200,31)});
  for(unsigned i=0;i<30;i++)bulk.push_back(event(2,101+i,201+i,30-i));
  bulk.push_back(event(3,140,240,0));s.add(0x61,bulk);
  B2LInput::poll(&s,&timer);assert(s.next==2&&s.eventCount==32&&s.updateCount==32&&s.bulkCount==1);
  assert(delivered.size()==32&&std::get<1>(delivered.front())==1&&std::get<1>(delivered.back())==0);
  assert(std::get<2>(delivered[1])==101&&std::get<2>(delivered[30])==130&&!s.pointer.Active);
 }
 // 63 remaining is legitimate backlog: bounded 248-byte bulk, no clear/reset/drop.
 {B2LInput s;delivered.clear();std::vector<Event> bulk(31,event(2,150,250,32));
  s.add(0x60,{event(1,100,200,63)});s.add(0x61,bulk);
  B2LInput::poll(&s,&timer);assert(s.eventCount==32&&s.maxPending==63&&s.touch&&!s.failures);
  s.add(0x60,{event(3,150,250,0)});B2LInput::poll(&s,&timer);assert(!s.pointer.Active&&s.eventCount==33);
 }
 // First event survives a failed bulk read. No uninitialized bulk bytes dispatched.
 {B2LInput s;delivered.clear();s.add(0x60,{event(1,10,20,1)});s.add(0x61,{event(3,10,20,0)},false);
  B2LInput::poll(&s,&timer);assert(delivered.size()==1&&s.pointer.Active&&s.failures==1&&s.transferFailures==1);
  s.add(0x60,{event(3,10,20,0)});B2LInput::poll(&s,&timer);assert(delivered.size()==2&&!s.pointer.Active&&!s.failures);
 }
 // Existing three-consecutive-failure cutoff releases contact once, leaves GPIO alive.
 {B2LInput s;delivered.clear();s.add(0x60,{event(1,10,20,0)});B2LInput::poll(&s,&timer);
  for(unsigned i=0;i<3;i++){s.add(0x60,{Event{}},false);B2LInput::poll(&s,&timer);}
  assert(!s.touch&&s.transferFailures==3&&s.quiesced==1&&delivered.size()==2&&std::get<1>(delivered.back())==0);
  B2LInput::poll(&s,&timer);assert(s.next==4&&timer.ms==20);
 }
 // Diagnostics expose scheduling gaps without changing GPIO debounce cadence.
 {B2LInput s;s.add(0x60,{Event{}});B2LInput::poll(&s,&timer);tick+=75000;
  s.add(0x60,{Event{}});B2LInput::poll(&s,&timer);assert(s.maxPollGapUS>=75000&&s.maxPollWorkUS&&timer.ms==20);}
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-input-poll-') as td:
 p=Path(td);(p/'test.cpp').write_text(pre+body+post)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(arm),'-c',str(arm/'B2LSecTsParser.c'),'-o',str(p/'parser.o')],check=True)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(arm),str(p/'test.cpp'),str(p/'parser.o'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('actual poll + real parser: FIFO ordering, 32-record bound, 63 backlog, partial failure, cutoff, timing PASS')
