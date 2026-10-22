#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the actual kernel-owned early renderer with host-backed scanout."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_early_console.py /path/to/patched/xnu")
    body = (Path(sys.argv[1]) / "osfmk/arm64/b2l_early.c").read_text()
    body = "\n".join(line for line in body.splitlines() if not line.startswith("#include"))
    # Substitute only MMIO mapping and architectural barrier for a host test.
    # Geometry validation, rendering, cursor state and ring behavior are unchanged.
    body = body.replace("b2l_fb = (volatile uint32_t *)(uintptr_t)physical;",
                        "b2l_fb = host_framebuffer;")
    body = body.replace('__asm__ volatile("dsb sy" ::: "memory");', '(void)0;')
    pre = r'''
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define B2L 1
#define B2L_BOOTSTRAP_TABLE_PAGES 16
#define B2L_FB_RESERVED_BASE 0xCA000000ULL
#define B2L_FB_RESERVED_SIZE 0x01400000ULL
struct video { uint64_t v_baseAddr,v_display,v_rowBytes,v_width,v_height,v_depth; };
typedef struct { struct video Video; uint64_t memSize; } boot_args;
static uint64_t host_log_count;static unsigned char host_log_last;
void b2l_log_append(char c){++host_log_count;host_log_last=(unsigned char)c;}
unsigned char iso_font[256*16];
static volatile uint32_t *host_framebuffer;
void b2l_early_putc(char c);
static void _doprnt(const char *fmt,va_list *args,void (*putc_fn)(char),int radix) {
 char text[512]; (void)radix; vsnprintf(text,sizeof(text),fmt,*args);
 for(const char*p=text;*p;++p)putc_fn(*p);
}
'''
    post = r'''
static void canaries(const uint8_t *raw,size_t size,unsigned width,unsigned height,unsigned stride) {
 for(size_t i=0;i<64;++i){assert(raw[i]==0x5a);assert(raw[64+size+i]==0x5a);}
 for(unsigned y=0;y<height;++y)for(unsigned x=width*4;x<stride;++x)
  assert(raw[64+(size_t)y*stride+x]==0x5a);
}
static void panic_format(const char *fmt,...) { va_list ap;va_start(ap,fmt);b2l_early_panic(fmt,&ap);va_end(ap); }
int main(void) {
 const unsigned width=1440,height=3040,stride=width*4+64;
 const size_t size=(size_t)height*stride;
 uint8_t *raw=malloc(size+128),*second=malloc(size+128);
 memset(raw,0x5a,size+128);memset(second,0x5a,size+128);
 host_framebuffer=(volatile uint32_t *)(raw+64);
 memset(iso_font,0x81,sizeof(iso_font));
 boot_args args={.Video={.v_baseAddr=B2L_FB_RESERVED_BASE|1,.v_width=width,
  .v_height=height,.v_rowBytes=stride,.v_depth=32}};
 b2l_early_init(&args);assert(b2l_early_available());assert(b2l_scale==3);
 assert(b2l_early_virtual()==(uintptr_t)(raw+64));
 uint64_t pa,bytes;assert(b2l_early_range(&pa,&bytes));assert(pa==B2L_FB_RESERVED_BASE&&bytes==size);
 b2l_col=0;b2l_row=b2l_rows-1;
 uint64_t logged=host_log_count;
 b2l_early_putc((char)0xff);assert(b2l_col==1);
 assert(host_log_count==logged+1&&host_log_last==0xff);
 unsigned y=b2l_row*16*b2l_scale;
 const uint32_t *line=(const uint32_t *)(raw+64+(size_t)y*stride);
 for(unsigned x=0;x<24;++x)assert(line[x]==((x<3||x>=21)?0xffffffff:0xff000000));
 b2l_early_putc('\r');assert(b2l_col==0);
 b2l_early_putc('\n');assert(b2l_row==0&&b2l_col==0);
 for(unsigned i=0;i<1500;++i)b2l_early_putc(i%43==0?'\n':(char)(i&255));
 canaries(raw,size,width,height,stride);
 b2l_early_relocate((uintptr_t)(second+64));
 assert(b2l_early_virtual()==(uintptr_t)(second+64));
 b2l_early_mark(19,"relocated");panic_format("test %u",17U);
 canaries(second,size,width,height,stride);
 const uint64_t bad_bases[]={B2L_FB_RESERVED_BASE-4096,B2L_FB_RESERVED_BASE+B2L_FB_RESERVED_SIZE-4096,UINT64_MAX-8};
 for(unsigned i=0;i<sizeof(bad_bases)/sizeof(bad_bases[0]);++i){
  b2l_active=0;args.Video.v_baseAddr=bad_bases[i];b2l_early_init(&args);assert(!b2l_active);
 }
 args.Video.v_baseAddr=B2L_FB_RESERVED_BASE;args.Video.v_rowBytes=width*4-4;
 b2l_early_init(&args);assert(!b2l_active);
 args.Video.v_rowBytes=stride;args.Video.v_depth=16;
 b2l_early_init(&args);assert(!b2l_active);
 canaries(raw,size,width,height,stride);canaries(second,size,width,height,stride);
 b2l_active=0;logged=host_log_count;b2l_early_putc('X');assert(host_log_count==logged+1&&host_log_last=='X');
 free(raw);free(second);puts("B2L early console geometry/ring/CR/high-bit/relocation guards passed");
}
'''
    with tempfile.TemporaryDirectory(prefix="b2l-early-console-") as temporary:
        root = Path(temporary)
        source, binary = root / "test.c", root / "test"
        source.write_text(pre + body + post)
        compiler = shlex.split(os.environ.get("CC", "clang"))
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
