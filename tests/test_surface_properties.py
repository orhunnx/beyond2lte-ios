#!/usr/bin/env python3
"""Run actual IOSurface metadata adapter code with native-collection/I/O fixtures.
The harness models OSObject lifetime and allocation/fault paths; Apple's parser
and serializer implementations are not part of this host test.
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PREAMBLE = r'''#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdio>
#define OSDynamicCast(T,p) dynamic_cast<T*>(p)
#define OSSafeReleaseNULL(p) do{if(p){(p)->release();(p)=nullptr;}}while(0)
static int live=0, buffers=0, fail_alloc=-1;
static bool allocation_fails(){if(fail_alloc<0)return false;if(fail_alloc==0)return true;--fail_alloc;return false;}
static void*IOMallocData(size_t n){if(allocation_fails())return nullptr;void*p=malloc(n);if(p)++buffers;return p;}
static void*IOMallocZeroData(size_t n){void*p=IOMallocData(n);if(p)memset(p,0,n);return p;}
static void IOFreeData(void*p,size_t){if(p){--buffers;free(p);}}
using IOReturn=int;
enum{kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNoMemory=2,kIOReturnNoSpace=3,kIOReturnVMError=4,kIOReturnNotFound=5,kIOReturnUnsupported=6};
constexpr unsigned B2L_SURFACE_MAX_PROPS=64u<<10;
enum{kB2LSurfaceSetYCbCrMatrix=5,kB2LSurfaceSetValue=9,kB2LSurfaceCopyValues=10,kB2LSurfaceSetBulkAttachments=27};
class OSSerialize;
class OSObject{public:unsigned refs=1;OSObject(){++live;}virtual~OSObject(){--live;}void retain(){++refs;}void release(){assert(refs);if(!--refs)delete this;}virtual bool serialize(OSSerialize*){return false;}};
class OSString:public OSObject{public:std::string text;explicit OSString(const char*s):text(s){}};
class OSNumber:public OSObject{public:uint32_t value;explicit OSNumber(uint32_t n):value(n){}};
class OSArray:public OSObject{public:std::vector<OSObject*>items;~OSArray()override{for(auto*p:items)p->release();}unsigned getCount(){return items.size();}OSObject*getObject(unsigned i){return i<items.size()?items[i]:nullptr;}void add(OSObject*p){p->retain();items.push_back(p);}};
class OSDictionary:public OSObject{public:std::map<std::string,OSObject*>items;~OSDictionary()override{for(auto&p:items)p.second->release();}static OSDictionary*withCapacity(unsigned){return allocation_fails()?nullptr:new OSDictionary;}static OSDictionary*withDictionary(OSDictionary*p){auto*d=withCapacity(0);if(d&&!d->merge(p)){d->release();return nullptr;}return d;}unsigned getCount(){return items.size();}bool setObject(OSString*k,OSObject*v){if(allocation_fails())return false;v->retain();auto it=items.find(k->text);if(it!=items.end())it->second->release();items[k->text]=v;return true;}bool merge(OSDictionary*d){for(auto&p:d->items){OSString k(p.first.c_str());if(!setObject(&k,p.second))return false;}return true;}bool serialize(OSSerialize*)override;};
class OSSerialize:public OSObject{public:std::string bytes;static OSSerialize*binaryWithCapacity(unsigned){return allocation_fails()?nullptr:new OSSerialize;}unsigned getLength(){return bytes.size();}const char*text(){return bytes.data();}};
static bool fail_serialize=false, oversize_serialize=false;
bool OSDictionary::serialize(OSSerialize*s){if(fail_serialize)return false;s->bytes="dictionary:";for(auto&p:items){s->bytes+=p.first;s->bytes+='=';if(auto*n=dynamic_cast<OSNumber*>(p.second))s->bytes+=std::to_string(n->value);s->bytes+=';';}if(oversize_serialize)s->bytes.resize(B2L_SURFACE_MAX_PROPS+1,'x');return true;}
class Descriptor:public OSObject{public:int prepare(int){return 0;} int complete(int){return 0;}std::vector<uint8_t>bytes;uint64_t length_override=0;bool short_read=false,short_write=false;explicit Descriptor(size_t n):bytes(n){}uint64_t getLength(){return length_override?length_override:bytes.size();}uint64_t readBytes(uint64_t off,void*p,uint64_t n){if(off>bytes.size()||n>bytes.size()-off)return 0;if(short_read)return n? n-1:0;memcpy(p,bytes.data()+off,n);return n;}uint64_t writeBytes(uint64_t off,const void*p,uint64_t n){if(off>bytes.size()||n>bytes.size()-off)return 0;if(short_write)return n? n-1:0;memcpy(bytes.data()+off,p,n);return n;}void*getBytesNoCopy(){return bytes.data();}};
struct IOExternalMethodArguments{uint64_t*scalarInput=nullptr;unsigned scalarInputCount=0;const void*structureInput=nullptr;uint64_t structureInputSize=0;Descriptor*structureInputDescriptor=nullptr;void*structureOutput=nullptr;uint64_t structureOutputSize=0;Descriptor*structureOutputDescriptor=nullptr;uint64_t structureOutputDescriptorSize=0;};
using IOLock=std::mutex;static IOLock global_lock;static IOLock*b2l_surface_lock=&global_lock;static void IOLockLock(IOLock*p){p->lock();}static void IOLockUnlock(IOLock*p){p->unlock();}
class B2LSurface:public OSObject{public:uint32_t id=77;Descriptor*state=new Descriptor(4096);OSDictionary*properties=nullptr;OSSerialize*serializedProperties=nullptr;~B2LSurface()override{OSSafeReleaseNULL(properties);OSSafeReleaseNULL(serializedProperties);state->release();}};
class B2LSurfaceRef:public OSObject{public:B2LSurface*surface;explicit B2LSurfaceRef(B2LSurface*s):surface(s){s->retain();}~B2LSurfaceRef()override{surface->release();}};
class IOSurfaceRootUserClient{public:IOLock own_lock;IOLock*fLock=&own_lock;OSArray*fRefs=new OSArray;~IOSurfaceRootUserClient(){fRefs->release();}IOReturn metadata(uint32_t,IOExternalMethodArguments*);};
static OSObject*parse_fixture=nullptr;
/* The host harness models native OS collection/parser/serializer allocation;
 * it executes adapter byte validation, mutation, seed and I/O code verbatim.
 * It does not claim to retest Apple's OSUnserialize/OSSerialize implementation. */
static OSObject*OSUnserializeXML(const char*,size_t){if(!parse_fixture)return nullptr;parse_fixture->retain();return parse_fixture;}
'''
TESTS = r'''static uint32_t word(B2LSurface*s,unsigned offset){uint32_t n;memcpy(&n,(uint8_t*)s->state->getBytesNoCopy()+offset,4);return n;}
static std::vector<uint8_t> packet(uint32_t id,const std::vector<uint32_t>&tokens){std::vector<uint8_t>b(12+tokens.size()*4);memcpy(b.data(),&id,4);memcpy(b.data()+12,tokens.data(),tokens.size()*4);return b;}
static void input(IOExternalMethodArguments&a,std::vector<uint8_t>&v){a.structureInput=v.data();a.structureInputSize=v.size();}
static void output(IOExternalMethodArguments&a,void*p,size_t n){a.structureOutput=p;a.structureOutputSize=n;a.structureOutputDescriptor=nullptr;}
int main(){
 int baseline=live;
 auto*s=new B2LSurface;
 IOSurfaceRootUserClient client,other;
 auto*ref=new B2LSurfaceRef(s);client.fRefs->add(ref);ref->release();
 unsigned retained=s->refs;
 IOExternalMethodArguments a;uint64_t scalars[]={77,248};a.scalarInput=scalars;a.scalarInputCount=2;
 assert(other.metadata(5,&a)==kIOReturnNotFound);
 assert(client.metadata(5,&a)==0&&((uint8_t*)s->state->getBytesNoCopy())[0x6e]==248);
 scalars[1]=256;assert(client.metadata(5,&a)==kIOReturnBadArgument);scalars[1]=9;
 assert(client.metadata(5,&a)==0&&((uint8_t*)s->state->getBytesNoCopy())[0x6e]==9);
 a.scalarInputCount=1;assert(client.metadata(5,&a)==kIOReturnBadArgument);a.scalarInputCount=0;
 std::vector<uint8_t>bulk(160,0xa5);uint64_t n=132,mask=0x3fffff;uint32_t id=77;
 memcpy(bulk.data()+0x88,&n,8);memcpy(bulk.data()+0x90,&mask,8);memcpy(bulk.data()+0x98,&id,4);input(a,bulk);
 assert(client.metadata(27,&a)==0);
 std::vector<uint8_t>before=s->state->bytes;
 for(unsigned bit=0;bit<22;bit++){
  memset(s->state->bytes.data()+0x34,0,132);mask=1ULL<<bit;memcpy(bulk.data()+0x90,&mask,8);
  n=b2l_surface_attachment_spans[bit].offset+b2l_surface_attachment_spans[bit].length;
  memcpy(bulk.data()+0x88,&n,8);assert(client.metadata(27,&a)==0);
  auto span=b2l_surface_attachment_spans[bit];
  for(unsigned j=0;j<132;j++)assert(s->state->bytes[0x34+j]==((j>=span.offset&&j<span.offset+span.length)?0xa5:0));
  before=s->state->bytes;n--;memcpy(bulk.data()+0x88,&n,8);
  assert(client.metadata(27,&a)==kIOReturnBadArgument&&before==s->state->bytes);
 }
 mask=1ULL<<22;n=132;memcpy(bulk.data()+0x90,&mask,8);memcpy(bulk.data()+0x88,&n,8);
 assert(client.metadata(27,&a)==kIOReturnBadArgument&&before==s->state->bytes);
 n=UINT64_MAX;memcpy(bulk.data()+0x88,&n,8);assert(client.metadata(27,&a)==kIOReturnBadArgument);
 a.structureInputSize=159;assert(client.metadata(27,&a)==kIOReturnBadArgument);
 auto*array=new OSArray;auto*value=new OSNumber(42);auto*key=new OSString("fixture");array->add(value);array->add(key);value->release();key->release();parse_fixture=array;
 /* Real IOCF binary token layout: array[value, symbol key]. */
 auto p=packet(77,{0xd3,0x82000002,0x04000020,42,0,0x88000008,0x74786966,0x00657275});
 input(a,p);uint32_t seed=0;output(a,&seed,4);
 assert(other.metadata(9,&a)==kIOReturnNotFound);
 assert(client.metadata(9,&a)==0&&seed==3&&word(s,0x20)==3&&word(s,0x28)>0);
 assert(s->properties->items.size()==1&&dynamic_cast<OSNumber*>(s->properties->items["fixture"])->value==42);
 assert(client.metadata(9,&a)==0&&seed==5&&word(s,0x20)==5);
 assert(s->refs==retained);
 auto*old=s->properties;auto*oldbytes=s->serializedProperties;
 output(a,&seed,3);assert(client.metadata(9,&a)==kIOReturnNoSpace&&s->properties==old&&s->serializedProperties==oldbytes&&word(s,0x20)==5);
 Descriptor out(4);out.short_write=true;a.structureOutputDescriptor=&out;
 assert(client.metadata(9,&a)==kIOReturnVMError&&s->properties==old&&word(s,0x20)==5);
 out.short_write=false;assert(client.metadata(9,&a)==0&&word(s,0x20)==7&&a.structureOutputDescriptorSize==4);
 output(a,&seed,4);
 for(int f=0;f<5;f++){old=s->properties;oldbytes=s->serializedProperties;uint32_t prior=word(s,0x20);int objects=live,buf=buffers;fail_alloc=f;int ret=client.metadata(9,&a);fail_alloc=-1;assert(ret!=0&&old==s->properties&&oldbytes==s->serializedProperties&&word(s,0x20)==prior&&live==objects&&buffers==buf);}
 fail_serialize=true;old=s->properties;assert(client.metadata(9,&a)==kIOReturnNoMemory&&s->properties==old);fail_serialize=false;
 oversize_serialize=true;assert(client.metadata(9,&a)==kIOReturnNoSpace&&s->properties==old);oversize_serialize=false;
 /* Bulk dictionary update merges and retains the old key. */
 auto*updates=new OSDictionary;auto*k2=new OSString("second");auto*v2=new OSNumber(7);assert(updates->setObject(k2,v2));k2->release();v2->release();auto*multi=new OSArray;multi->add(updates);updates->release();parse_fixture=multi;
 assert(client.metadata(9,&a)==0&&s->properties->items.size()==2);multi->release();parse_fixture=array;
 /* Readback owns a snapshot and returns the shared property seed at +4. */
 std::vector<uint8_t>req(13),reply(1024);memcpy(req.data(),&id,4);input(a,req);output(a,reply.data(),reply.size());
 assert(client.metadata(10,&a)==0&&a.structureOutputSize==s->serializedProperties->getLength()+12);
 uint32_t readseed;memcpy(&readseed,reply.data()+4,4);assert(readseed==word(s,0x20));
 assert(!memcmp(reply.data()+12,s->serializedProperties->text(),a.structureOutputSize-12));
 output(a,reply.data(),2);assert(client.metadata(10,&a)==kIOReturnNoSpace);
 /* Descriptor lengths are checked before narrowing; read faults unwind. */
 Descriptor in(p.size());memcpy(in.bytes.data(),p.data(),p.size());a.structureInputDescriptor=&in;output(a,&seed,4);
 in.length_override=(1ULL<<32)+p.size();assert(client.metadata(9,&a)==kIOReturnBadArgument);
 in.length_override=0;in.short_read=true;assert(client.metadata(9,&a)==kIOReturnBadArgument);in.short_read=false;
 assert(client.metadata(9,&a)==0);a.structureInputDescriptor=nullptr;
 /* Parser preflight rejects collection refs, oversized count and truncation,
  * accepts leaf refs, and stays bounded on every prefix/random byte stream. */
 auto cycle=packet(77,{0xd3,0x82000001,0x8c000000});input(a,cycle);assert(client.metadata(9,&a)==kIOReturnBadArgument);
 auto leafref=packet(77,{0xd3,0x82000002,0x09000004,0x00636261,0x8c000001});assert(b2l_surface_serialization_valid(leafref.data()+12,leafref.size()-12));
 auto huge=packet(77,{0xd3,0x82ffffff});assert(!b2l_surface_serialization_valid(huge.data()+12,huge.size()-12));
 for(size_t z=0;z<p.size()-12;z++)(void)b2l_surface_serialization_valid(p.data()+12,z);
 for(const char*x:{"<array ID=\"1\"></array>","<array ID = \"1\"></array>","<array><string IDREF=\"1\"/></array>","<!DOCTYPE x><array/>","<array>","<array><string>x</string></array>"}){
  bool want=!strcmp(x,"<array><string>x</string></array>");assert(b2l_surface_serialization_valid((const uint8_t*)x,strlen(x)+1)==want);
 }
 uint32_t rng=1;for(unsigned j=0;j<10000;j++){std::vector<uint8_t>bytes(4+j%257);for(auto&v:bytes){rng=rng*1664525+1013904223;v=rng>>24;}if(j%2==0){uint32_t sig=0xd3;memcpy(bytes.data(),&sig,4);} (void)b2l_surface_serialization_valid(bytes.data(),bytes.size());}
 /* The client mapping retain protects updates, then releases all metadata. */
 array->release();parse_fixture=nullptr;s->release();
 client.fRefs->items[0]->release();client.fRefs->items.clear();
 assert(live==baseline+4&&buffers==0);
 puts("IOSurface actual metadata helpers: ownership, 22 masked fields, matrix, seeds/readback, bounds, allocation/copy failure unwind PASS");
}
'''

def extract(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0]

def test_metadata():
    source = (ROOT / "xnu-overlay/iokit/Kernel/arm/B2LSurface.cpp").read_text()
    code = PREAMBLE + '\nenum {kIODirectionIn=1,kIODirectionOut=2};\n'+(ROOT/'xnu-overlay/iokit/Kernel/arm/B2LMemoryIO.h').read_text()+'\n'
    code += "static void\nput32(" + extract(source, "static void\nput32(", "static void\nput64(")
    code += "static IOReturn\nb2l_struct_out(" + extract(source, "static IOReturn\nb2l_struct_out(", "/* Map s for this client")
    code += "static B2LSurface *\nb2l_surface_held(" + extract(source, "static B2LSurface *\nb2l_surface_held(", "/* BEGIN B2L SURFACE METADATA")
    code += extract(source, "/* BEGIN B2L SURFACE METADATA", "/* END B2L SURFACE METADATA */").split("*/", 1)[1]
    code += TESTS
    with tempfile.TemporaryDirectory(prefix="b2l-surface-properties-") as temp:
        cpp = pathlib.Path(temp) / "surface.cpp"
        binary = pathlib.Path(temp) / "surface"
        cpp.write_text(code)
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g", "-O1",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

LIFETIME_PREAMBLE = r'''#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <mutex>
#include <functional>
#include <cstdio>
using IOReturn=int;using IOOptionBits=unsigned;using task_t=void*;
enum{kIOReturnSuccess=0,kIOReturnNoMemory=1,kIOReturnVMError=2,kIOReturnNoSpace=3,kIOReturnNotFound=4};
constexpr unsigned kIOMapAnywhere=1,kIOMapReadOnly=2;
constexpr unsigned B2L_SURFACE_RESULT_SIZE=0xc68,B2L_SURFACE_TIMESTAMPS=0x800;
static int live=0,maps=0,buffers=0;static bool fail_buffer=false,fail_ref=false,fail_array=false,fail_mapping=false;
static void*IOMallocZeroData(size_t n){if(fail_buffer)return nullptr;void*p=calloc(1,n);if(p)++buffers;return p;}
static void IOFreeData(void*p,size_t){if(p){--buffers;free(p);}}
class OSObject{public:unsigned refs=1;OSObject(){++live;}virtual~OSObject(){--live;}virtual bool init(){return true;}void retain(){++refs;}void release(){assert(refs);if(!--refs)delete this;}};
#define OSSafeReleaseNULL(p) do{if(p){(p)->release();p=nullptr;}}while(0)
#define OSTypeAlloc(T) (fail_ref?nullptr:new T)
class Mapping:public OSObject{public:Mapping(){++maps;}~Mapping()override{--maps;}uint64_t getAddress(){return 0x100000;}};
static std::function<void()> mapping_hook;
class Descriptor:public OSObject{public:int prepare(int){return 0;} int complete(int){return 0;}std::vector<uint8_t>bytes;bool short_write=false;explicit Descriptor(unsigned n):bytes(n){}uint64_t getLength(){return bytes.size();}uint64_t writeBytes(uint64_t off,const void*p,uint64_t n){if(short_write)return n-1;if(off>bytes.size()||n>bytes.size()-off)return 0;memcpy(bytes.data()+off,p,n);return n;}Mapping*createMappingInTask(task_t,unsigned,unsigned){if(mapping_hook){auto fn=mapping_hook;mapping_hook=nullptr;fn();}return fail_mapping?nullptr:new Mapping;}};
struct IOExternalMethodArguments{Descriptor*structureOutputDescriptor=nullptr;uint64_t structureOutputDescriptorSize=0;void*structureOutput=nullptr;uint64_t structureOutputSize=0;};
class B2LSurface:public OSObject{public:uint32_t id=77,allocSize=4096,width=16,height=16,bytesPerRow=64,offset=0,pixelFormat=0,cacheMode=0,parentID=0;uint16_t bytesPerElement=4;uint8_t elementWidth=1,elementHeight=1;unsigned mapCache=0,users=0;Descriptor*memory=new Descriptor(4096),*state=new Descriptor(4096);~B2LSurface()override{assert(users==0);memory->release();state->release();}};
static void b2l_surface_put(B2LSurface*s){assert(s->users);--s->users;}
class B2LSurfaceRef:public OSObject{public:B2LSurface*surface=nullptr;Mapping*data=nullptr,*state=nullptr;uint32_t count=0;~B2LSurfaceRef()override{OSSafeReleaseNULL(data);OSSafeReleaseNULL(state);if(surface){b2l_surface_put(surface);surface->release();}}};
class OSArray:public OSObject{public:std::vector<OSObject*>v;~OSArray()override{for(auto*p:v)p->release();}unsigned getCount(){return v.size();}OSObject*getObject(unsigned i){return v.at(i);}bool setObject(OSObject*p){if(fail_array)return false;p->retain();v.push_back(p);return true;}void removeObject(unsigned i){auto*p=v.at(i);v.erase(v.begin()+i);p->release();}};
using IOLock=std::mutex;static void IOLockLock(IOLock*p){p->lock();}static void IOLockUnlock(IOLock*p){p->unlock();}
class IOSurfaceRootUserClient{public:task_t fTask=nullptr;IOLock own;IOLock*fLock=&own;OSArray*fRefs=new OSArray;~IOSurfaceRootUserClient(){fRefs->release();}B2LSurfaceRef*findRef(uint32_t,unsigned*);IOReturn addRef(B2LSurface*,B2LSurfaceRef**,bool,bool*);IOReturn returnSurface(B2LSurface*,IOExternalMethodArguments*,bool cached=false);IOReturn trapRelease(void*,void*,void*,void*,void*,void*);};
'''
LIFETIME_TESTS = r'''static IOReturn lookup(IOSurfaceRootUserClient&c,B2LSurface*s,IOExternalMethodArguments&a,bool cached){++s->users;return c.returnSurface(s,&a,cached);}
static uint64_t base(const std::vector<uint8_t>&v){uint64_t n;memcpy(&n,v.data(),8);return n;}
static void release(IOSurfaceRootUserClient&c,B2LSurface*s){assert(c.trapRelease((void*)(uintptr_t)s->id,nullptr,nullptr,nullptr,nullptr,nullptr)==0);}
int main(){int baseline=live;
 {
 auto*s=new B2LSurface;IOSurfaceRootUserClient c;std::vector<uint8_t>out(B2L_SURFACE_RESULT_SIZE);
 IOExternalMethodArguments a;a.structureOutput=out.data();a.structureOutputSize=out.size();
 assert(lookup(c,s,a,true)==0&&base(out)==0x100000&&c.fRefs->getCount()==1&&s->users==1&&maps==2);
 auto*ref=c.findRef(77,nullptr);assert(ref->count==1&&ref->refs==1);
 int objects=live;
 assert(lookup(c,s,a,true)==0&&base(out)==UINT64_MAX&&ref->count==1&&ref->refs==1&&s->users==1&&maps==2&&live==objects);
 uint32_t id;memcpy(&id,out.data()+0x18,4);assert(id==77);
 fail_buffer=true;assert(lookup(c,s,a,true)==kIOReturnNoMemory&&ref->count==1&&s->users==1&&maps==2&&ref->refs==1);fail_buffer=false;
 a.structureOutputSize=1;assert(lookup(c,s,a,true)==kIOReturnNoSpace&&ref->count==1&&s->users==1&&maps==2);
 Descriptor desc(B2L_SURFACE_RESULT_SIZE);desc.short_write=true;a.structureOutputDescriptor=&desc;
 assert(lookup(c,s,a,true)==kIOReturnVMError&&ref->count==1&&s->users==1&&maps==2);
 a.structureOutputDescriptor=nullptr;a.structureOutputSize=out.size();
 /* Normal ID lookup retains the native kernel count and normal reply. */
 assert(lookup(c,s,a,false)==0&&base(out)==0x100000&&ref->count==2&&s->users==1);
 fail_buffer=true;assert(lookup(c,s,a,false)==kIOReturnNoMemory&&ref->count==2&&ref->refs==1);fail_buffer=false;
 release(c,s);assert(ref->count==1&&maps==2);release(c,s);assert(c.fRefs->getCount()==0&&s->users==0&&maps==0);s->release();
 }
 assert(live==baseline&&buffers==0&&maps==0);
 /* A second lookup can publish the mapping while the first maps pixels. */
 for(bool cached:{false,true})for(bool fault:{false,true}){
 auto*s=new B2LSurface;IOSurfaceRootUserClient c;std::vector<uint8_t>out(B2L_SURFACE_RESULT_SIZE),innerout(B2L_SURFACE_RESULT_SIZE);
 IOExternalMethodArguments a,inner;a.structureOutput=out.data();a.structureOutputSize=out.size();inner.structureOutput=innerout.data();inner.structureOutputSize=innerout.size();
 mapping_hook=[&]{assert(lookup(c,s,inner,true)==0);if(fault)fail_buffer=true;};
 int ret=lookup(c,s,a,cached);fail_buffer=false;
 auto*ref=c.findRef(77,nullptr);assert(ref&&ref->refs==1&&s->users==1&&maps==2);
 assert(ret==(fault?kIOReturnNoMemory:0));
 assert(ref->count==((cached||fault)?1:2));
 if(!fault)assert(base(out)==(cached?UINT64_MAX:0x100000));
 while(c.fRefs->getCount())release(c,s);s->release();
 assert(live==baseline+1); /* c's empty array remains until iteration end */
 }
 assert(live==baseline&&buffers==0&&maps==0);
 /* Failed first reply, allocation, mapping or array publication drops only
  * its newly acquired map/user count and leaves no retained objects. */
 for(unsigned mode=0;mode<5;mode++){
 auto*s=new B2LSurface;IOSurfaceRootUserClient c;std::vector<uint8_t>out(B2L_SURFACE_RESULT_SIZE);
 IOExternalMethodArguments a;a.structureOutput=out.data();a.structureOutputSize=mode==0?1:out.size();
 fail_buffer=mode==1;fail_ref=mode==2;fail_mapping=mode==3;fail_array=mode==4;
 assert(lookup(c,s,a,true)!=0);fail_buffer=fail_ref=fail_mapping=fail_array=false;
 assert(c.fRefs->getCount()==0&&s->users==0&&s->refs==1&&maps==0&&buffers==0);s->release();
 }
 assert(live==baseline&&buffers==0&&maps==0);
 puts("IOSurface actual lookup lifetime: existing/raced sentinel, unchanged cached map count, normal lookup count and failure unwind PASS");
}
'''

def test_lookup_lifetime():
    source = (ROOT / "xnu-overlay/iokit/Kernel/arm/B2LSurface.cpp").read_text()
    code = LIFETIME_PREAMBLE + '\nenum {kIODirectionIn=1,kIODirectionOut=2};\n'+(ROOT/'xnu-overlay/iokit/Kernel/arm/B2LMemoryIO.h').read_text()+'\n'
    code += "B2LSurfaceRef *\nIOSurfaceRootUserClient::findRef(" + extract(source, "B2LSurfaceRef *\nIOSurfaceRootUserClient::findRef(", "IOReturn\nIOSurfaceRootUserClient::create(")
    code += "IOReturn\nIOSurfaceRootUserClient::trapRelease(" + extract(source, "IOReturn\nIOSurfaceRootUserClient::trapRelease(", "static IOExternalTrap b2l_surface_traps[]")
    code += LIFETIME_TESTS
    with tempfile.TemporaryDirectory(prefix="b2l-surface-lifetime-") as temp:
        cpp = pathlib.Path(temp) / "surface.cpp"
        binary = pathlib.Path(temp) / "surface"
        cpp.write_text(code)
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g", "-O1",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

if __name__ == "__main__":
    test_metadata()
    test_lookup_lifetime()
