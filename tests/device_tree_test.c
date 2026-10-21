/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../uefi/XnuLoader/DeviceTree.h"
static unsigned seed_properties;
static uint32_t read32(const unsigned char *p) { uint32_t n; memcpy(&n,p,4); return n; }
static size_t decode(const unsigned char *buf, size_t size, size_t off,
                     unsigned depth, unsigned *nodes, unsigned *properties)
{
    assert(depth < 8 && off + 8 <= size);
    uint32_t nprops = read32(buf+off), children = read32(buf+off+4);
    off += 8; ++*nodes;
    const char *node_name = 0;
    assert(nprops && nprops < 64 && children < 8);
    for (uint32_t i=0; i<nprops; ++i) {
        assert(off + 36 <= size && memchr(buf+off, 0, 32));
        uint32_t len=read32(buf+off+32);
        const char *name=(const char *)buf+off;
        off += 36; assert(len <= size-off);
        if (!strcmp(name,"name")) { assert(len && buf[off+len-1]==0); node_name=(const char *)buf+off; }
        assert(strcmp(name,"clock-frequency") && strcmp(name,"bus-frequency"));
        if (!strcmp(name,"reg") && node_name && !strcmp(node_name,"framebuffer")) {
            uint64_t pa; memcpy(&pa,buf+off,8); assert(depth==1 && len==16 && pa==0xca000000);
        }
        if (!strcmp(name,"random-seed")) { ++seed_properties; assert(len==256); for(unsigned j=0;j<256;++j)assert(buf[off+j]==(unsigned char)(j+1)); }
        if (!strcmp(name,"timebase-frequency")) assert(len==4 && read32(buf+off)==26000000);
        if (!strcmp(name,"state")) assert(len==8 && !memcmp(buf+off,"running",8));
        if (!strcmp(name,"device_type")) assert(len && buf[off+len-1]==0);
        if (!strcmp(name,"cluster-type")) assert(len==4 && read32(buf+off)=='S');
        if (!strcmp(name,"reg") && len==4) assert(read32(buf+off)==0x100);
        off += (len + 3) & ~3U; ++*properties;
    }
    for (uint32_t i=0; i<children; ++i) off=decode(buf,size,off,depth+1,nodes,properties);
    return off;
}
int main(void)
{
    unsigned char buf[8192], small[8192];
    struct b2l_dt_config c={0}; size_t used=0;
    c.memory_base=0x900000000ULL; c.memory_size=512ULL*1024*1024;
    c.mpidr=0x80000100; c.counter_frequency=26000000;
    c.video.base=0xca000000; c.video.row_bytes=1440*4; c.video.width=1440; c.video.height=3040;
    _Static_assert(B2L_SEED_BYTES == 256, "XNU PRNG seed length");
    for(unsigned i=0;i<B2L_SEED_BYTES;++i)c.random_seed[i]=(unsigned char)(i+1);
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)==0);
    unsigned nodes=0, props=0;
    assert(decode(buf,used,0,0,&nodes,&props)==used);
    assert(nodes==9 && props>40 && seed_properties==1);
    for(size_t capacity=0;capacity<used;++capacity) {
        size_t ignored=0; memset(small,0x5a,sizeof(small));
        assert(b2l_build_device_tree(small,capacity,&c,&ignored)<0);
        for(size_t j=capacity;j<sizeof(small);++j)assert(small[j]==0x5a);
    }
    assert(b2l_build_device_tree(small,used,&c,&used)==0);
    assert(!memcmp(buf,small,used));
    c.counter_frequency=0;
    assert(b2l_build_device_tree(buf,sizeof(buf),&c,&used)<0);
    assert(sizeof(struct b2l_boot_args)==1152);
    assert(offsetof(struct b2l_boot_args,device_tree)==96);
    assert(offsetof(struct b2l_boot_args,flags)==1136);
    puts("Apple DT capacity, topology, entropy, and boot_args ABI tests passed");
    return 0;
}
