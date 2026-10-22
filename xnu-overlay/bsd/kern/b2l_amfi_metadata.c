/* SPDX-License-Identifier: BSD-2-Clause
 * Bounded unsigned Mach-O XML metadata reader. Independent of cs_blob and
 * CodeDirectory validation; no input byte is treated as authenticated. */
#include "b2l_amfi_metadata.h"
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[3]<<24|(uint32_t)p[2]<<16|(uint32_t)p[1]<<8|p[0];}
static uint64_t be64(const uint8_t *p){return (uint64_t)be32(p)<<32|be32(p+4);}
static int range(uint64_t at,uint64_t n,uint64_t end){return at<=end&&n<=end-at;}
static int read_at(const struct b2l_amfi_reader*r,uint64_t at,void*out,size_t n){return !r||!r->read||!range(at,n,r->bytes)||r->read(r->opaque,at,out,n);}
struct b2l_amfi_locations { uint64_t signature; uint32_t xml_at,xml_size,der_at,der_size,cd_at,cd_size; };
static enum b2l_amfi_metadata_result
find_metadata(const struct b2l_amfi_reader*r,uint64_t base,struct b2l_amfi_locations*locations)
{
    uint8_t h[32],cmd[16],idx[8],bh[8];uint64_t end;uint32_t csat=0,cssize=0;
    *locations=(struct b2l_amfi_locations){0};
    if(!r||!r->read||!range(base,32,r->bytes))return B2L_AMFI_XML_INVALID;
    end=r->bytes;
    if(base){
        if(read_at(r,0,h,8))return B2L_AMFI_XML_IO;
        uint32_t magic=be32(h),count=be32(h+4);int wide=magic==0xcafebabfu;
        if((magic!=0xcafebabeu&&!wide)||!count||count>64)return B2L_AMFI_XML_INVALID;
        unsigned size=wide?32:20;unsigned found=0;
        if(!range(8,(uint64_t)size*count,r->bytes))return B2L_AMFI_XML_INVALID;
        for(uint32_t i=0;i<count;i++){
            if(read_at(r,8+(uint64_t)i*size,h,size))return B2L_AMFI_XML_IO;
            uint64_t offset=wide?be64(h+8):be32(h+8),bytes=wide?be64(h+16):be32(h+12);
            if(!range(offset,bytes,r->bytes)||!bytes)return B2L_AMFI_XML_INVALID;
            if(offset==base){if(++found!=1||be32(h)!=0x0100000cu)return B2L_AMFI_XML_INVALID;end=offset+bytes;}
        }
        if(found!=1||!range(base,32,end))return B2L_AMFI_XML_INVALID;
    }
    if(read_at(r,base,h,32))return B2L_AMFI_XML_IO;
    if(le32(h)!=0xfeedfacfu||le32(h+4)!=0x0100000cu||le32(h+12)!=2)return B2L_AMFI_XML_INVALID;
    uint32_t count=le32(h+16),bytes=le32(h+20);
    if(count>4096||bytes>65536||!range(base+32,bytes,end)||count>bytes/8)return B2L_AMFI_XML_INVALID;
    uint64_t at=base+32,commands_end=at+bytes;
    for(uint32_t i=0;i<count;i++){
        if(!range(at,8,commands_end))return B2L_AMFI_XML_INVALID;
        if(read_at(r,at,cmd,8))return B2L_AMFI_XML_IO;
        uint32_t kind=le32(cmd),n=le32(cmd+4);
        if(n<8||(n&7)||!range(at,n,commands_end))return B2L_AMFI_XML_INVALID;
        if(kind==0x1d){
            if(csat||cssize||n!=16)return B2L_AMFI_XML_INVALID;
            if(read_at(r,at,cmd,16))return B2L_AMFI_XML_IO;
            csat=le32(cmd+8);cssize=le32(cmd+12);
            if(cssize<12||cssize>B2L_AMFI_SIGNATURE_MAX||csat<32+bytes||!range(csat,cssize,end-base))return B2L_AMFI_XML_INVALID;
        }
        at+=n;
    }
    if(at!=commands_end)return B2L_AMFI_XML_INVALID;
    if(!csat)return B2L_AMFI_XML_ABSENT;
    uint64_t sig=base+csat;
    if(read_at(r,sig,h,12))return B2L_AMFI_XML_IO;
    uint32_t total=be32(h+4),entries=be32(h+8);
    if(be32(h)!=0xfade0cc0u||total>cssize||total<12||entries>64||entries>(total-12)/8)return B2L_AMFI_XML_INVALID;
    uint32_t starts[64],sizes[64],xmlat=0,xmlsize=0,derat=0,dersize=0,cdat=0,cdsize=0;
    for(uint32_t i=0;i<entries;i++){
        if(read_at(r,sig+12+8u*i,idx,8))return B2L_AMFI_XML_IO;
        uint32_t type=be32(idx),off=be32(idx+4);
        if(off<12+8u*entries||!range(off,8,total))return B2L_AMFI_XML_INVALID;
        if(read_at(r,sig+off,bh,8))return B2L_AMFI_XML_IO;
        uint32_t n=be32(bh+4);
        if(n<8||!range(off,n,total))return B2L_AMFI_XML_INVALID;
        for(uint32_t j=0;j<i;j++)if(off<starts[j]+sizes[j]&&starts[j]<off+n)return B2L_AMFI_XML_INVALID;
        starts[i]=off;sizes[i]=n;
        if(type==0){if(cdat)return B2L_AMFI_XML_INVALID;cdat=off;cdsize=n;}
        if(type==5){if(xmlat||be32(bh)!=0xfade7171u||n<=8||n>B2L_AMFI_XML_MAX)return B2L_AMFI_XML_INVALID;xmlat=off;xmlsize=n;}
        if(type==7){if(derat||be32(bh)!=0xfade7172u||n<=8||n>B2L_AMFI_DER_MAX)return B2L_AMFI_XML_INVALID;derat=off;dersize=n;}
    }
    locations->signature=sig;locations->xml_at=xmlat;locations->xml_size=xmlsize;
    locations->der_at=derat;locations->der_size=dersize;
    locations->cd_at=cdat;locations->cd_size=cdsize;
    return B2L_AMFI_XML_PRESENT;
}
enum b2l_amfi_metadata_result
b2l_amfi_read_xml(const struct b2l_amfi_reader*r,uint64_t base,void*out,size_t capacity,size_t*length)
{
    if(length)*length=0;
    if(!out||!length)return B2L_AMFI_XML_INVALID;
    struct b2l_amfi_locations where;
    enum b2l_amfi_metadata_result result=find_metadata(r,base,&where);
    if(result!=B2L_AMFI_XML_PRESENT)return result;
    if(!where.xml_at)return B2L_AMFI_XML_ABSENT;
    if(where.xml_size>capacity)return B2L_AMFI_XML_INVALID;
    if(read_at(r,where.signature+where.xml_at,out,where.xml_size))return B2L_AMFI_XML_IO;
    *length=where.xml_size;return B2L_AMFI_XML_PRESENT;
}
enum b2l_amfi_metadata_result
b2l_amfi_read_identity(const struct b2l_amfi_reader*r,uint64_t base,void*out,size_t capacity,size_t*length)
{
    if(length)*length=0;
    if(out&&capacity)((char*)out)[0]=0;
    if(!out||!length||!capacity)return B2L_AMFI_XML_INVALID;
    struct b2l_amfi_locations where;
    enum b2l_amfi_metadata_result result=find_metadata(r,base,&where);
    if(result!=B2L_AMFI_XML_PRESENT)return result;
    if(!where.cd_at)return B2L_AMFI_XML_ABSENT;
    uint8_t header[24];
    if(where.cd_size<44)return B2L_AMFI_XML_INVALID;
    if(read_at(r,where.signature+where.cd_at,header,sizeof(header)))return B2L_AMFI_XML_IO;
    if(be32(header)!=0xfade0c02u)return B2L_AMFI_XML_INVALID;
    uint32_t version=be32(header+8),offset=be32(header+20),minimum=44;
    /* Pinned public CS_CodeDirectory versioned header sizes. Future layouts
     * are rejected; the identifier must not alias any fixed header field. */
    if(version<0x20000u||version>0x20600u)return B2L_AMFI_XML_INVALID;
    if(version>=0x20100u)minimum=48;
    if(version>=0x20200u)minimum=52;
    if(version>=0x20300u)minimum=64;
    if(version>=0x20400u)minimum=88;
    if(version>=0x20500u)minimum=96;
    if(version>=0x20600u)minimum=108;
    if(where.cd_size<minimum||offset<minimum||offset>=where.cd_size)return B2L_AMFI_XML_INVALID;
    size_t limit=where.cd_size-offset;
    if(limit>B2L_AMFI_IDENTITY_MAX)limit=B2L_AMFI_IDENTITY_MAX;
    if(limit>capacity)limit=capacity;
    /* Stack staging keeps caller's output empty on IO/malformed failures. */
    uint8_t text[B2L_AMFI_IDENTITY_MAX];
    if(read_at(r,where.signature+where.cd_at+offset,text,limit))return B2L_AMFI_XML_IO;
    for(size_t i=0;i<limit;i++)if(!text[i]){
        if(i==0)return B2L_AMFI_XML_INVALID;
        for(size_t j=0;j<=i;j++)((uint8_t*)out)[j]=text[j];
        *length=i+1;return B2L_AMFI_XML_PRESENT;
    }
    return B2L_AMFI_XML_INVALID;
}

enum b2l_amfi_metadata_result
b2l_amfi_read_der(const struct b2l_amfi_reader*r,uint64_t base,void*out,size_t capacity,size_t*length)
{
    if(length)*length=0;
    if(!out||!length)return B2L_AMFI_XML_INVALID;
    struct b2l_amfi_locations where;
    enum b2l_amfi_metadata_result result=find_metadata(r,base,&where);
    if(result!=B2L_AMFI_XML_PRESENT)return result;
    if(!where.der_at)return B2L_AMFI_XML_ABSENT;
    if(where.der_size>capacity)return B2L_AMFI_XML_INVALID;
    if(read_at(r,where.signature+where.der_at,out,where.der_size))return B2L_AMFI_XML_IO;
    *length=where.der_size;return B2L_AMFI_XML_PRESENT;
}
