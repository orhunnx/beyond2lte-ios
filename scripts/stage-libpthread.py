#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Stage immutable Apple OSS libpthread sources for the standalone B2L kernel.

Downloaded sources and the SDK's public TargetConditionals.h remain local build
inputs. This script records exact source and staged hashes; no moving refs or
prebuilt kernel extension is used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PTHREAD_COMMIT='1f4f5265b319111142f1bf3a27d4484ef5a98314'
PTHREAD_RELEASE='libpthread-539.100.4'
XNU_COMMIT='ac9718fb1af618d5ce8678d0dc6e8a58f252216f'
UPSTREAM='https://github.com/apple-oss-distributions/libpthread.git'
FORMAT=1


def run(*args):
    return subprocess.check_output(args,text=True).strip()


def hashes(root):
    return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file() and p.name!='b2l-provider.json'}


def replace(root,file,old,new,count):
    path=root/'kern'/file
    text=path.read_text()
    if text.count(old)!=count:
        raise SystemExit(f'Unexpected pinned provider text: {file}: {old!r}')
    path.write_text(text.replace(old,new))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xnu',type=Path)
    parser.add_argument('--source',type=Path,default=os.environ.get('LIBPTHREAD_SOURCE'))
    parser.add_argument('--sdk',type=Path)
    args=parser.parse_args()
    xnu=args.xnu.resolve()
    if run('git','-C',str(xnu),'rev-parse','HEAD')!=XNU_COMMIT:
        raise SystemExit('Refusing incompatible XNU revision')
    destination=xnu/'bsd/pthread/b2l_libpthread'
    manifest=destination/'b2l-provider.json'
    if destination.exists():
        if not manifest.is_file():
            raise SystemExit(f'Refusing to overwrite unmanaged provider: {destination}')
        data=json.loads(manifest.read_text())
        if (data.get('format')!=FORMAT or data.get('source_commit')!=PTHREAD_COMMIT or
            data.get('files')!=hashes(destination)):
            raise SystemExit('Staged provider was modified or has the wrong revision; inspect it before rebuilding')
        print(f'Pinned {PTHREAD_RELEASE} already staged and verified')
        return
    source=args.source
    if source is None:
        repo=Path(__file__).resolve().parent.parent
        source=repo/'out/dependencies'/PTHREAD_RELEASE
        source.parent.mkdir(parents=True,exist_ok=True)
        if not source.exists():
            subprocess.run(['git','init','-q',str(source)],check=True)
            subprocess.run(['git','-C',str(source),'fetch','-q','--depth','1',UPSTREAM,PTHREAD_COMMIT],check=True)
            subprocess.run(['git','-C',str(source),'checkout','-q','FETCH_HEAD'],check=True)
    source=source.resolve()
    if run('git','-C',str(source),'rev-parse','HEAD')!=PTHREAD_COMMIT:
        raise SystemExit('Unexpected libpthread source commit')
    subprocess.run(['git','-C',str(source),'diff','--quiet','HEAD','--','kern','private','include'],check=True)
    sdk=args.sdk or Path(run('xcrun','--sdk','macosx','--show-sdk-path'))
    public_target_header=sdk/'usr/include/TargetConditionals.h'
    if not public_target_header.is_file():
        raise SystemExit('The selected public SDK has no TargetConditionals.h')
    with tempfile.TemporaryDirectory(prefix='b2l-pthread-stage-',dir=destination.parent) as temp:
        staged=Path(temp)/'provider';staged.mkdir()
        for directory in ('kern','private','include'):
            shutil.copytree(source/directory,staged/directory)
        # Kext source addresses cross the stricter built-in XNU callback ABI.
        replace(staged,'kern_trace.h','vm_kernel_unslide_or_perm_external(ptr,',
                'vm_kernel_unslide_or_perm_external((vm_offset_t)ptr,',1)
        for callback,count in (('psynch_wait_update_owner',4),('psynch_wait_complete',1),('psynch_wait_wakeup',2)):
            replace(staged,'kern_synch.c',f'pthread_kern->{callback}(kwq,',
                    f'pthread_kern->{callback}((uintptr_t)kwq,',count)
        replace(staged,'kern_support.c','*kevent_list_out = NULL;','*kevent_list_out = 0;',1)
        replace(staged,'kern_support.c','user_addr_t kevent_list = NULL;','user_addr_t kevent_list = 0;',2)
        replace(staged,'kern_support.c','workq_kevent(p, &th_addrs, NULL,','workq_kevent(p, &th_addrs, 0,',1)
        replace(staged,'kern_support.c','panic(__func__ ": thread_set_wq_state failed: %d", error);',
                'panic("%s: thread_set_wq_state failed: %d", __func__, error);',4)
        shutil.copyfile(public_target_header,staged/'TargetConditionals.h')
        data={'format':FORMAT,'release':PTHREAD_RELEASE,'source_commit':PTHREAD_COMMIT,
              'upstream':UPSTREAM,'files':hashes(staged)}
        (staged/'b2l-provider.json').write_text(json.dumps(data,indent=2,sort_keys=True)+'\n')
        staged.rename(destination)
    print(f'Staged real {PTHREAD_RELEASE} @ {PTHREAD_COMMIT}')


if __name__=='__main__':
    main()
