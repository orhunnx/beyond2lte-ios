from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'xnu-overlay/osfmk/arm64/b2l_early.c').read_text();a=s.index('/* Capture typed values');b=s.index('\nvoid\nb2l_early_panic',a);actual=s[a:b]
code='#include <stdint.h>\n#include <assert.h>\n#include <string.h>\n#include <stddef.h>\n'+actual+r'''
int main(void){
 char text[1024];memset(text,'Q',sizeof(text));text[1023]=0;
 b2l_early_critical_exit(1,6,0x100000000ull+7,6,9,text);
 assert(b2l_critical_exit.ready&&b2l_critical_exit.pid==1&&b2l_critical_exit.name_space==6&&b2l_critical_exit.code==0x100000007ull);
 assert(strlen(b2l_critical_exit.description)==255&&b2l_critical_exit.signal==6&&b2l_critical_exit.status==9);
 b2l_early_critical_exit(99,0,0,0,0,NULL);assert(b2l_critical_exit.pid==1&&b2l_critical_exit.code==0x100000007ull);
 memset(&b2l_critical_exit,0,sizeof(b2l_critical_exit));b2l_early_critical_exit(1,0,0,0,0,NULL);assert(!strcmp(b2l_critical_exit.description,"none"));
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-exit-test-') as td:
 p=Path(td);(p/'t.c').write_text(code);subprocess.run(['clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'t.c'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
print('actual critical exit capture: typed fields, bounded copy, null description, first-exit preservation PASS')
