#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise actual B2L attach, gc init/enable/pause bodies after LOCKS startup.

Host hooks replace screen mapping/dispatch, allocations and ticket primitives;
lock validation, callback presence and display/history effects are asserted.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


def function(source, name):
    match = re.search(r'\n(?:static\s+)?(?:void|boolean_t)\s*\n'+name+r'\([^;]*?\)\s*\{', source)
    if not match:
        raise AssertionError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    xnu = Path(sys.argv[1])
    source = (xnu/'osfmk/console/video_console.c').read_text()
    assert 'static LCK_TICKET_DECLARE(vcputc_lock, &vconsole_lck_grp);' in source
    assert 'static SIMPLE_LOCK_DECLARE(vc_progress_lock, 0);' in source
    attach = function(source, 'vcattach')
    # The complete B2L branch is actual source; the excluded legacy branch
    # requires the whole message buffer/progress infrastructure, so omit it.
    attach = attach[:attach.index('\n#if defined(CONFIG_VC_PROGRESS_METER_SUPPORT)')]+'\n}'
    body = '\n'.join(function(source, n) for n in ('gc_initialize','gc_enable','gc_pause')) + attach
    pre = r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define ARM64_BOARD_CONFIG_B2L 1
#define TRUE 1
#define FALSE 0
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define Z_WAITOK 0
#define ATTR_NONE 0
#define COLOR_FOREGROUND 0
#define COLOR_CODE_SET(a,b,c) 7
#define kPETextMode 1
#define kprintf(...) (void)0
#define panic(...) abort()
typedef int boolean_t;
struct vc_info { unsigned v_columns,v_rows; } vinfo;
typedef struct { uint64_t v_baseAddr,v_offset; } PE_Video;
static struct { PE_Video video; } PE_state={{0xca000001,0}};
static unsigned char pixels[64];
static unsigned init_count,ops_init,clear_count,enable_count,allocations;
static struct {int valid,locked;} vcputc_lock,vc_progress_lock;
static void lockit(void *p){typeof(vcputc_lock)*l=p;assert(l->valid&&!l->locked);l->locked=1;}
static void unlockit(void*p){typeof(vcputc_lock)*l=p;assert(l->valid&&l->locked);l->locked=0;}
#define VCPUTC_LOCK_LOCK() lockit(&vcputc_lock)
#define VCPUTC_LOCK_UNLOCK() unlockit(&vcputc_lock)
#define VCPUTC_LOCK_INIT() do { ++init_count; assert(!vcputc_lock.valid);vcputc_lock.valid=1; } while(0)
#define simple_lock(p,g) lockit(p)
#define simple_unlock(p) unlockit(p)
#define LCK_GRP_NULL 0
#define thread_call_enter_delayed(p,d) (void)0
static boolean_t gc_enabled,gc_initialized,vm_initialized,disableConsoleOutput,gc_acquired;
static boolean_t gc_paused_progress,vc_progress_enable;
static unsigned char *gc_buffer_attributes,*gc_buffer_characters,*gc_buffer_colorcodes,*gc_buffer_tab_stops;
static unsigned gc_buffer_columns,gc_buffer_rows,gc_buffer_size,gc_x,gc_y;
static unsigned char gc_color_code;
static void *kalloc_data(size_t s,int flags){(void)flags;allocations++;return calloc(1,s);}
static void kfree_data(void*p,size_t s){(void)s;if(p){assert(allocations);allocations--;free(p);}}
static int console_is_serial(void){return 1;}
static void gc_reset_vt100(void){}
static void gc_reset_screen(void){}
static void init_backend(struct vc_info *p){assert(p==&vinfo);ops_init++;}
static void enable_backend(boolean_t enabled){(void)enabled;enable_count++;}
static void clear_backend(unsigned x,unsigned y,unsigned top,unsigned bottom,int which){(void)x;(void)y;(void)top;(void)bottom;(void)which;memset(pixels,0,sizeof(pixels));clear_count++;}
static void cursor(unsigned x,unsigned y){(void)x;(void)y;}
static struct {
 void (*initialize)(struct vc_info *);
 void (*enable)(boolean_t);
 void (*clear_screen)(unsigned,unsigned,unsigned,unsigned,int);
 void (*show_cursor)(unsigned,unsigned);
}gc_ops;
static uint64_t b2l_early_virtual(void){return (uintptr_t)pixels;}
static void initialize_screen(PE_Video *,unsigned);
'''
    post = r'''
/* Host equivalent of initialize_screen's existing non-NULL geometry/ops
 * dispatch, with MMIO mapping replaced by host pixels. Actual control bodies
 * above execute without being replaced. */
static void initialize_screen(PE_Video *video,unsigned op){
 assert(video&&op==kPETextMode);assert(video->v_baseAddr==(uintptr_t)pixels);assert(video->v_offset==0);
 vinfo.v_columns=8;vinfo.v_rows=2;
 gc_ops.initialize=init_backend;gc_ops.enable=enable_backend;
 gc_ops.clear_screen=clear_backend;gc_ops.show_cursor=cursor;
 gc_initialize(&vinfo);
}
int main(void){
 /* Model XNU LOCKS callbacks; each of the two real declarations registers
  * exactly one initializer. Later gc_initialize/attach must not repeat it. */
 vcputc_lock.valid=1;vc_progress_lock.valid=1;init_count=2;
 memset(pixels,0xa5,sizeof(pixels));vcattach();
 assert(vm_initialized&&gc_initialized&&gc_acquired&&ops_init==1&&init_count==2);
 assert(!clear_count&&!enable_count&&!allocations);
 for(unsigned i=0;i<sizeof(pixels);++i)assert(pixels[i]==0xa5);
 gc_enable(FALSE);assert(enable_count==1&&!gc_enabled&&!disableConsoleOutput);
 for(unsigned i=0;i<sizeof(pixels);++i)assert(pixels[i]==0xa5);
 gc_pause(TRUE,FALSE);assert(!gc_enabled&&!disableConsoleOutput);
 gc_pause(FALSE,FALSE);assert(gc_enabled);
 gc_enable(TRUE);assert(gc_enabled&&clear_count==1&&allocations==4);
 gc_enable(FALSE);assert(!gc_enabled&&!allocations);
 vcattach();assert(init_count==2&&ops_init==2);
 assert(!vcputc_lock.locked&&!vc_progress_lock.locked);
 puts("B2L real console attach/disable/pause/enable lifecycle: ASan/UBSan passed");
}
'''
    with tempfile.TemporaryDirectory(prefix='b2l-console-lifecycle-') as temporary:
        root=Path(temporary);src=root/'test.c';binary=root/'test'
        src.write_text(pre+body+post)
        args=shlex.split(os.environ.get('CC','clang'))
        subprocess.run(args+['-std=gnu11','-g','-O1','-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    pe=(xnu/'pexpert/gen/pe_gen.c').read_text()
    assert 'vcattach();' in function(pe,'PE_init_printf')
    print('Both LOCKS declarations present; ordinary PE_init_printf(TRUE) attach restored')


if __name__=='__main__':
    main()
