#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile
def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_console_raster.py /path/to/patched/xnu")
    s=(Path(sys.argv[1])/'osfmk/console/video_console.c').read_text()
    def function(name):
     a=s.index('static void\n'+name+'(');start=s.index('{',a);depth=1;i=start+1
     while depth:
      depth+=(s[i]=='{')-(s[i]=='}');i+=1
     return s[a:i]
    pre='''#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n#include <assert.h>\n#include <stdio.h>\n#define ARM64_BOARD_CONFIG_B2L 1\n#define ISO_CHAR_WIDTH 8\n#define ISO_CHAR_HEIGHT 16\n#define ATTR_BOLD 1\n#define ATTR_UNDER 2\n#define ATTR_REVERSE 4\n#define VC_CHAR_WIDTH (8*vc_b2l_text_scale)\n#define VC_CHAR_HEIGHT (16*vc_b2l_text_scale)\nstatic unsigned vc_b2l_text_scale;\nstatic struct { uintptr_t v_baseaddr; unsigned v_depth,v_rowbytes,v_rowscanbytes,v_rows; } vinfo;\nstatic uint8_t iso_font[256*16];\nstatic uint32_t vc_color_fore=0xffffffff,vc_color_back=0;\nstatic void video_scroll_down(void*a,void*b,void*c){ (void)a;(void)b;(void)c; abort(); }\nstatic void video_scroll_up(void*a,void*b,void*c){ (void)a;(void)b;(void)c; abort(); }\n'''
    post='''
    int main(void) {
     for(unsigned scale=2;scale<=3;++scale) {
      vc_b2l_text_scale=scale;
      unsigned width=1440,height=3040,stride=width*4+64;
      size_t size=(size_t)stride*height;
      uint8_t *raw=malloc(size+128); memset(raw,0x5a,size+128);
      vinfo.v_baseaddr=(uintptr_t)(raw+64);vinfo.v_depth=32;
      vinfo.v_rowbytes=stride;vinfo.v_rowscanbytes=width*4;vinfo.v_rows=height/VC_CHAR_HEIGHT;
      unsigned cols=width/VC_CHAR_WIDTH;
      memset(iso_font+65*16,0x81,16);
      vc_b2l_paint_char_scaled(cols-1,vinfo.v_rows-1,65,0);
      unsigned x=(cols-1)*VC_CHAR_WIDTH,y=(vinfo.v_rows-1)*VC_CHAR_HEIGHT;
      for(unsigned dy=0;dy<VC_CHAR_HEIGHT;dy++) {
       uint32_t *p=(uint32_t *)(raw+64+(y+dy)*stride+x*4);
       for(unsigned dx=0;dx<VC_CHAR_WIDTH;dx++) assert(p[dx]==((dx<scale||dx>=7*scale)?0xffffffff:0));
      }
      vc_reverse_cursor(cols-1,vinfo.v_rows-1);
      vc_reverse_cursor(cols-1,vinfo.v_rows-1);
      for(unsigned row=0;row<height;row++) {
       memset(raw+64+row*stride,(row/VC_CHAR_HEIGHT)&255,width*4);
       for(unsigned p=width*4;p<stride;p++) assert(raw[64+row*stride+p]==0x5a);
      }
      vc_scroll_up(1,0,vinfo.v_rows);
      for(unsigned row=0;row<(vinfo.v_rows-1)*VC_CHAR_HEIGHT;row++)
       assert(raw[64+row*stride]==((row/VC_CHAR_HEIGHT+1)&255));
      for(unsigned row=0;row<height;row++) memset(raw+64+row*stride,(row/VC_CHAR_HEIGHT)&255,width*4);
      vc_scroll_down(1,0,vinfo.v_rows);
      for(unsigned row=VC_CHAR_HEIGHT;row<vinfo.v_rows*VC_CHAR_HEIGHT;row++)
       assert(raw[64+row*stride]==((row/VC_CHAR_HEIGHT-1)&255));
      for(unsigned row=0;row<height;row++) for(unsigned p=width*4;p<stride;p++) assert(raw[64+row*stride+p]==0x5a);
      for(unsigned i=0;i<64;i++) {assert(raw[i]==0x5a);assert(raw[64+size+i]==0x5a);}
      free(raw);
     }
     puts("B2L scaled glyph/cursor/overlapping scroll bounds passed (2x/3x)");return 0;
    }
    '''
    with tempfile.TemporaryDirectory(prefix="b2l-console-raster-") as temporary:
        root = Path(temporary)
        source = root / "raster.c"
        binary = root / "raster"
        source.write_text(pre+'\n'.join(function(n) for n in
            ['vc_b2l_paint_char_scaled','vc_reverse_cursor','vc_scroll_up','vc_scroll_down'])+post)
        compiler = shlex.split(os.environ.get("CC", "clang"))
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-but-set-variable", "-fsanitize=address,undefined",
            str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
