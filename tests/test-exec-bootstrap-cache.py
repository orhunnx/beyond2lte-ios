#!/usr/bin/env python3
"""Run the native launch-cache argv helper and XNU string accounting under sanitizers."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'out/build-env-0063/src/xnu/bsd/kern/kern_exec.c').read_text()
a=s.index('__attribute__((noinline))\nstatic int\nexec_add_user_string(');b=s.index('\n/*',a)
copy=s[a:b]
code=r'''
#include <cassert>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#if !defined(__APPLE__)
using user_addr_t=uintptr_t;
#endif
using boolean_t=bool;
#define TRUE true
#define UIO_SYSSPACE 1
#define UIO_SEG_IS_USER_SPACE(s) ((s)!=UIO_SYSSPACE)
#ifndef CAST_USER_ADDR_T
#define CAST_USER_ADDR_T(p) ((user_addr_t)(p))
#endif
#define CAST_DOWN(t,p) ((t)(p))
#define EXECUTABLE_KEY "executable_path="
struct image_params{char*ip_strings,*ip_strendp,*ip_endargv;int ip_strspace,ip_argspace,ip_envc,ip_argc;};
static bool has_arg;static int mode,logs,force_error;static int ready=1;
static bool PE_parse_boot_argn(const char*n,void*p,int size){assert(!strcmp(n,"b2l_userspace_bootstrap")&&size==sizeof(int));memcpy(p,&mode,size);return has_arg;}
static void b2l_early_trace(const char*,...){++logs;}
static int copystr(const char*s,char*d,int cap,size_t*n){if(force_error){*n=0;return EFAULT;}*n=std::min(strlen(s)+1,(size_t)cap);memcpy(d,s,*n);return *n==strlen(s)+1?0:ENAMETOOLONG;}
static int copyinstr(user_addr_t,char*,int,size_t*){assert(false);return EFAULT;}
''' + copy + '\n' + 'int b2l_apps_cache_is_ready(void){return ready;}\n'+(r/'xnu-overlay/bsd/kern/b2l_exec_bootstrap_cache.h').read_text() + r'''
int main(){
 const char*target="/usr/libexec/launchd_cache_loader";
 for(int trial=0;trial<10;++trial){
  char mem[1024];memset(mem,0xa5,sizeof(mem));
  const char*path=trial==0?"/tmp/launchd_cache_loader":target;
  std::string saved=std::string(EXECUTABLE_KEY)+path;memcpy(mem,saved.c_str(),saved.size()+1);
  char*args=mem+saved.size()+1;strcpy(args,"launchd_cache_loader");char*end=args+strlen(args)+1;
  image_params p{mem,end,end,500,500,0,1};mode=1;ready=1;has_arg=true;force_error=0;
  int ptr=8;
  if(trial==1)ready=0;if(trial==2)mode=0;if(trial==3)has_arg=false;if(trial==4)p.ip_argc=3;if(trial==5)ptr=4;
  if(trial==6)p.ip_argspace=10;if(trial==7)p.ip_strspace=10;if(trial==8)force_error=1;
  auto old=p;int error=b2l_exec_bootstrap_cache(&p,ptr);
  if(trial<6){assert(!error&&p.ip_strendp==old.ip_strendp&&p.ip_argc==old.ip_argc);}
  else if(trial<8){assert(error==E2BIG&&p.ip_strendp==old.ip_strendp&&p.ip_argc==1);}
  else if(trial==8){assert(error==EFAULT&&p.ip_strendp==old.ip_strendp&&p.ip_argc==1);}
  else{
   const char*c="/private/var/db/b2l-launchd-bootstrap-v3.plist";const char*sig="/System/Library/xpc/launchd.plist.sig";
   size_t bytes=strlen(c)+strlen(sig)+2;
   assert(!error&&p.ip_argc==3&&p.ip_envc==0&&p.ip_strendp==old.ip_strendp+bytes);
   assert(p.ip_argspace==old.ip_argspace-(int)bytes-16&&p.ip_strspace==old.ip_strspace-(int)bytes);
   assert(!strcmp(old.ip_strendp,c)&&!strcmp(old.ip_strendp+strlen(c)+1,sig)&&!strcmp(args,"launchd_cache_loader"));
   assert((unsigned char)*p.ip_strendp==0xa5);
   auto once=p;assert(!b2l_exec_bootstrap_cache(&p,8)&&p.ip_strendp==once.ip_strendp&&p.ip_argc==3);
  }
 }
 puts("actual native cache-loader argv: exact-path/gates/bounds/accounting PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-exec-cache-test-') as td:
 p=Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
