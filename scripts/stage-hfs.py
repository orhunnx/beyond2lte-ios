#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Stage pinned Apple OSS HFS for an explicitly unencrypted B2L root."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

COMMIT = '124b970fd5390f1c2cd6f985478616a04f6eeb1f'
RELEASE = 'hfs-715.120.4'
XNU_COMMIT = 'ac9718fb1af618d5ce8678d0dc6e8a58f252216f'
UPSTREAM = 'https://github.com/apple-oss-distributions/hfs.git'


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*'))
            if p.is_file() and p.name != 'b2l-provider.json'}


def unencrypted_getattr(text):
    """Keep native unencrypted class-0 queries when the key-service build is off.

    Native CONFIG_PROTECT HFS returns class 0 for c_cpentry == NULL. The B2L
    key-service-free build must not turn that read-only query into ENOTSUP.
    This does not add encrypted files, class changes or key generation.
    """
    start = text.index('#if CONFIG_PROTECT\n\tif (VATTR_IS_ACTIVE(vap, va_dataprotect_class))')
    end = text.index('\n#endif', start) + len('\n#endif')
    old = text[start:end]
    if 'CP_CLASS(cp->c_cpentry->cp_pclass)' not in old or old.count('VATTR_SET_SUPPORTED') != 1:
        raise SystemExit('Pinned HFS protection getattr changed')
    new = old.replace('#if CONFIG_PROTECT', '#if CONFIG_PROTECT || defined(ARM64_BOARD_CONFIG_B2L)', 1)
    line = next(line for line in old.splitlines() if 'vap->va_dataprotect_class =' in line)
    new = new.replace(line, '#if CONFIG_PROTECT\n' + line + '\n#else\n\t\tvap->va_dataprotect_class = 0; /* explicitly unencrypted */\n#endif')
    return text[:start] + new + text[end:]


def upgrade_unencrypted_getattr(destination, data):
    path = destination / 'core/hfs_vnops.c'
    if data.get('unencrypted_getattr_revision') == 1:
        return
    text = unencrypted_getattr(path.read_text())
    path.write_text(text)
    data['unencrypted_getattr_revision'] = 1
    data['files'] = hashes(destination)
    (destination / 'b2l-provider.json').write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xnu', type=Path)
    parser.add_argument('--source', type=Path, default=os.environ.get('HFS_SOURCE'))
    parser.add_argument('--sdk', type=Path)
    args = parser.parse_args()
    xnu = args.xnu.resolve()
    if run('git', '-C', str(xnu), 'rev-parse', 'HEAD') != XNU_COMMIT:
        raise SystemExit('Refusing incompatible XNU revision')
    destination = xnu / 'bsd/b2l_hfs/upstream'
    manifest = destination / 'b2l-provider.json'
    if destination.exists():
        if not manifest.is_file():
            raise SystemExit('Refusing unmanaged HFS source directory')
        data = json.loads(manifest.read_text())
        if (data.get('format') != 1 or data.get('source_commit') != COMMIT or
                data.get('files') != hashes(destination)):
            raise SystemExit('Staged HFS sources were modified or have an unexpected revision')
        upgrade_unencrypted_getattr(destination, data)
        print('Pinned HFS source already staged and verified')
        return
    source = args.source
    if source is None:
        source = Path(__file__).resolve().parent.parent / 'out/dependencies' / RELEASE
        source.parent.mkdir(parents=True, exist_ok=True)
        if not source.exists():
            subprocess.run(['git', 'init', '-q', str(source)], check=True)
            subprocess.run(['git', '-C', str(source), 'fetch', '-q', '--depth', '1',
                            UPSTREAM, COMMIT], check=True)
            subprocess.run(['git', '-C', str(source), 'checkout', '-q', 'FETCH_HEAD'], check=True)
    source = source.resolve()
    if run('git', '-C', str(source), 'rev-parse', 'HEAD') != COMMIT:
        raise SystemExit('Unexpected HFS source commit')
    subprocess.run(['git', '-C', str(source), 'diff', '--quiet', 'HEAD', '--',
                    'core', 'hfs_encodings'], check=True)
    sdk = args.sdk or Path(run('xcrun', '--sdk', 'macosx', '--show-sdk-path'))
    header = sdk / 'usr/include/TargetConditionals.h'
    if not header.is_file():
        raise SystemExit('Public SDK TargetConditionals.h is missing')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='b2l-hfs-stage-', dir=destination.parent) as td:
        staged = Path(td) / 'source'
        for directory in ('core', 'hfs_encodings'):
            shutil.copytree(source / directory, staged / directory)
        path = staged / 'core/hfs_iokit.h'
        text = path.read_text()
        include = '#include <AppleKeyStore/AppleKeyStoreFSServices.h>\n'
        first = 'int hfs_unwrap_key('
        if text.count(include) != 1 or text.count(first) != 1 or text.count('__END_DECLS') != 1:
            raise SystemExit('Pinned HFS key-service header changed')
        text = text.replace(include, '#if CONFIG_PROTECT\n' + include + '#endif\n')
        begin, end = text.index(first), text.index('__END_DECLS')
        text = text[:begin] + '#if CONFIG_PROTECT\n' + text[begin:end] + '#endif\n' + text[end:]
        path.write_text(text)
        shutil.copyfile(header, staged / 'TargetConditionals.h')
        data = {'format': 1, 'release': RELEASE, 'source_commit': COMMIT,
                'upstream': UPSTREAM, 'files': hashes(staged)}
        upgrade_unencrypted_getattr(staged, data)
        staged.rename(destination)
    print(f'Staged {RELEASE} @ {COMMIT} for unencrypted HFS')


if __name__ == '__main__':
    main()
