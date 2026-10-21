#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Actual BearSSL/legacy MD5 adapter and dispatch, RFC1321 and boundary vectors."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main():
    xnu = Path(sys.argv[1]).resolve()
    stage = Path(__file__).resolve().parent.parent
    provider = (stage / 'xnu-overlay/libkern/crypto/b2l_md5.c').read_text()
    provider = provider.replace('#include <libkern/libkern.h>', '')
    dispatch = '\n'.join(line for line in (xnu / 'libkern/crypto/corecrypto_md5.c').read_text().splitlines()
                         if not line.startswith('#include'))
    pre = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ARM64_BOARD_CONFIG_B2L 1
#include <libkern/crypto/md5.h>
struct ccdigest_info { size_t block_size,state_size; };
struct ccdigest_ctx { uint64_t nbits; unsigned num; unsigned char data[64],state[16]; };
#define ccdigest_di_decl(di,name) struct ccdigest_ctx name[1]={{0}}
#define ccdigest_nbits(di,c) ((c)->nbits)
#define ccdigest_num(di,c) ((c)->num)
#define ccdigest_data(di,c) ((c)->data)
#define ccdigest_state_ccn(di,c) ((c)->state)
struct crypto { const struct ccdigest_info *ccmd5_di; void(*ccdigest_init_fn)(const struct ccdigest_info*,struct ccdigest_ctx*); void(*ccdigest_update_fn)(const struct ccdigest_info*,struct ccdigest_ctx*,size_t,const void*); };
static struct crypto *g_crypto_funcs;
static void ccdigest_final(const struct ccdigest_info*d,struct ccdigest_ctx*c,void*out){(void)d;(void)c;(void)out;abort();}
'''
    # RFC1321 messages, HFS's 16+8 update shape, and every padding/block edge.
    messages = [b'', b'a', b'abc', b'message digest', b'abcdefghijklmnopqrstuvwxyz',
                b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789',
                b'1234567890' * 8]
    messages += [bytes((i * 29 + 3) & 255 for i in range(n))
                 for n in (24, 55, 56, 57, 63, 64, 65, 127, 128, 129, 255, 256, 1025)]
    cases = []
    for message in messages:
        literal = ','.join(str(c) for c in message) or '0'
        cases.append('{ static const unsigned char input[]={' + literal + '}; '
                     'check(input,' + str(len(message)) + ',"' +
                     hashlib.md5(message).hexdigest() + '"); }')
    post = r'''
static void check(const unsigned char*data,size_t n,const char*expected){
 unsigned char want[16];for(unsigned i=0;i<16;i++){unsigned v;assert(sscanf(expected+i*2,"%2x",&v)==1);want[i]=(unsigned char)v;}
 const unsigned chunks[]={1,7,16,24,55,56,63,64,65,127,4096};
 for(unsigned k=0;k<sizeof(chunks)/sizeof(chunks[0]);k++){
  struct { unsigned char before[8];MD5_CTX ctx;unsigned char after[8]; } box;
  memset(&box,0xa5,sizeof(box));MD5Init(&box.ctx);assert(box.ctx.count[0]==0&&box.ctx.count[1]==0);
  MD5Update(&box.ctx,NULL,0);
  for(size_t i=0;i<n;){size_t take=n-i;if(take>chunks[k])take=chunks[k];MD5Update(&box.ctx,data+i,(unsigned)take);i+=take;}
  assert(box.ctx.count[0]==0&&box.ctx.count[1]==n);
  MD5_CTX saved=box.ctx;unsigned char output[18];memset(output,0x5a,sizeof(output));MD5Final(output+1,&box.ctx);
  assert(!memcmp(output+1,want,16)&&output[0]==0x5a&&output[17]==0x5a&&!memcmp(&box.ctx,&saved,sizeof(saved)));
  for(unsigned i=0;i<8;i++)assert(box.before[i]==0xa5&&box.after[i]==0xa5);
 }
 /* Context values are self-contained across copies, as PF expects. */
 MD5_CTX first,copy;MD5Init(&first);size_t split=n/2;MD5Update(&first,data,(unsigned)split);copy=first;memset(&first,0xa5,sizeof(first));
 MD5Update(&copy,data+split,(unsigned)(n-split));unsigned char out[16];MD5Final(out,&copy);assert(!memcmp(out,want,16));
 if(n==24){MD5Init(&first);MD5Update(&first,data,16);MD5Update(&first,data+16,8);MD5Final(out,&first);assert(!memcmp(out,want,16));}
}
int main(void){
'''
    post += '\n'.join(cases)
    post += r'''
 unsigned char block[64]={0};MD5_CTX count;MD5Init(&count);count.count[0]=5;count.count[1]=0xfffffff0U;MD5Update(&count,block,64);assert(count.count[0]==6&&count.count[1]==48);
 MD5Init(&count);count.count[0]=UINT32_MAX;count.count[1]=0xfffffff0U;MD5Update(&count,block,64);assert(count.count[0]==0&&count.count[1]==48);
 puts("Actual MD5 RFC/boundary/incremental/context-copy/count ABI tests passed");return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='b2l-md5-test-') as td:
        path = Path(td)
        (path / 'test.c').write_text(pre + provider + dispatch + post)
        compiler = shlex.split(os.environ.get('CC', 'clang'))
        subprocess.run(compiler + ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-function', '-fsanitize=address,undefined',
                       '-fno-omit-frame-pointer', '-I' + str(xnu / 'libkern'),
                       '-I' + str(stage / 'xnu-overlay/libkern/crypto'),
                       '-I' + str(xnu / 'libkern/crypto'),
                       '-I' + str(xnu / 'libkern/crypto/b2l_bearssl/src'),
                       '-I' + str(xnu / 'libkern/crypto/b2l_bearssl/inc'),
                       str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    main()
