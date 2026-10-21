#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply a pinned patch sequence without overwriting unrelated local edits."""
import subprocess
import sys
import tempfile
from pathlib import Path


def git(root, *args, **kwargs):
    return subprocess.run(['git', '-C', str(root), *args], check=True, **kwargs)


def main():
    xnu = Path(sys.argv[1]).resolve()
    patches = [Path(p).resolve() for p in sys.argv[2:]]
    paths = set()
    for patch in patches:
        for line in patch.read_text().splitlines():
            if line.startswith(('--- a/', '+++ b/')):
                name = line[6:].split('\t', 1)[0]
                relative = Path(name)
                if relative.is_absolute() or '..' in relative.parts:
                    raise SystemExit('Unsafe board patch path')
                paths.add(name)
    if not patches or not paths:
        raise SystemExit('Empty board patch sequence')
    with tempfile.TemporaryDirectory(prefix='b2l-patch-state-') as temp:
        reference = Path(temp)
        for name in paths:
            result = subprocess.run(['git', '-C', str(xnu), 'show', 'HEAD:' + name],
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            if result.returncode == 0:
                target = reference / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(result.stdout)
                mode = subprocess.check_output(['git', '-C', str(xnu), 'ls-tree',
                                                'HEAD', '--', name]).split()[0]
                target.chmod(0o755 if mode == b'100755' else 0o644)
            elif result.returncode != 128:
                raise SystemExit('Cannot read pinned board patch input')
        def snapshot(root):
            return {name: (root / name).read_bytes() if (root / name).is_file() else None
                    for name in paths}
        states = [snapshot(reference)]
        for patch in patches:
            git(reference, 'apply', '--check', str(patch))
            git(reference, 'apply', str(patch))
            states.append(snapshot(reference))
        current = snapshot(xnu)
        matches = [i for i, state in enumerate(states) if current == state]
        if not matches:
            changed = [name for name in sorted(paths)
                       if current[name] != states[-1][name]]
            raise SystemExit('Refusing modified or partial board patch state: ' + ', '.join(changed))
        already = max(matches)
        for patch in patches[already:]:
            git(xnu, 'apply', '--check', str(patch))
            git(xnu, 'apply', str(patch))
        print(f'Board patch sequence verified: {len(patches)} applied ({already} already present)')


if __name__ == '__main__':
    main()
