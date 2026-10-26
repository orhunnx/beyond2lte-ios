#!/usr/bin/env python3
"""Run the actual scoped exec env helper and XNU string accounting under sanitizers."""
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
struct image_params{char*ip_strings,*ip_strendp,*ip_endargv;int ip_strspace,ip_argspace,ip_envc;};
static bool has_arg;static int mode,logs,force_error;
static bool PE_parse_boot_argn(const char*n,void*p,int size){assert(!strcmp(n,"b2l_diag")&&size==sizeof(int));memcpy(p,&mode,size);return has_arg;}
static void b2l_early_trace(const char*,...){++logs;}
static int copystr(const char*s,char*d,int cap,size_t*n){if(force_error){*n=0;return EFAULT;}*n=std::min(strlen(s)+1,(size_t)cap);memcpy(d,s,*n);return *n==strlen(s)+1?0:ENAMETOOLONG;}
static int copyinstr(user_addr_t,char*,int,size_t*){assert(false);return EFAULT;}
''' + copy + '\n' + (r/'xnu-overlay/bsd/kern/b2l_exec_graphics_diag.h').read_text() + r'''
int main(){
 const char*targets[]={"/System/Library/CoreServices/SpringBoard.app/SpringBoard","/usr/libexec/backboardd","/Applications/Preferences.app/Preferences"};
 for(int ptr:{4,8})for(auto path:targets){
  char mem[1024];memset(mem,0xa5,sizeof(mem));
  std::string saved=std::string(EXECUTABLE_KEY)+path;memcpy(mem,saved.c_str(),saved.size()+1);
  char*args=mem+saved.size()+1;const char original[]="arg0\0one\0";memcpy(args,original,sizeof(original));
  char*env=args+sizeof(original);const char before[]="FIRST=keep\0SECOND=keep\0";memcpy(env,before,sizeof(before)-1);
  image_params p{mem,env+sizeof(before)-1,env,400,400,2};auto old=p;
  has_arg=false;mode=1;assert(b2l_exec_graphics_diag(&p,ptr)==0&&p.ip_strendp==old.ip_strendp);
  has_arg=true;for(int off:{0,2,-1}){mode=off;assert(!b2l_exec_graphics_diag(&p,ptr)&&p.ip_envc==2);}
  mode=1;assert(!b2l_exec_graphics_diag(&p,ptr));
  constexpr char expected[]="CG_CONTEXT_SHOW_BACKTRACE=1";size_t n=sizeof(expected);
  assert(p.ip_envc==3&&p.ip_strendp==old.ip_strendp+n);
  assert(p.ip_argspace==old.ip_argspace-(int)n-ptr&&p.ip_strspace==old.ip_strspace-(int)n);
  assert(!memcmp(old.ip_strendp,expected,n)&&!memcmp(env,before,sizeof(before)-1)&&!memcmp(args,original,sizeof(original)));
  assert((unsigned char)*p.ip_strendp==0xa5);
  auto added=p;assert(!b2l_exec_graphics_diag(&p,ptr)&&p.ip_envc==3&&p.ip_strendp==added.ip_strendp);
  for(int missing=0;missing<2;++missing){p=old;if(missing)p.ip_strspace=n-1;else p.ip_argspace=n+3*ptr-1;auto prior=p;assert(!b2l_exec_graphics_diag(&p,ptr)&&p.ip_strendp==prior.ip_strendp&&p.ip_envc==2);}
  p=old;force_error=1;assert(b2l_exec_graphics_diag(&p,ptr)==EFAULT&&p.ip_envc==2&&p.ip_strendp==old.ip_strendp);force_error=0;
 }
 for(auto path:{"/tmp/SpringBoard","/usr/libexec/backboardd.evil","/Applications/Preferences.app/Preferences.extra","/sbin/launchd"}){
  char mem[256]{};std::string saved=std::string(EXECUTABLE_KEY)+path;memcpy(mem,saved.c_str(),saved.size()+1);char*e=mem+saved.size()+1;
  image_params p{mem,e,e,100,100,0};assert(!b2l_exec_graphics_diag(&p,8)&&p.ip_envc==0);
 }
 // Existing values, including an explicit 0, are not replaced or duplicated.
 for(auto value:{"CG_CONTEXT_SHOW_BACKTRACE=0","CG_CONTEXT_SHOW_BACKTRACE=custom"}){
  char mem[512]{};std::string saved=std::string(EXECUTABLE_KEY)+targets[0];memcpy(mem,saved.c_str(),saved.size()+1);char*e=mem+saved.size()+1;strcpy(e,value);
  image_params p{mem,e+strlen(value)+1,e,200,200,1};auto prior=p;assert(!b2l_exec_graphics_diag(&p,8)&&p.ip_strendp==prior.ip_strendp&&p.ip_envc==1);
 }
 puts("scoped CoreGraphics env: exact paths, opt-in, preserve env, 32/64 pointer accounting, no-space and copy failure PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-cg-env-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
