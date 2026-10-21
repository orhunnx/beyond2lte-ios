#include "b2l_compat.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "fixtures.h"

struct memory { unsigned calls, acquire_calls, pc_calls, generic_calls, width;
    int fail, fail_pc, fail_generic; uint64_t address, value, arg_a, arg_b; };
static int pc(uint64_t a, void *v) {
    struct memory *m=v; m->pc_calls++;
    return m->fail_pc || a < 0x1000 || a >= UINT64_C(0x100000000) || (a&3);
}
static int read_mem(uint64_t a,unsigned w,uint64_t *out,void *v) {
    struct memory *m=v; m->calls++;m->address=a;m->width=w;
    if (m->fail || a<0x1000 || a>UINT64_C(0xffffffff)-w+1) return 1;
    *out=m->value;return 0;
}
static int acquire_mem(uint64_t a,unsigned w,uint64_t *out,void *v) {
    struct memory *m=v;m->acquire_calls++;return read_mem(a,w,out,v);
}
static int pacga(uint64_t a,uint64_t b,uint64_t *out,void *v) {
    struct memory *m=v;m->generic_calls++;m->arg_a=a;m->arg_b=b;
    if(m->fail_generic)return 1;
    /* A deterministic callback fixture, not production QARMA. */
    *out=UINT64_C(0x12345678ffffffff);return 0;
}
static struct b2l_compat_ops ops(struct memory *m) {
    struct b2l_compat_ops o={B2L_COMPAT_MODE_STRIPPED,read_mem,acquire_mem,pc,pacga,m};return o;
}
static struct b2l_compat_ctx ctx(void) {
    struct b2l_compat_ctx c;memset(&c,0,sizeof(c));
    for(unsigned i=0;i<31;i++)c.x[i]=UINT64_C(0x2000)+16*i;
    c.pc=0x1000;c.sp=0x8000;c.cpsr=UINT32_C(0xf0000000);return c;
}
static uint32_t word(const char *n) {
    for(size_t i=0;i<sizeof(fixtures)/sizeof(*fixtures);i++)if(!strcmp(n,fixtures[i].name))return fixtures[i].word;
    assert(!"missing fixture");return 0;
}
static void same(const struct b2l_compat_ctx *a,const struct b2l_compat_ctx *b) {
    assert(!memcmp(a->x,b->x,sizeof(a->x)));assert(a->sp==b->sp);assert(a->pc==b->pc);assert(a->cpsr==b->cpsr);
}
static uint64_t rng=UINT64_C(0x9876543210123456);
static uint64_t random64(void) {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
static void fault(uint32_t i,struct b2l_compat_ctx c,struct b2l_compat_ops *o) {
    struct b2l_compat_ctx before=c;
    assert(b2l_compat_emulate(i,&c,o)==B2L_COMPAT_FAULT);same(&c,&before);
}
static void test_identity(void) {
    struct memory m={0};struct b2l_compat_ops o=ops(&m);
    for(size_t i=0;i<sizeof(fixtures)/sizeof(*fixtures);i++)if(fixtures[i].kind==IDENTITY) {
        struct b2l_compat_ctx c=ctx(),e=c;
        c.x[0]=e.x[0]=UINT64_C(0xabcd123456789abc); /* never guess a tag/PAC mask */
        e.pc+=4;assert(b2l_compat_emulate(fixtures[i].word,&c,&o)==B2L_COMPAT_OK);same(&c,&e);
    }
    assert(!m.calls && !m.generic_calls);
    fault(word("pacia_reg"),ctx(),NULL);
    o.mode=0;fault(word("pacia_reg"),ctx(),&o);
}
static void test_branches(void) {
    struct memory m={0};struct b2l_compat_ops o=ops(&m);
    for(size_t i=0;i<sizeof(fixtures)/sizeof(*fixtures);i++)if(fixtures[i].kind==BRANCH) {
        struct b2l_compat_ctx c=ctx(),e=c;
        uint32_t w=fixtures[i].word;unsigned rn=(w>>5)&31;
        if(w==word("retaa")||w==word("retab"))rn=30;
        if(rn==31){fault(w,c,&o);continue;}
        e.pc=c.x[rn];if((w&(1u<<21)) && w!=word("retaa") && w!=word("retab"))e.x[30]=c.pc+4;
        assert(b2l_compat_emulate(w,&c,&o)==B2L_COMPAT_OK);same(&c,&e);
    }
    struct b2l_compat_ctx c=ctx();c.x[0]=0x2001;fault(word("blraa_normal"),c,&o);
    c.x[0]=UINT64_C(0xffff000000002000);fault(word("blraa_normal"),c,&o);
    m.fail_pc=1;fault(word("retab"),ctx(),&o);
}
static void test_loads(void) {
    struct memory m={0};m.value=UINT64_C(0xfedcba9876543210);struct b2l_compat_ops o=ops(&m);
    for(unsigned key=0;key<2;key++) {
        const char *names[2][8]={{"ldraa_zero","ldraa_plus","ldraa_minus","ldraa_wbplus","ldraa_wbminus","ldraa_sp","ldraa_zr","ldraa_overlap"},{"ldrab_zero","ldrab_plus","ldrab_minus","ldrab_wbplus","ldrab_wbminus","ldrab_sp","ldrab_zr","ldrab_overlap"}};
        const int offsets[]={0,4088,-4096,8,-8,8,8,0};
        for(unsigned n=0;n<8;n++) {
            struct b2l_compat_ctx c=ctx(),e=c;c.x[1]=e.x[1]=0x6000;
            uint32_t w=word(names[key][n]);unsigned rn=(w>>5)&31,rd=w&31;
            uint64_t base=rn==31?c.sp:c.x[rn];uint64_t a=base+(uint64_t)(int64_t)offsets[n];
            e.pc+=4;if(rd!=31)e.x[rd]=m.value;
            if(w&0x800){if(rn==31)e.sp=a;else e.x[rn]=a;}
            assert(b2l_compat_emulate(w,&c,&o)==B2L_COMPAT_OK);same(&c,&e);assert(m.address==a && m.width==8);
        }
    }
    const char *names[]={"ldapr_x","ldapr_w","ldaprb","ldaprh","ldapr_sp","ldapr_zr","ldaprb_zr"};
    const unsigned widths[]={8,4,1,2,8,8,1};
    for(unsigned n=0;n<7;n++) {
        struct b2l_compat_ctx c=ctx(),e=c;unsigned rd=word(names[n])&31;
        uint64_t value=m.value;if(widths[n]!=8)value&=(UINT64_C(1)<<(8*widths[n]))-1;
        if(rd!=31)e.x[rd]=value;e.pc+=4;unsigned old=m.acquire_calls;
        assert(b2l_compat_emulate(word(names[n]),&c,&o)==B2L_COMPAT_OK);same(&c,&e);
        assert(m.acquire_calls==old+1 && m.width==widths[n]);
    }
    /* Every signed10-bit LDRA offset, both keys, writeback modes. */
    for (unsigned key=0;key<2;key++) for(unsigned wb=0;wb<2;wb++)
      for(unsigned imm=0;imm<1024;imm++) {
        uint32_t w=word("ldraa_zero") | (key<<23) | ((imm&511u)<<12) |
            ((imm&512u)<<13) | (wb<<11);
        struct b2l_compat_ctx c=ctx(),e=c;c.x[1]=e.x[1]=0x6000;
        int off=(imm>=512 ? (int)imm-1024 : (int)imm)*8;
        uint64_t a=c.x[1]+(uint64_t)(int64_t)off;e.x[0]=m.value;e.pc+=4;
        if(wb)e.x[1]=a;
        assert(b2l_compat_emulate(w,&c,&o)==B2L_COMPAT_OK);same(&c,&e);assert(m.address==a);
    }
    struct b2l_compat_ctx unaligned=ctx();unaligned.x[1]|=1;
    fault(word("ldapr_x"),unaligned,&o);fault(word("ldaprh"),unaligned,&o);
    /* Ordinary LDRA data access may be unaligned; callback owns data mapping. */
    assert(b2l_compat_emulate(word("ldraa_zero"),&unaligned,&o)==B2L_COMPAT_OK);
    struct b2l_compat_ctx edge=ctx();edge.x[1]=UINT64_MAX-3;
    fault(word("ldraa_zero"),edge,&o);fault(word("ldapr_x"),edge,&o);
    m.fail=1;fault(word("ldraa_wbplus"),ctx(),&o);fault(word("ldapr_x"),ctx(),&o);
    m.fail=0;struct b2l_compat_ctx c=ctx();c.sp=0x8008;unsigned old=m.calls;
    fault(word("ldraa_sp"),c,&o);fault(word("ldapr_sp"),c,&o);assert(m.calls==old);
    /* WB with base==destination is rejected before any callbacks. */
    fault(word("ldraa_wbplus")|1u,ctx(),&o);assert(m.calls==old);
    o.read=NULL;fault(word("ldraa_zero"),ctx(),&o);o=ops(&m);
    o.read_acquire=NULL;fault(word("ldapr_zr"),ctx(),&o);
}
static void test_generic(void) {
    struct memory m={0};struct b2l_compat_ops o=ops(&m);
    const char *names[]={"pacga","pacga_alias_n","pacga_alias_m","pacga_zr_n","pacga_zr_d"};
    for(unsigned n=0;n<5;n++) {
        struct b2l_compat_ctx c=ctx(),e=c;uint32_t w=word(names[n]);
        unsigned rn=(w>>5)&31,rm=(w>>16)&31,rd=w&31;
        uint64_t a=rn==31?0:c.x[rn],b=rm==31?c.sp:c.x[rm];
        if(rd!=31)e.x[rd]=UINT64_C(0x1234567800000000);e.pc+=4;
        assert(b2l_compat_emulate(w,&c,&o)==B2L_COMPAT_OK);same(&c,&e);
        assert(m.arg_a==a && m.arg_b==b);
    }
    m.fail_generic=1;fault(word("pacga_alias_n"),ctx(),&o);m.fail_generic=0;o.pacga=NULL;fault(word("pacga_zr_d"),ctx(),&o);
}
static void test_misc_and_random(void) {
    struct memory m={0};struct b2l_compat_ops o=ops(&m);struct b2l_compat_ctx c=ctx(),before=c;
    assert(b2l_compat_emulate(0xd503201f,&c,&o)==B2L_COMPAT_UNHANDLED);same(&c,&before);
    /* Reserved zero-modifier Rn and adjacent opcodes must not be swallowed. */
    assert(b2l_compat_emulate(0xdac12000,&c,&o)==B2L_COMPAT_UNHANDLED);same(&c,&before);
    assert(b2l_compat_emulate(0xdac14800,&c,&o)==B2L_COMPAT_UNHANDLED);same(&c,&before);
    c.pc=UINT64_MAX-3;fault(word("pacibsp"),c,&o);c=ctx();c.pc|=2;fault(word("pacibsp"),c,&o);
    for(unsigned n=0;n<200000;n++) {
        struct b2l_compat_ctx a=ctx(),b;struct memory ma={0},mb={0};
        for(unsigned r=0;r<31;r++)a.x[r]=(random64()&0xfffff)&~UINT64_C(3);
        a.sp=(random64()&0xffff0);a.cpsr=(uint32_t)random64();b=a;
        ma.value=mb.value=random64();ma.fail=mb.fail=(n%17==0);ma.fail_pc=mb.fail_pc=(n%31==0);
        struct b2l_compat_ops oa=ops(&ma),ob=ops(&mb);uint32_t w;
        if(n&1)w=(uint32_t)random64();
        else {w=fixtures[n%(sizeof(fixtures)/sizeof(*fixtures))].word;
            /* Mix real operand fields, including register31 and overlap. */
            if((w&0xff000000)==0xda000000 || (w&0xff000000)==0xf8000000 || (w&0xff000000)==0xb8000000)w=(w&~0x3ffu)|((uint32_t)random64()&0x3ffu);
            else if((w&0xff000000)==0xd7000000)w=(w&~0x3ffu)|((uint32_t)random64()&0x3ffu);
        }
        enum b2l_compat_result ra=b2l_compat_emulate(w,&a,&oa),rb=b2l_compat_emulate_reference(w,&b,&ob);
        assert(ra==rb);same(&a,&b);assert(!memcmp(&ma,&mb,sizeof(ma)));
    }
}
int main(void) {test_identity();test_branches();test_loads();test_generic();test_misc_and_random();puts("compat core fixtures + 200000 C/assembly differential cases passed");return 0;}
