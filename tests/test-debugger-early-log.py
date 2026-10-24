#!/usr/bin/env python3
"""Run the actual patched early-debugger formatter with NULL argument shapes."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=Path(os.environ.get('XNU_PATH',root/'out/build-env-0063/src/xnu'))/'osfmk/kern/debug.c'
s=source.read_text();start=s.index('\tif (!PE_arm_debug_and_trace_initialized()) {',s.index('DebuggerTrapWithState(debugger_op'))
end=s.index('\n#endif',start)
block=s[start:end]
pre=r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define EARLY_PANIC_BUFLEN 256
static int initialized, calls;
static char logged[512];
static int PE_arm_debug_and_trace_initialized(void){return initialized;}
static void paniclog_append_noflush(const char *fmt,...){
 va_list ap;va_start(ap,fmt);vsnprintf(logged,sizeof logged,fmt,ap);va_end(ap);calls++;
}
static void actual(const char *db_message,const char *db_panic_str,va_list *db_panic_args){
'''
post=r'''
}
static void formatted(const char *msg,const char *fmt,...){
 va_list ap;va_start(ap,fmt);actual(msg,fmt,&ap);va_end(ap);
}
int main(void){
 actual("debugger 100% %n %s",NULL,NULL);
 assert(!strcmp(logged,"debugger 100% %n %s\n"));
 formatted("ignored","panic %d %s",42,"ok");
 assert(!strcmp(logged,"panic 42 ok\n"));
 actual("missing args","unsafe %n",NULL);
 assert(!strcmp(logged,"missing args\n"));
 formatted("missing format",NULL,1);
 assert(!strcmp(logged,"missing format\n"));
 actual(NULL,NULL,NULL);
 assert(!strcmp(logged,"Debugger entry (no message)\n"));
 char longmsg[600];memset(longmsg,'a',599);longmsg[599]=0;
 actual(longmsg,NULL,NULL);assert(strlen(logged)==256&&logged[255]=='\n');
 initialized=1;int before=calls;actual(NULL,NULL,NULL);assert(calls==before);
 puts("actual early debugger formatter: 7 ASan/UBSan cases passed");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-debugger-test-') as d:
 p=Path(d);(p/'test.c').write_text(pre+block+post)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
