#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise actual AES adapter/wrappers with real pinned BearSSL under ASan/UBSan.

Vectors: NIST SP 800-38A Appendix F.1/F.2. GCM is unavailable: its dispatch hooks
are traps to prove every negative API path stops before touching them.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def strip_includes(text):
    return '\n'.join(line for line in text.splitlines() if not line.startswith('#include'))


def main():
    xnu=Path(sys.argv[1]).resolve();bear=xnu/'libkern/crypto/b2l_bearssl'
    pre=r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <libkern/crypto/aes.h>
#include <corecrypto/cc_error.h>
#include "bearssl_block.h"
#define ARM64_BOARD_CONFIG_B2L 1
#define panic(...) abort()
struct host_crypto {
 const struct ccmode_gcm *ccaes_gcm_encrypt,*ccaes_gcm_decrypt;
 int (*ccgcm_init_with_iv_fn)(const struct ccmode_gcm*,ccgcm_ctx*,size_t,const void*,const void*);
 int (*ccgcm_inc_iv_fn)(const struct ccmode_gcm*,ccgcm_ctx*,void*);
};
static struct host_crypto *g_crypto_funcs;
/* These are unsupported-path dispatch traps, not implementations. */
int ccgcm_init(const struct ccmode_gcm*m,ccgcm_ctx*c,size_t n,const void*k){(void)m;(void)c;(void)n;(void)k;abort();}
int ccgcm_set_iv(const struct ccmode_gcm*m,ccgcm_ctx*c,size_t n,const void*i){(void)m;(void)c;(void)n;(void)i;abort();}
int ccgcm_reset(const struct ccmode_gcm*m,ccgcm_ctx*c){(void)m;(void)c;abort();}
int ccgcm_aad(const struct ccmode_gcm*m,ccgcm_ctx*c,size_t n,const void*i){(void)m;(void)c;(void)n;(void)i;abort();}
int ccgcm_update(const struct ccmode_gcm*m,ccgcm_ctx*c,size_t n,const void*i,void*o){(void)m;(void)c;(void)n;(void)i;(void)o;abort();}
int ccgcm_finalize(const struct ccmode_gcm*m,ccgcm_ctx*c,size_t n,void*t){(void)m;(void)c;(void)n;(void)t;abort();}
'''
    body=strip_includes((xnu/'libkern/crypto/b2l_aes.c').read_text())
    body+=strip_includes((xnu/'libkern/crypto/corecrypto_aes.c').read_text())
    post=r'''
static void unhex(const char*s,unsigned char*out,size_t n){for(size_t i=0;i<n;++i){unsigned x;assert(sscanf(s+i*2,"%2x",&x)==1);out[i]=x;}}
static void gcm_denied(void){
 unsigned char out[64];memset(out,0xa5,sizeof(out));ccgcm_ctx *ctx=(void*)1;const void*in=(void*)2;
 assert(aes_encrypt_key_gcm(in,16,ctx)==aes_error);
 assert(aes_decrypt_key_gcm(in,16,ctx)==aes_error);
 assert(aes_encrypt_key_with_iv_gcm(in,16,in,ctx)==aes_error);
 assert(aes_decrypt_key_with_iv_gcm(in,16,in,ctx)==aes_error);
 assert(aes_encrypt_set_iv_gcm(in,12,ctx)==aes_error);
 assert(aes_decrypt_set_iv_gcm(in,12,ctx)==aes_error);
 assert(aes_encrypt_reset_gcm(ctx)==aes_error&&aes_decrypt_reset_gcm(ctx)==aes_error);
 assert(aes_encrypt_inc_iv_gcm(out,ctx)==aes_error&&aes_decrypt_inc_iv_gcm(out,ctx)==aes_error);
 assert(aes_encrypt_aad_gcm(in,17,ctx)==aes_error&&aes_decrypt_aad_gcm(in,17,ctx)==aes_error);
 assert(aes_encrypt_gcm(in,64,out,ctx)==aes_error&&aes_decrypt_gcm(in,64,out,ctx)==aes_error);
 assert(aes_encrypt_finalize_gcm(out,16,ctx)==aes_error&&aes_decrypt_finalize_gcm(out,16,ctx)==aes_error);
 assert(aes_encrypt_get_ctx_size_gcm()==0&&aes_decrypt_get_ctx_size_gcm()==0);
 for(size_t i=0;i<sizeof(out);++i)assert(out[i]==0xa5);
}
int main(void){
 const char *keys[]={"2b7e151628aed2a6abf7158809cf4f3c","8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b","603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"};
 const char *cbc[]={
 "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b273bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7",
 "4f021db243bc633d7178183a9fa071e8b4d9ada9ad7dedf4e5e738763f69145a571b242012fb7ae07fa9baac3df102e008b0e27988598881d920a9e64f5615cd",
 "f58c4c04d6e5f1ba779eabfb5f7bfbd69cfc4e967edb808d679f777bc6702c7d39f23369a9d9bacfa530e26304231461b2eb05e2c39be9fcda6c19078c6a9d1b"};
 const char *ecb[]={
 "3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaaf43b1cd7f598ece23881b00e3ed0306887b0c785e27e8ad3f8223207104725dd4",
 "bd334f1d6e45f25ff712a214571fa5cc974104846d0ad3ad7734ecb3ecee4eefef7afd2270e2e60adce0ba2face6444e9a4b41ba738d6c72fb16691603c18e0e",
 "f3eed1bdb5d2a03c064b5a7e3db181f8591ccb10d410ed26dc5ba74a31362870b6ed21b99ca6f4f9f153e7b1beafed1d23304b7a39f9f3ff067d8d8f9e24ecc7"};
 unsigned char plain[64],key[32],iv[16],original_iv[16],want[64],want_ecb[64],buffer[72];
 unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710",plain,64);
 for(unsigned i=0;i<16;++i)iv[i]=i;memcpy(original_iv,iv,16);
 for(unsigned bits=128;bits<=256;bits+=64){unsigned n=(bits-128)/64;unhex(keys[n],key,bits/8);unhex(cbc[n],want,64);unhex(ecb[n],want_ecb,64);
  aes_encrypt_ctx encrypt,copy; aes_decrypt_ctx decrypt;
  for(unsigned format=0;format<2;++format){int keylen=format?bits:bits/8;
   assert(aes_encrypt_key(key,keylen,&encrypt)==aes_good);assert(aes_decrypt_key(key,keylen,&decrypt)==aes_good);
   memset(buffer,0xa5,sizeof(buffer));assert(aes_encrypt_cbc(plain,iv,4,buffer+1,&encrypt)==aes_good);
   assert(!memcmp(buffer+1,want,64)&&buffer[0]==0xa5&&buffer[65]==0xa5&&!memcmp(iv,original_iv,16));
   assert(aes_decrypt_cbc(buffer+1,iv,4,buffer+1,&decrypt)==aes_good);assert(!memcmp(buffer+1,plain,64)&&!memcmp(iv,original_iv,16));
   copy=encrypt; /* A copied network AES context must remain self-contained. */
   for(unsigned b=0;b<4;++b){assert(aes_encrypt(plain+b*16,buffer+1,&copy)==aes_good);assert(!memcmp(buffer+1,want_ecb+b*16,16));assert(aes_decrypt(buffer+1,buffer+1,&decrypt)==aes_good);assert(!memcmp(buffer+1,plain+b*16,16));}
   assert(aes_encrypt_cbc(plain,NULL,1,buffer+1,&encrypt)==aes_good);assert(!memcmp(buffer+1,want_ecb,16));
   assert(aes_encrypt_cbc(NULL,NULL,0,NULL,&encrypt)==aes_good);assert(aes_decrypt_cbc(NULL,NULL,0,NULL,&decrypt)==aes_good);
   /* Generic mode IV advances, whereas legacy const input IV never changes. */
   cccbc_iv_decl(16,chain);cccbc_set_iv(&b2l_aes_cbc_encrypt,chain,iv);
   assert(cccbc_update(&b2l_aes_cbc_encrypt,encrypt.ctx,chain,2,plain,buffer+1)==0);
   assert(cccbc_update(&b2l_aes_cbc_encrypt,encrypt.ctx,chain,2,plain+32,buffer+33)==0);
   assert(!memcmp(buffer+1,want,64)&&!memcmp(chain,want+48,16));
   cccbc_set_iv(&b2l_aes_cbc_decrypt,chain,iv);
   assert(cccbc_update(&b2l_aes_cbc_decrypt,decrypt.ctx,chain,4,buffer+1,buffer+1)==0);
   assert(!memcmp(buffer+1,plain,64)&&!memcmp(chain,want+48,16));
   /* The adapter preserves partial overlap before invoking in-place BearSSL. */
   memcpy(buffer,plain,64);assert(aes_encrypt_cbc(buffer,iv,4,buffer+3,&encrypt)==0);assert(!memcmp(buffer+3,want,64));
   assert(cccbc_update(&b2l_aes_cbc_encrypt,encrypt.ctx,chain,SIZE_MAX/16+1,plain,buffer)==CCERR_OVERFLOW);
  }
 }
 aes_encrypt_ctx enc; aes_decrypt_ctx dec;
 assert(aes_encrypt_key128(key,&enc)==0&&aes_decrypt_key128(key,&dec)==0);
 assert(aes_encrypt_key256(key,&enc)==0&&aes_decrypt_key256(key,&dec)==0);
 const int bad[]={-1,0,1,15,17,31,33,127,129};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i){assert(aes_encrypt_key(key,bad[i],&enc)!=0);assert(aes_decrypt_key(key,bad[i],&dec)!=0);assert(aes_encrypt(plain,buffer,&enc)!=0);assert(aes_decrypt(plain,buffer,&dec)!=0);}
 assert(aes_encrypt_key(NULL,16,&enc)!=0&&aes_decrypt_key(NULL,16,&dec)!=0);
 assert(aes_encrypt_key(key,16,NULL)!=0&&aes_decrypt_key(key,16,NULL)!=0);
 assert(aes_encrypt_cbc(plain,iv,1,buffer,NULL)!=0&&aes_decrypt_cbc(plain,iv,1,buffer,NULL)!=0);
 gcm_denied();struct host_crypto absent={0};g_crypto_funcs=&absent;gcm_denied();
 struct ccmode_gcm partial={.size=64,.block_size=16};absent.ccaes_gcm_encrypt=&partial;absent.ccaes_gcm_decrypt=&partial;gcm_denied();
 puts("Actual AES128/192/256 CBC/ECB NIST vectors, key bits/bytes, copy/in-place/IV/overflow and all18 unavailable GCM APIs: ASan/UBSan passed");
}
'''
    with tempfile.TemporaryDirectory(prefix='b2l-aes-test-') as temporary:
        root=Path(temporary);src=root/'test.c';binary=root/'test'
        src.write_text(pre+body+post)
        args=shlex.split(os.environ.get('CC','clang'))+['-std=c11','-g','-O1','-fsanitize=address,undefined',
            '-D__STDC_WANT_LIB_EXT1__=1','-DCC_USE_ASM=0','-DCC_DIT_MAYBE_SUPPORTED=0',
            '-I'+str(bear/'inc'),'-I'+str(bear/'src')]
        for path in ('EXTERNAL_HEADERS','EXTERNAL_HEADERS/corecrypto','osfmk','libkern'):
            args+=['-idirafter',str(xnu/path)]
        args += [str(src)] + [str(bear/'src/symcipher'/f'{n}.c') for n in ('aes_ct','aes_ct_enc','aes_ct_dec','aes_ct_cbcenc','aes_ct_cbcdec')]
        args += [str(xnu/'osfmk/corecrypto'/f'{n}.c') for n in ('cccbc','cc_clear','cc_try_abort')]
        subprocess.run(args+['-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)


if __name__=='__main__':
    main()
