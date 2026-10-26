#!/usr/bin/env python3
"""Production collection writer/reader vs independent iOS26 nested-header walk."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];d=r/'xnu-overlay/iokit/Kernel/arm/hid';src=(d/'IOHIDEvent.cpp').read_text()
def method(marker):
 a=src.index(marker);b=src.index('{',a);i=b+1;depth=1
 while depth:depth+=(src[i]=='{')-(src[i]=='}');i+=1
 return src[a:i]
structs=(d/'IOHIDEventStructDefs.h').read_text();at=structs.index('uint32_t transducerIndex;');a=structs.rindex('typedef struct {',0,at);b=structs.index('} IOHIDDigitizerEventData;',at)+len('} IOHIDDigitizerEventData;')
code=r'''
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <cstdio>
#include <vector>
using UInt8=uint8_t;using UInt32=uint32_t;using UInt64=uint64_t;using IOByteCount=size_t;
using IOFixed=int32_t;using IOHIDEventType=uint32_t;
const uint32_t kIOHIDEventTypeDigitizer=11,kIOHIDEventTypeCount=44,kIOHIDEventOptionIsCollection=2;
struct IOHIDEventData{uint32_t size,type,options;uint8_t depth,reserved[3];};
struct __attribute__((packed,aligned(4))) IOHIDSystemQueueElement{uint64_t timeStamp,senderID;uint32_t options,attributeLength,eventCount;uint8_t payload[0];};
''' + structs[a:b] + '\n#include "'+str(d/'b2l_hid_wire.h')+'"\n'+r'''
#define IOHIDEventGetSize(t,s) do{(s)=((t)==11?104:32);}while(0)
class OSObject{public:unsigned refs=1;virtual ~OSObject(){};void retain(){refs++;}void release(){if(!--refs)delete this;}};
class OSArray:public OSObject{public:std::vector<OSObject*> items;~OSArray(){for(auto*p:items)p->release();}static OSArray* withObjects(const OSObject**p,unsigned n){auto*a=new OSArray;for(unsigned i=0;i<n;i++)a->setObject(const_cast<OSObject*>(p[i]));return a;}bool setObject(OSObject*p){p->retain();items.push_back(p);return true;}unsigned getCount(){return items.size();}OSObject*getObject(unsigned i){return items[i];}};
class IOHIDEvent:public OSObject{public:
 IOHIDEventData*_data=nullptr;IOHIDEvent*_parent=nullptr;OSArray*_children=nullptr;
 uint32_t _eventCount=0,_options=0;uint64_t _timeStamp=0,_senderID=0;
 ~IOHIDEvent(){if(_children)_children->release();free(_data);}
 bool initWithType(uint32_t t,uint32_t extra=0){unsigned n;IOHIDEventGetSize(t,n);_data=(IOHIDEventData*)calloc(1,n+extra);_data->size=n+extra;_data->type=t;return true;}
 void appendChild(IOHIDEvent*);size_t getLength();IOByteCount getLength(UInt32*);IOByteCount appendBytes(UInt8*,IOByteCount);IOByteCount readBytes(void*,IOByteCount);static IOHIDEvent*withBytes(const void*,IOByteCount);
};
'''
for m in ('void IOHIDEvent::appendChild(', 'size_t IOHIDEvent::getLength()', 'IOByteCount IOHIDEvent::getLength(UInt32 * count)', 'IOByteCount IOHIDEvent::appendBytes(', 'IOByteCount IOHIDEvent::readBytes(', 'IOHIDEvent * IOHIDEvent::withBytes('):code+=method(m)+'\n'
code+=r'''
static IOHIDEvent* digit(uint32_t x){auto*e=new IOHIDEvent;assert(e->initWithType(11));e->_senderID=0x100000122;e->_timeStamp=1234567;auto*v=(IOHIDDigitizerEventData*)e->_data;v->position.x=x;v->position.y=12345;v->generationCount=9;return e;}
// Independent walk of current (nonlegacy) native helper: every data item has a
// 28-byte header; payload depth chooses its parent. No production reader used.
static bool nativeWalk(const std::vector<uint8_t>&b,unsigned expected){
 if(b.size()<28)return false;uint32_t count;memcpy(&count,b.data()+24,4);if(count!=expected)return false;
 size_t off=0;for(unsigned i=0;i<count;i++){
  if(b.size()-off<28)return false;uint32_t attrs;memcpy(&attrs,b.data()+off+20,4);off+=28;if(attrs>b.size()-off)return false;off+=attrs;
  if(b.size()-off<16)return false;uint32_t size,type;memcpy(&size,b.data()+off,4);memcpy(&type,b.data()+off+4,4);
  if(size!=160||type!=11||size>b.size()-off)return false;off+=size;
 }return off==b.size();
}
static std::vector<uint8_t> encode(IOHIDEvent*p){size_t n=p->getLength();std::vector<uint8_t>b(n+8,0xa5);assert(p->readBytes(b.data(),n)==n);for(size_t i=n;i<b.size();i++)assert(b[i]==0xa5);b.resize(n);return b;}
int main(){
 static_assert(sizeof(IOHIDSystemQueueElement)==28);
 auto*p=digit(0);auto*c=digit(32768);p->appendChild(c);c->release();auto b=encode(p);
 assert(b.size()==376&&nativeWalk(b,2));uint32_t n;memcpy(&n,b.data()+188+24,4);assert(n==1);assert(b[28+12]==0&&b[188+28+12]==1);
 auto*q=IOHIDEvent::withBytes(b.data(),b.size());assert(q&&q->_children->getCount()==1);assert(encode(q)==b);q->release();
 // 0088's mixed current-data/legacy-framing packet is rejected independently.
 auto old=b;old.erase(old.begin()+188,old.begin()+216);assert(old.size()==348&&!nativeWalk(old,2));assert(!IOHIDEvent::withBytes(old.data(),old.size()));
 auto bad=b;bad.pop_back();assert(!IOHIDEvent::withBytes(bad.data(),bad.size()));
 bad=b;memset(bad.data()+188+20,0xff,4);assert(!IOHIDEvent::withBytes(bad.data(),bad.size()));
 bad=b;bad[188+28+12]=3;assert(!IOHIDEvent::withBytes(bad.data(),bad.size()));
 bad=b;bad[24]=3;assert(!IOHIDEvent::withBytes(bad.data(),bad.size()));
 auto*g=digit(65536);c->appendChild(g);g->release();auto nested=encode(p);assert(nested.size()==564&&nativeWalk(nested,3));assert(nested[376+28+12]==2);
 q=IOHIDEvent::withBytes(nested.data(),nested.size());assert(q&&q->_children->getCount()==1);assert(((IOHIDEvent*)q->_children->getObject(0))->_children->getCount()==1);assert(encode(q)==nested);q->release();p->release();
 auto*k=new IOHIDEvent;k->initWithType(3);auto keyboard=encode(k);assert(keyboard.size()==60);q=IOHIDEvent::withBytes(keyboard.data(),keyboard.size());assert(q&&encode(q)==keyboard);q->release();k->release();
 puts("Production HID collection: native per-event headers/depth/subtree counts; malformed/0088 framing rejected; nested roundtrip and keyboard unchanged PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
