#!/usr/bin/env python3
"""Execute actual PACGA runtime and pinned XNU SipHash with host OS boundaries.

Only platform headers/bounds annotations are adapted. Cipher code is taken
from the prepared pinned source, not a replacement implementation in this test.
"""
from pathlib import Path
import argparse,re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('xnu',type=Path)
p.add_argument('--tsan',action='store_true')
a=p.parse_args();s=r/'xnu-overlay/osfmk/arm64'
with tempfile.TemporaryDirectory(prefix='b2l-pacga-runtime-') as td:
 d=Path(td);(d/'net').mkdir()
 header=(a.xnu/'bsd/net/siphash.h').read_text().replace('#include <sys/types.h>','#include <stddef.h>')
 header=re.sub(r'\s+__sized_by\([^)]*\)','',header)
 (d/'net/siphash.h').write_text(header)
 source=(a.xnu/'bsd/net/siphash.c').read_text()
 source=re.sub(r'^#include .*\n','',source,flags=re.M)
 adapter='''#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <net/siphash.h>
#define __indexable
#define __unsafe_indexable
#undef __sized_by
#define __sized_by(x)
#undef __sized_by_or_null
#define __sized_by_or_null(x)
#undef __unsafe_forge_bidi_indexable
#define __unsafe_forge_bidi_indexable(t,p,n) ((void)(n),(t)(p))
#define ASSERT(x) assert(x)
#define MIN(a,b) ((a)<(b)?(a):(b))
static inline uint64_t le64dec(const void*p){uint64_t v;memcpy(&v,p,8);return v;}
static inline void le64enc(void*p,uint64_t v){memcpy(p,&v,8);}
#define le64toh(x) (x)
'''
 (d/'siphash.c').write_text(adapter+source)
 cc=subprocess.check_output(['xcrun','--find','clang'],text=True).strip()
 sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 flags=['-isysroot',sdk,'-O2','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-pthread','-I'+str(d),'-I'+str(s)]
 flags+=['-fsanitize=thread'] if a.tsan else ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([cc,*flags,str(r/'tests/test_arm64e_runtime.c'),str(s/'b2l_compat.c'),str(s/'b2l_pacga_qarma5.c'),str(r/'xnu-overlay/bsd/kern/b2l_pacga_siphash.c'),str(d/'siphash.c'),'-o',str(d/'test')],check=True)
 for mode in ['qarma','siphash']:subprocess.run([str(d/'test'),mode],check=True)
