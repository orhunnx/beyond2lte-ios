#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Host-test the actual B2L adapters and locally supplied XNU crypto sources.

No upstream crypto implementation or private SDK header is copied into this repo.
Kernel locking is replaced by pthread mutexes; algorithm/adapter bodies are unchanged.
"""
import hashlib
import hmac
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def strip_includes(source):
    return '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))


def oracle(seed, nonce, personalization):
    key, value = bytes(32), bytes([1]) * 32
    def update(data):
        nonlocal key, value
        key = hmac.digest(key, value + b'\0' + data, 'sha256')
        value = hmac.digest(key, value, 'sha256')
        if data:
            key = hmac.digest(key, value + b'\1' + data, 'sha256')
            value = hmac.digest(key, value, 'sha256')
    update(seed + nonce + personalization)
    def generate(size, reseed=None):
        nonlocal value
        if reseed is not None:
            update(reseed)
        output = b''
        while len(output) < size:
            value = hmac.digest(key, value, 'sha256')
            output += value
        update(b'')
        return output[:size].hex()
    return generate


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: test_random_services.py /path/to/patched/xnu')
    xnu = Path(sys.argv[1]).resolve()
    prng = (xnu / 'osfmk/prng/prng_random.c').read_text()
    begin = prng.index('/* Register the real built-in HMAC-SHA256')
    end = prng.index('\n#endif', begin)
    adapter = strip_includes(prng[begin:end])
    rand = strip_includes((xnu / 'libkern/crypto/corecrypto_rand.c').read_text())
    sha = strip_includes((xnu / 'libkern/crypto/corecrypto_sha2.c').read_text())
    # Leave the unchanged external-only SHA384 methods out of the host unit.
    a, b = sha.index('void\nSHA384_Init'), sha.index('void\nSHA512_Init')
    sha = sha[:a] + sha[b:]
    pre = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <corecrypto/cckprng.h>
#include <corecrypto/ccsha2.h>
#include <corecrypto/ccdigest.h>
#include <libkern/crypto/sha2.h>
#include "b2l_kmem_random.h"
typedef unsigned u_int;
#define ARM64_BOARD_CONFIG_B2L 1
#define CRYPTO_SHA2 1
#define MAX_CPUS 8
#define SEED_SIZE 64
#define __startup_func
#define STARTUP(a,b,c)
#define LCK_GRP_DECLARE(n,s) int n
#define LCK_MTX_DECLARE(n,g) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define lck_mtx_lock(m) assert(pthread_mutex_lock(m)==0)
#define lck_mtx_unlock(m) assert(pthread_mutex_unlock(m)==0)
#define lck_mtx_try_lock(m) (pthread_mutex_trylock(m)==0)
#define panic(...) do { fprintf(stderr,__VA_ARGS__); abort(); } while(0)
#define kprintf(...) (void)0
static uint64_t host_seed=0x1234567890abcdef;
uint64_t early_random(void) {return ++host_seed;}
void register_and_init_prng(struct cckprng_ctx *,const struct cckprng_funcs *);
void read_frandom(void *,unsigned);
typedef void *crypto_random_ctx_t;
struct host_crypto_functions {
 struct ccrng_state *(*ccrng_fn)(int *);
 void (*random_generate_fn)(void*,void*,size_t);
 void (*random_uniform_fn)(void*,uint64_t,uint64_t*);
 size_t (*random_kmem_ctx_size_fn)(void);
 void (*random_kmem_init_fn)(void*);
 const struct ccdigest_info *ccsha256_di,*ccsha512_di;
 void (*ccdigest_init_fn)(const struct ccdigest_info*,ccdigest_ctx_t);
 void (*ccdigest_update_fn)(const struct ccdigest_info*,ccdigest_ctx_t,size_t,const void*);
};
static struct host_crypto_functions *g_crypto_funcs;
'''
    post = r'''
static int host_samples;
static int32_t host_entropy(size_t *size,void*out,void *arg) {
 (void)arg;
 if(host_samples) {assert(*size>=64);memset(out,0xa5,64);*size=64;}
 return host_samples;
}
static void printhex(const char *label,const unsigned char *data,size_t size) {
 printf("%s ",label);for(size_t i=0;i<size;++i)printf("%02x",data[i]);puts("");
}
void read_frandom(void *out,unsigned size) {
 while(size){unsigned n=size>256?256:size;b2l_prng_generate(&b2l_prng_ctx,0,n,out);out=(char*)out+n;size-=n;}
}
static int registered;
void register_and_init_prng(struct cckprng_ctx *ctx,const struct cckprng_funcs *f) {
 unsigned char seed[64],nonce[8];for(unsigned i=0;i<64;++i)seed[i]=i;for(unsigned i=0;i<8;++i)nonce[i]=i;
 f->init_with_getentropy(ctx,MAX_CPUS,64,seed,8,nonce,host_entropy,NULL);
 f->initgen(ctx,0);registered++;
}
static void *thread(void *arg) {
 unsigned char *out=arg;for(unsigned i=0;i<100;++i)b2l_prng_generate(&b2l_prng_ctx,0,32,out+i*32);return NULL;
}
int main(void) {
 b2l_prng_startup();assert(registered==1);
 unsigned char out[66];memset(out,0x5a,sizeof(out));
 b2l_prng_generate(&b2l_prng_ctx,0,64,out+1);assert(out[0]==0x5a&&out[65]==0x5a);printhex("drbg",out+1,64);
 unsigned char reseed[64];memset(reseed,0x42,sizeof(reseed));b2l_prng_reseed(&b2l_prng_ctx,64,reseed);
 b2l_prng_generate(&b2l_prng_ctx,0,64,out+1);printhex("reseed",out+1,64);
 host_samples=0;b2l_prng_refresh(&b2l_prng_ctx);host_samples=576;b2l_prng_refresh(&b2l_prng_ctx);
 b2l_prng_generate(&b2l_prng_ctx,0,64,out+1);printhex("refresh",out+1,64);
 /* A busy refresh is nonblocking and does not call the entropy callback. */
 lck_mtx_lock(&b2l_prng_lock);b2l_prng_refresh(&b2l_prng_ctx);lck_mtx_unlock(&b2l_prng_lock);
 assert(cc_rand_generate(out,64)==0);assert(random_buf(out,64)==0);
 unsigned char threaded[4][3200];pthread_t threads[4];
 for(unsigned i=0;i<4;++i)assert(pthread_create(&threads[i],NULL,thread,threaded[i])==0);
 for(unsigned i=0;i<4;++i)assert(pthread_join(threads[i],NULL)==0);
 for(unsigned i=0;i<400;++i)for(unsigned j=0;j<i;++j)assert(memcmp((char*)threaded+i*32,(char*)threaded+j*32,32)!=0);
 struct b2l_kmem_rng k={0};const uint64_t vector[]={UINT64_C(0xe220a8397b1dcdaf),UINT64_C(0x6e789e6aa1b965f4),UINT64_C(0x06c45d188009454f),UINT64_C(0xf88bb8a8724c81ec)};
 for(unsigned i=0;i<4;++i)assert(b2l_kmem_next(&k)==vector[i]);
 struct b2l_kmem_rng one,two;crypto_random_kmem_init(&one);crypto_random_kmem_init(&two);assert(one.state!=two.state);
 assert(crypto_random_kmem_ctx_size()==sizeof(one));
 for(unsigned n=0;n<65;++n){memset(out,0x5a,sizeof(out));crypto_random_generate(&one,out+1,n);assert(out[0]==0x5a&&out[n+1]==0x5a);}
 const uint64_t bounds[]={1,2,3,17,UINT64_MAX,UINT64_C(0x8000000000000001)};
 for(unsigned i=0;i<6;++i)for(unsigned j=0;j<10000;++j){uint64_t v;crypto_random_uniform(&two,bounds[i],&v);assert(v<bounds[i]);}
 /* Rejection path reference: independently advance a second context. */
 for(unsigned i=0;i<1000;++i){uint64_t bound=UINT64_C(0x8000000000000001),v;struct b2l_kmem_rng ref={i},actual={i};do{v=b2l_kmem_next(&ref);}while(v<UINT64_C(0x7fffffffffffffff));assert(b2l_kmem_bounded(&actual,bound)==v%bound);assert(actual.state==ref.state);}
 unsigned char message[1000];for(unsigned i=0;i<1000;++i)message[i]=i;
 SHA256_CTX s256;SHA512_CTX s512;
 SHA256_Init(&s256);SHA256_Update(&s256,message,3);SHA256_Update(&s256,message+3,997);SHA256_Final(out,&s256);printhex("sha256",out,32);
 SHA512_Init(&s512);SHA512_Update(&s512,message,31);SHA512_Update(&s512,message+31,969);SHA512_Final(out,&s512);printhex("sha512",out,64);
 puts("B2L allocator/strong PRNG/digest adapters: ASan/UBSan passed");
}
'''
    sources = ('ccdrbg_nisthmac ccdrbg ccsha256_ltc_di ccsha256_initial_state '
               'ccsha256_ltc_compress ccsha256_K ccdigest_init ccdigest_update '
               'ccdigest_final_64be cchmac_init cchmac_update cchmac_final cchmac '
               'cc_clear cc_cmp_safe cc_try_abort ccsha512_ltc_di ccsha512_initial_state '
               'ccsha512_ltc_compress ccsha512_K').split()
    with tempfile.TemporaryDirectory(prefix='b2l-random-') as temp:
        root = Path(temp)
        source, binary = root / 'test.c', root / 'test'
        source.write_text(pre + adapter + rand + sha + post)
        args = shlex.split(os.environ.get('CC', 'clang')) + [
            '-std=c11', '-g', '-O1', '-fsanitize=address,undefined',
            '-D__STDC_WANT_LIB_EXT1__=1', '-DCC_USE_ASM=0', '-DCC_DIT_MAYBE_SUPPORTED=0',
            '-I'+str(xnu/'libkern/crypto')]
        for path in ('EXTERNAL_HEADERS', 'EXTERNAL_HEADERS/corecrypto', 'osfmk', 'libkern'):
            args += ['-idirafter', str(xnu/path)]
        args += [str(source)] + [str(xnu/'osfmk/corecrypto'/f'{s}.c') for s in sources]
        subprocess.run(args + ['-o', str(binary)], check=True)
        output = subprocess.check_output([str(binary)], text=True)
        results = dict(line.split(' ', 1) for line in output.splitlines() if line.startswith(('drbg ', 'reseed ', 'refresh ', 'sha256 ', 'sha512 ')))
        generate = oracle(bytes(range(64)), bytes(range(8)), b'xnu b2l kernel random')
        assert results['drbg'] == generate(64)
        assert results['reseed'] == generate(64, bytes([0x42])*64)
        assert results['refresh'] == generate(64, bytes([0xa5])*64)
        message = bytes(i&255 for i in range(1000))
        assert results['sha256'] == hashlib.sha256(message).hexdigest()
        assert results['sha512'] == hashlib.sha512(message).hexdigest()
        assert prng.index('STARTUP(EARLY_BOOT, STARTUP_RANK_FIRST, b2l_prng_startup)') > 0
        vm = (xnu/'osfmk/vm/vm_map_store.c').read_text()
        assert 'STARTUP(EARLY_BOOT, STARTUP_RANK_MIDDLE, vm_map_store_crypto_init)' in vm
        print(output.splitlines()[-1])
        print('Independent Python HMAC-DRBG/SHA256/SHA512 known-answer checks passed; FIRST precedes allocator MIDDLE')


if __name__ == '__main__':
    main()
