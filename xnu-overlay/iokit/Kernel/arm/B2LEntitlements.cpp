/* SPDX-License-Identifier: BSD-2-Clause
 * Unauthenticated development metadata, never a validated code signature.
 * XML decoding uses XNU's public OSUnserializeXML with bounded preflight.
 */
#ifndef B2L_AMFI_HOST_TEST
#include <IOKit/IOLib.h>
#include <sys/errno.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSUnserialize.h>
#endif
#include "../../../bsd/kern/b2l_amfi.h"
#include "../../../bsd/kern/b2l_amfi_metadata.h"
class B2LMetadataEntitlements : public OSObject {
    OSDeclareDefaultStructors(B2LMetadataEntitlements);
public:
    OSDictionary *dict;
    void *xml;
    size_t xml_len;
    void *der;
    size_t der_len;
    bool platform;
    uint8_t cdhash[20],cdhash_type;
    char *identity;
    size_t identity_len;
    bool init(void) APPLE_KEXT_OVERRIDE;
    void free(void) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(B2LMetadataEntitlements, OSObject);
bool B2LMetadataEntitlements::init(void)
{
    dict=nullptr;xml=nullptr;xml_len=0;der=nullptr;der_len=0;platform=false;identity=nullptr;identity_len=0;
    memset(cdhash,0,sizeof(cdhash));cdhash_type=0;
    return OSObject::init();
}
void B2LMetadataEntitlements::free(void)
{
    OSSafeReleaseNULL(dict);
    if(xml)kfree_data(xml,xml_len);
    if(der)kfree_data(der,der_len);
    if(identity)kfree_data(identity,identity_len);
    OSObject::free();
}
static B2LMetadataEntitlements *ent(const void*p)
{return OSDynamicCast(B2LMetadataEntitlements,(OSObject*)(uintptr_t)p);}
/* Reject aliases/cycles, internal DTDs, embedded NULs, >32 container depth or
 * >4096 tags before the allocating XML parser. Normal XML plist declarations
 * and the conventional PUBLIC DOCTYPE are accepted; no DTD is evaluated. */
static bool xml_bounded(const char*s,size_t n)
{
    unsigned depth=0,tags=0;
    for(size_t i=0;i<n;i++){
        if(!s[i])return false;
        if(s[i]!='<')continue;
        size_t end=i+1;char quote=0;
        for(;end<n;end++){
            char c=s[end];if(!c)return false;
            if(quote){if(c==quote)quote=0;continue;}
            if(c=='\''||c=='"'){quote=c;continue;}
            if(c=='>')break;
            if(c=='[')return false;
        }
        if(end==n||++tags>4096)return false;
        const char *t=s+i+1;size_t z=end-i-1;
        if(!z)return false;
        if(t[0]=='?'||t[0]=='!'){i=end;continue;}
        bool closing=t[0]=='/';size_t k=closing?1:0,start=k;
        while(k<z&&t[k]!=' '&&t[k]!='\t'&&t[k]!='\n'&&t[k]!='/'&&t[k]!='\r')k++;
        size_t name=k-start;
        bool plist=name==5&&!memcmp(t+start,"plist",5);
        bool container=plist||(name==4&&!memcmp(t+start,"dict",4))||(name==5&&!memcmp(t+start,"array",5));
        /* Attributes permit XML alias IDs. Only the plist version attribute
         * is needed in signed entitlement plists; reject all others. */
        while(k<z&&(t[k]==' '||t[k]=='\t'||t[k]=='\n'||t[k]=='\r'))k++;
        if(k<z&&t[k]!='/'&&!plist)return false;
        if(plist&&k<z&&t[k]!='/'&&
           !(z-k==13&&!memcmp(t+k,"version=\"1.0\"",13)))return false;
        bool self=z&&t[z-1]=='/';
        if(container){if(closing){if(!depth)return false;--depth;}else if(!self&&++depth>32)return false;}
        i=end;
    }
    return depth==0;
}
extern "C" {
void *b2l_ent_create_metadata(const void*blob,size_t length,bool platform,const void*identifier,size_t identifier_len)
{
    if(identifier){
        if(identifier_len<2||identifier_len>B2L_AMFI_IDENTITY_MAX)return nullptr;
        const char*text=(const char*)identifier;
        for(size_t i=0;i<identifier_len;i++)if((text[i]==0)!=(i==identifier_len-1))return nullptr;
    }else if(identifier_len)return nullptr;
    OSDictionary *dict=nullptr;
    if(blob){
        if(length<=8||length>B2L_AMFI_XML_MAX)return nullptr;
        const uint8_t*b=(const uint8_t*)blob;
        if(OSReadBigInt32(b,0)!=0xfade7171u||OSReadBigInt32(b,4)!=length)return nullptr;
        size_t chars=length-8;while(chars&&b[8+chars-1]==0)--chars;
        if(!chars||!xml_bounded((const char*)b+8,chars))return nullptr;
        char*text=(char*)kalloc_data(chars+1,Z_WAITOK);
        if(!text)return nullptr;
        memcpy(text,b+8,chars);text[chars]=0;
        auto parsed=OSUnserializeXML(text,chars+1);
        kfree_data(text,chars+1);
#if defined(IOKIT_ENABLE_SHARED_PTR)
        dict=OSDynamicCast(OSDictionary,parsed.get());
        if(dict)dict->retain();
#else
        dict=OSDynamicCast(OSDictionary,parsed);
        if(dict)dict->retain();
        OSSafeReleaseNULL(parsed);
#endif
        if(!dict)return nullptr;
    }else{
        auto empty=OSDictionary::withCapacity(1);
#if defined(IOKIT_ENABLE_SHARED_PTR)
        dict=empty.detach();
#else
        dict=empty;
#endif
        if(!dict)return nullptr;
    }
    B2LMetadataEntitlements*object=OSTypeAlloc(B2LMetadataEntitlements);
    if(!object||!object->init()){OSSafeReleaseNULL(object);dict->release();return nullptr;}
    object->dict=dict;object->platform=platform;
    /* Recursively freezes child collections, preventing retained result
     * objects from mutating subsequent privilege decisions. */
    dict->setOptions(OSCollection::kImmutable,OSCollection::kImmutable);
    if(blob){
        object->xml=kalloc_data(length,Z_WAITOK);
        if(!object->xml){object->release();return nullptr;}
        memcpy(object->xml,blob,length);object->xml_len=length;
    }
    if(identifier){
        object->identity=(char*)kalloc_data(identifier_len,Z_WAITOK);
        if(!object->identity){object->release();return nullptr;}
        memcpy(object->identity,identifier,identifier_len);object->identity_len=identifier_len;
    }
    return object;
}
void *b2l_ent_create(const void*blob,size_t length,bool platform)
{return b2l_ent_create_metadata(blob,length,platform,nullptr,0);}
int b2l_ent_copy_identity(const void*p,void**out,size_t*length)
{
    if(out)*out=nullptr;if(length)*length=0;
    if(!out||!length)return EINVAL;
    auto e=ent(p);if(!e||!e->identity)return ENOENT;
    void*copy=kalloc_data(e->identity_len,Z_WAITOK);if(!copy)return ENOMEM;
    memcpy(copy,e->identity,e->identity_len);*out=copy;*length=e->identity_len;return 0;
}
bool b2l_ent_attach_cdhash(void*p,const uint8_t hash[20],uint8_t type)
{
 auto e=ent(p);if(!e||!hash||e->cdhash_type||(type!=2&&type!=3))return false;
 memcpy(e->cdhash,hash,20);e->cdhash_type=type;return true;
}
int b2l_ent_copy_cdhash(const void*p,uint8_t out[20],uint8_t*type)
{
 if(out)memset(out,0,20);if(type)*type=0;if(!out||!type)return EINVAL;
 auto e=ent(p);if(!e||!e->cdhash_type)return ENOENT;
 memcpy(out,e->cdhash,20);*type=e->cdhash_type;return 0;
}
/* Called only while constructing an unpublished, exclusively owned label. */
bool b2l_ent_attach_der(void*p,const void*blob,size_t length)
{
    auto e=ent(p);if(!e||e->der||!blob||length<=8||length>B2L_AMFI_DER_MAX)return false;
    if(OSReadBigInt32(blob,0)!=0xfade7172u||OSReadBigInt32(blob,4)!=length)return false;
    void*copy=kalloc_data(length,Z_WAITOK);if(!copy)return false;
    memcpy(copy,blob,length);e->der=copy;e->der_len=length;return true;
}
bool b2l_ent_copy_der(const void*p,void**out,size_t*length)
{
    if(out)*out=nullptr;if(length)*length=0;
    auto e=ent(p);if(!out||!length||!e||!e->der)return false;
    void*copy=kalloc_data(e->der_len,Z_WAITOK);if(!copy)return false;
    memcpy(copy,e->der,e->der_len);*out=copy;*length=e->der_len;return true;
}
void b2l_ent_retain(void*p){if(p)((OSObject*)p)->retain();}
void b2l_ent_release(void*p){if(p)((OSObject*)p)->release();}
bool b2l_ent_platform(const void*p){auto e=ent(p);return e&&e->platform;}
bool b2l_ent_bool(const void*p,const char*n){auto e=ent(p);return e&&n&&e->dict->getObject(n)==kOSBooleanTrue;}
bool b2l_ent_string(const void*p,const char*n,const char*v)
{
    auto e=ent(p);if(!e||!n||!v)return false;
    OSObject*o=e->dict->getObject(n);auto s=OSDynamicCast(OSString,o);
    if(s)return s->isEqualTo(v);
    auto a=OSDynamicCast(OSArray,o);
    for(unsigned i=0;a&&i<a->getCount();i++){s=OSDynamicCast(OSString,a->getObject(i));if(s&&s->isEqualTo(v))return true;}
    return false;
}
void *b2l_ent_copy_object(const void*p,const char*n)
{auto e=ent(p);OSObject*o=e&&n?e->dict->getObject(n):nullptr;if(o)o->retain();return o;}
void *b2l_ent_copy_dict(const void*p)
{auto e=ent(p);if(!e)return nullptr;e->dict->retain();return e->dict;}
bool b2l_ent_copy_xml(const void*p,void**out,size_t*length)
{
    if(out)*out=nullptr;if(length)*length=0;
    auto e=ent(p);if(!out||!length||!e||!e->xml)return false;
    void*copy=kalloc_data(e->xml_len,Z_WAITOK);if(!copy)return false;
    memcpy(copy,e->xml,e->xml_len);*out=copy;*length=e->xml_len;return true;
}
}
