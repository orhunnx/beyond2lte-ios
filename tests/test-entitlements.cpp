#define B2L_AMFI_HOST_TEST 1
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#define APPLE_KEXT_OVERRIDE override
#define Z_WAITOK 0
#define OSDeclareDefaultStructors(T) public: T()=default; ~T() override=default;
#define OSDefineMetaClassAndStructors(T,B)
#define OSTypeAlloc(T) new T
#define OSDynamicCast(T,p) dynamic_cast<T*>(p)
#define OSSafeReleaseNULL(p) do {if(p){(p)->release();p=nullptr;}}while(0)
static int live_objects,live_buffers;
static bool allocation_fail;
static void*kalloc_data(size_t n,int){if(allocation_fail)return nullptr;void*p=malloc(n);if(p)++live_buffers;return p;}
static void kfree_data(void*p,size_t){if(p){--live_buffers;free(p);}}
static uint32_t OSReadBigInt32(const void*p,size_t o){const auto*b=(const uint8_t*)p+o;return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}
class OSObject{public:unsigned refs=1;OSObject(){++live_objects;}virtual~OSObject(){--live_objects;}virtual bool init(){return true;}virtual void free(){}void retain(){++refs;}void release(){assert(refs);if(!--refs){free();delete this;}}};
class OSCollection:public OSObject{public:enum{kImmutable=1};unsigned options=0;virtual unsigned setOptions(unsigned o,unsigned m){unsigned old=options;options=(old&~m)|(o&m);return old;}};
class OSBoolean:public OSObject{};
static OSBoolean true_object,false_object;
static OSBoolean*kOSBooleanTrue=&true_object;
class OSString:public OSObject{public:std::string value;OSString(const char*s):value(s){}bool isEqualTo(const char*s){return value==s;}};
class OSArray:public OSCollection{public:std::vector<OSObject*>v;void add(OSObject*p){p->retain();v.push_back(p);}unsigned getCount(){return (unsigned)v.size();}OSObject*getObject(unsigned i){return v.at(i);}void free()override{for(auto*p:v)p->release();}unsigned setOptions(unsigned o,unsigned m)override{unsigned old=OSCollection::setOptions(o,m);for(auto*p:v)if(auto*c=dynamic_cast<OSCollection*>(p))c->setOptions(o,m);return old;}};
class OSDictionary:public OSCollection{public:std::map<std::string,OSObject*>v;static OSDictionary*withCapacity(unsigned){return new OSDictionary;}void set(const char*n,OSObject*p){p->retain();v[n]=p;}OSObject*getObject(const char*n){auto i=v.find(n);return i==v.end()?nullptr:i->second;}void free()override{for(auto&i:v)i.second->release();}unsigned setOptions(unsigned o,unsigned m)override{unsigned old=OSCollection::setOptions(o,m);for(auto&i:v)if(auto*c=dynamic_cast<OSCollection*>(i.second))c->setOptions(o,m);return old;}};
/* Kernel XML parser is independently maintained by XNU. This fixture factory
 * only supplies OSObject types; actual adapter preflight/ownership/queries run. */
static OSObject*OSUnserializeXML(const char*s,size_t){if(strstr(s,"BAD_DICT"))return new OSString("wrong type");auto*d=new OSDictionary;d->set("yes",&true_object);d->set("no",&false_object);auto*t=new OSString("hello");d->set("string",t);t->release();auto*a=new OSArray;auto*e=new OSString("member");a->add(e);e->release();d->set("array",a);a->release();return d;}
#include "../xnu-overlay/iokit/Kernel/arm/B2LEntitlements.cpp"
static std::vector<uint8_t>blob(const std::string&text){std::vector<uint8_t>b(8+text.size());b[0]=0xfa;b[1]=0xde;b[2]=0x71;b[3]=0x71;uint32_t z=(uint32_t)b.size();for(unsigned i=0;i<4;i++)b[7-i]=(uint8_t)(z>>(8*i));memcpy(b.data()+8,text.data(),text.size());return b;}
int main(){int base=live_objects;auto b=blob("<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>yes</key><true/></dict></plist>");void*p=b2l_ent_create(b.data(),b.size(),true);assert(p&&b2l_ent_platform(p));assert(b2l_ent_bool(p,"yes")&&!b2l_ent_bool(p,"no")&&!b2l_ent_bool(p,"array")&&!b2l_ent_bool(p,"missing"));assert(b2l_ent_string(p,"string","hello")&&b2l_ent_string(p,"array","member")&&!b2l_ent_string(p,"array","missing")&&!b2l_ent_string(p,"yes","true"));auto*a=(OSArray*)b2l_ent_copy_object(p,"array");assert(a&&a->refs==2&&(a->options&OSCollection::kImmutable));b2l_ent_release(p);assert(a->refs==1);a->release();assert(live_objects==base&&live_buffers==0);
 p=b2l_ent_create(b.data(),b.size(),true);void*copy=nullptr;size_t n=0;assert(b2l_ent_copy_xml(p,&copy,&n)&&n==b.size()&&!memcmp(copy,b.data(),n));kfree_data(copy,n);auto*d=(OSDictionary*)b2l_ent_copy_dict(p);assert(d&&(d->options&1));b2l_ent_release(p);d->release();assert(live_objects==base&&live_buffers==0);
 p=b2l_ent_create(nullptr,0,true);assert(p&&b2l_ent_platform(p)&&!b2l_ent_bool(p,"yes"));copy=(void*)1;n=1;assert(!b2l_ent_copy_xml(p,&copy,&n)&&copy==nullptr&&n==0);b2l_ent_release(p);
 for(auto text:{"<dict ID=\"1\"></dict>","<dict><array IDREF=\"1\"/></dict>","<!DOCTYPE plist [<!ENTITY x 'x'>]><dict/>","<dict>BAD_DICT</dict>"}){auto bad=blob(text);assert(!b2l_ent_create(bad.data(),bad.size(),true));}
 std::string deep;for(unsigned i=0;i<33;i++)deep+="<array>";for(unsigned i=0;i<33;i++)deep+="</array>";auto bad=blob(deep);assert(!b2l_ent_create(bad.data(),bad.size(),true));
 allocation_fail=true;assert(!b2l_ent_create(b.data(),b.size(),true));allocation_fail=false;assert(live_objects==base&&live_buffers==0);
 const char identity[]="example.fixture";p=b2l_ent_create_metadata(nullptr,0,true,identity,sizeof(identity));assert(p);
 copy=(void*)1;n=99;assert(!b2l_ent_copy_identity(p,&copy,&n)&&n==sizeof(identity)&&!memcmp(copy,identity,n));
 b2l_ent_release(p);assert(!memcmp(copy,identity,n));kfree_data(copy,n);
 p=b2l_ent_create(nullptr,0,true);copy=(void*)1;n=1;assert(b2l_ent_copy_identity(p,&copy,&n)==ENOENT&&copy==nullptr&&!n);b2l_ent_release(p);
 assert(!b2l_ent_create_metadata(nullptr,0,true,"",1));assert(!b2l_ent_create_metadata(nullptr,0,true,"a\0b",4));
 p=b2l_ent_create_metadata(nullptr,0,true,identity,sizeof(identity));allocation_fail=true;copy=(void*)1;n=1;assert(b2l_ent_copy_identity(p,&copy,&n)==ENOMEM&&!copy&&!n);allocation_fail=false;b2l_ent_release(p);
 assert(live_objects==base&&live_buffers==0);
 puts("Actual C++ metadata adapter: typed queries, retained copies, immutable nesting, bounded XML, failure cleanup PASS");return 0;}
