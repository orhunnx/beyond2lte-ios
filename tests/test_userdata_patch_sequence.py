#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply the actual tracked patch sequence in an isolated pinned Git fixture."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    xnu = Path(sys.argv[1]).resolve()
    stage = Path(__file__).resolve().parent.parent
    patches = sorted((stage / 'patches').glob('[0-9][0-9][0-9][0-9]-*.patch'))
    if len(patches) != 23:
        raise SystemExit('Expected exactly twenty-three board patches')
    head = subprocess.check_output(['git', '-C', str(xnu), 'rev-parse', 'HEAD'], text=True).strip()
    objects = subprocess.check_output(['git', '-C', str(xnu), 'rev-parse',
                                      '--git-path', 'objects'], text=True).strip()
    objects = Path(objects)
    if not objects.is_absolute():
        objects = xnu / objects
    names = set()
    for patch in patches:
        for line in patch.read_text().splitlines():
            if line.startswith(('--- a/', '+++ b/')):
                names.add(line[6:].split('\t', 1)[0])
    helper = stage / 'scripts/apply-board-patches.py'
    with tempfile.TemporaryDirectory(prefix='b2l-userdata-patches-') as td:
        fixture = Path(td)
        subprocess.run(['git', 'init', '-q', str(fixture)], check=True)
        alternate = fixture / '.git/objects/info/alternates'
        alternate.write_text(str(objects.resolve()) + '\n')
        subprocess.run(['git', '-C', str(fixture), 'update-ref', 'HEAD', head], check=True)
        for name in names:
            result = subprocess.run(['git', '-C', str(xnu), 'show', 'HEAD:' + name],
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            if result.returncode == 0:
                target = fixture / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(result.stdout)
                mode = subprocess.check_output(['git', '-C', str(xnu), 'ls-tree',
                                                'HEAD', '--', name]).split()[0]
                target.chmod(0o755 if mode == b'100755' else 0o644)
            elif result.returncode != 158:
                raise SystemExit('Cannot read pinned source fixture')
        command = [sys.executable, str(helper), str(fixture)]
        subprocess.run(command + list(map(str, patches[:22])), check=True)
        subprocess.run(command + list(map(str, patches)), check=True)
        subprocess.run(command + list(map(str, patches)), check=True)
        target = fixture / 'iokit/bsddev/IOKitBSDInit.cpp'
        target.write_text(target.read_text() + '\n// synthetic local edit\n')
        before = target.read_bytes()
        result = subprocess.run(command + list(map(str, patches)), capture_output=True)
        assert result.returncode != 0 and target.read_bytes() == before
        assert b'Refusing modified or partial board patch state' in result.stderr
    print('Actual patch sequence: 0022->0023, idempotence and local-edit refusal passed')


if __name__ == '__main__':
    main()
