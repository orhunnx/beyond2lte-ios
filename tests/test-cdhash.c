#define main identity_checks
#include "test-identity.c"
#undef main
#include "b2l_cache_core.h"
int main(void){
 assert(!identity_checks());struct b2l_amfi_reader r={sizeof(disk),rd,NULL};uint8_t hash[20],type,digest[32];
 uint8_t*c=fixture(0x20600,"example.fixture",16);c[36]=32;c[37]=2;b2l_sha256(c,124,digest);
 assert(b2l_amfi_read_cdhash(&r,0,hash,&type)==0&&type==2&&!memcmp(hash,digest,20));
 c[37]=3;c[36]=20;b2l_sha256(c,124,digest);assert(!b2l_amfi_read_cdhash(&r,0,hash,&type)&&type==3&&!memcmp(hash,digest,20));
 c[37]=1;assert(b2l_amfi_read_cdhash(&r,0,hash,&type)==B2L_AMFI_XML_ABSENT&&!type&&!memcmp(hash,(uint8_t[20]){0},20));
 c[37]=2;c[36]=32;fail_at=276;assert(b2l_amfi_read_cdhash(&r,0,hash,&type)==B2L_AMFI_XML_IO&&!type&&!memcmp(hash,(uint8_t[20]){0},20));fail_at=UINT64_MAX;
 uint32_t random=7;for(unsigned i=0;i<10000;i++){
  fixture(0x20600,"example.fixture",16);disk[276+36]=32;disk[276+37]=2;random=random*1664525u+1013904223u;
  disk[random%512]^=(uint8_t)(random>>24);int e=b2l_amfi_read_cdhash(&r,0,hash,&type);assert(e>=0&&e<=3);
  if(e)assert(!type&&!memcmp(hash,(uint8_t[20]){0},20));else assert(type==2||type==3);
 }
 puts("Original CDHash metadata: SHA256/truncated variants, errors, retained copy and 10k malformed fixtures passed");
}
