/* SPDX-License-Identifier: BSD-2-Clause
 * Original SHA-256 implementation of the public algorithm, no external code.
 */
#include "CacheCore.h"
static uint32_t rr(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static uint32_t be(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void block(struct b2l_sha*s,const uint8_t*p){
 static const uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint32_t w[64];for(unsigned i=0;i<16;i++)w[i]=be(p+4*i);
 for(unsigned i=16;i<64;i++)w[i]=w[i-16]+(rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10));
 uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
 for(unsigned i=0;i<64;i++){uint32_t t=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+k[i]+w[i],u=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
 s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
void b2l_sha_init(struct b2l_sha*s){const uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memcpy(s->h,h,32);s->bytes=0;s->used=0;}
void b2l_sha_update(struct b2l_sha*s,const void*v,size_t n){const uint8_t*p=v;s->bytes+=n;while(n){size_t k=64-s->used;if(k>n)k=n;memcpy(s->block+s->used,p,k);s->used+=(unsigned)k;p+=k;n-=k;if(s->used==64){block(s,s->block);s->used=0;}}}
void b2l_sha_final(struct b2l_sha*s,uint8_t out[32]){uint64_t bits=s->bytes*8;s->block[s->used++]=128;if(s->used>56){memset(s->block+s->used,0,64-s->used);block(s,s->block);s->used=0;}memset(s->block+s->used,0,56-s->used);for(unsigned i=0;i<8;i++)s->block[63-i]=(uint8_t)(bits>>(8*i));block(s,s->block);for(unsigned i=0;i<32;i++)out[i]=(uint8_t)(s->h[i/4]>>(24-8*(i%4)));memset(s,0,sizeof(*s));}
void b2l_sha256(const void*p,size_t n,uint8_t out[32]){struct b2l_sha s;b2l_sha_init(&s);b2l_sha_update(&s,p,n);b2l_sha_final(&s,out);}
