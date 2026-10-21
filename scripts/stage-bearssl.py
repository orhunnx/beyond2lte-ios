#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Stage pinned MIT BearSSL portable AES sources as an external local input."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

COMMIT='7bea48e5e850ab4cafbe68d3765cdaba13a86d6f'
XNU_COMMIT='ac9718fb1af618d5ce8678d0dc6e8a58f252216f'
UPSTREAM='https://www.bearssl.org/git/BearSSL'
AES_FILES=('aes_ct','aes_ct_enc','aes_ct_dec','aes_ct_cbcenc','aes_ct_cbcdec')


def run(*args):
    return subprocess.check_output(args,text=True).strip()


def hashes(root):
    return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file() and p.name!='b2l-provider.json'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xnu',type=Path)
    parser.add_argument('--source',type=Path,default=os.environ.get('BEARSSL_SOURCE'))
    args=parser.parse_args();xnu=args.xnu.resolve()
    if run('git','-C',str(xnu),'rev-parse','HEAD')!=XNU_COMMIT:
        raise SystemExit('Refusing incompatible XNU revision')
    destination=xnu/'libkern/crypto/b2l_bearssl'
    manifest=destination/'b2l-provider.json'
    if destination.exists():
        if not manifest.is_file():
            raise SystemExit(f'Refusing to overwrite unmanaged AES input: {destination}')
        data=json.loads(manifest.read_text())
        if data.get('format')!=1 or data.get('source_commit')!=COMMIT or data.get('files')!=hashes(destination):
            raise SystemExit('Staged AES dependency was modified or has an unexpected revision')
        print('Pinned BearSSL AES sources already staged and verified');return
    source=args.source
    if source is None:
        source=Path(__file__).resolve().parent.parent/'out/dependencies/bearssl'
        source.parent.mkdir(parents=True,exist_ok=True)
        if not source.exists():
            subprocess.run(['git','init','-q',str(source)],check=True)
            subprocess.run(['git','-C',str(source),'fetch','-q','--depth','1',UPSTREAM,COMMIT],check=True)
            subprocess.run(['git','-C',str(source),'checkout','-q','FETCH_HEAD'],check=True)
    source=source.resolve()
    if run('git','-C',str(source),'rev-parse','HEAD')!=COMMIT:
        raise SystemExit('Unexpected BearSSL source commit')
    subprocess.run(['git','-C',str(source),'diff','--quiet','HEAD','--','inc','src','LICENSE.txt'],check=True)
    with tempfile.TemporaryDirectory(prefix='b2l-aes-stage-',dir=destination.parent) as temp:
        staged=Path(temp)/'provider';(staged/'src/symcipher').mkdir(parents=True)
        shutil.copytree(source/'inc',staged/'inc')
        for name in ('inner.h','config.h'):
            shutil.copyfile(source/'src'/name,staged/'src'/name)
        header=staged/'src/inner.h';text=header.read_text()
        if text.count('#include <limits.h>')!=1:
            raise SystemExit('Pinned BearSSL inner header changed')
        header.write_text(text.replace('#include <limits.h>',
            '#if defined(KERNEL)\n#include <machine/limits.h>\n#else\n#include <limits.h>\n#endif'))
        for name in AES_FILES:
            shutil.copyfile(source/'src/symcipher'/f'{name}.c',staged/'src/symcipher'/f'{name}.c')
        shutil.copyfile(source/'LICENSE.txt',staged/'LICENSE.txt')
        data={'format':1,'source_commit':COMMIT,'upstream':UPSTREAM,'files':hashes(staged)}
        (staged/'b2l-provider.json').write_text(json.dumps(data,sort_keys=True,indent=2)+'\n')
        staged.rename(destination)
    print(f'Staged real BearSSL portable AES @ {COMMIT}')


if __name__=='__main__':
    main()
