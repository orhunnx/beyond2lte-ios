#!/usr/bin/env python3
"""Prepare a writable development iOS root after copying its OS and cryptexes.

Run as root with volume ownership enabled. This applies generic A5-derived
Setup/migration preferences, creates runtime directories, and adds cryptex
fallback hard links for ignition_level=1. It does not patch Apple executables,
create MCM container identities, create activation records, or disable daemons.
Activation still requires mobileactivationd's native Hactivation gate; seeded
preferences alone do not establish an activated device. See --dry-run first.
"""
import argparse
import json
import os
from pathlib import Path
import plistlib
import stat
import tempfile


class Root:
    def __init__(self, path, dry_run=False, verify=False):
        self.root = Path(path).resolve(strict=True)
        self.dry_run = dry_run
        self.verify = verify
        self.actions = []

    def path(self, relative):
        """Resolve image symlinks within the image, including absolute links."""
        parts = list(Path(relative).parts)
        if Path(relative).is_absolute():
            raise ValueError('Expected an image-relative path')
        done = []
        links = 0
        while parts:
            part = parts.pop(0)
            if part in ('', '.'):
                continue
            if part == '..':
                if not done:
                    raise ValueError('Path escapes image root')
                done.pop()
                continue
            candidate = self.root.joinpath(*done, part)
            if candidate.is_symlink():
                links += 1
                if links > 40:
                    raise ValueError('Too many symlinks in image path')
                target = Path(os.readlink(candidate))
                if target.is_absolute():
                    done = []
                    target_parts = list(target.parts)[1:]
                else:
                    target_parts = list(target.parts)
                parts = target_parts + parts
            else:
                done.append(part)
        return self.root.joinpath(*done)

    def record(self, action, relative, **details):
        self.actions.append(dict(action=action, path=str(relative), **details))

    def directory(self, relative, uid=0, gid=0, mode=0o755):
        path = self.path(relative)
        if path.exists() and not path.is_dir():
            raise ValueError(f'Not a directory: {relative}')
        self.record('directory', relative, uid=uid, gid=gid, mode=oct(mode))
        if self.verify:
            self.metadata(path, uid, gid, mode)
        elif not self.dry_run:
            path.mkdir(parents=True, exist_ok=True)
            os.chown(path, uid, gid)
            os.chmod(path, mode)

    def metadata(self, path, uid, gid, mode):
        st = path.stat()
        actual = (st.st_uid, st.st_gid, stat.S_IMODE(st.st_mode))
        if actual != (uid, gid, mode):
            raise ValueError(f'Unexpected ownership/mode: {path.relative_to(self.root)} {actual}')

    def save(self, relative, data, uid, gid, mode=0o600):
        path = self.path(relative)
        self.record('plist', relative, uid=uid, gid=gid, mode=oct(mode), keys=sorted(data))
        if self.verify:
            self.metadata(path, uid, gid, mode)
            if load(path) != data:
                raise ValueError(f'Preference verification failed: {relative}')
            return
        if self.dry_run:
            return
        if not path.parent.is_dir():
            raise ValueError(f'Missing prepared parent for {relative}')
        fd, temp = tempfile.mkstemp(prefix='.b2l-', dir=path.parent)
        try:
            with os.fdopen(fd, 'wb') as stream:
                plistlib.dump(data, stream, fmt=plistlib.FMT_BINARY)
                stream.flush()
                os.fsync(stream.fileno())
            os.chown(temp, uid, gid)
            os.chmod(temp, mode)
            os.replace(temp, path)
        finally:
            if os.path.exists(temp):
                os.unlink(temp)

    def pref(self, name, values):
        relative = f'private/var/mobile/Library/Preferences/{name}.plist'
        path = self.path(relative)
        data = load(path) if path.exists() else {}
        if not isinstance(data, dict):
            raise ValueError(f'Preference is not a dictionary: {name}')
        data.update(values)
        self.save(relative, data, 501, 501)


def load(path):
    with open(path, 'rb') as stream:
        return plistlib.load(stream)


def account_ids(root):
    accounts = {}
    for line in root.path('private/etc/passwd').read_text().splitlines():
        fields = line.split(':')
        if len(fields) >= 4 and fields[2].isdigit() and fields[3].isdigit():
            accounts[fields[0]] = (int(fields[2]), int(fields[3]))
    for name, expected in [('mobile', (501, 501)), ('_logd', (272, 272))]:
        if accounts.get(name) != expected:
            raise ValueError(f'Unexpected iOS {name} account mapping')


def directories(root):
    for relative, uid, gid, mode in [
        ('dev', 0, 0, 0o755),
        ('private/var/mobile', 501, 501, 0o711),
        ('private/var/mobile/Library', 501, 501, 0o755),
        ('private/var/mobile/Library/Preferences', 501, 501, 0o755),
        ('private/var/mobile/Library/Logs', 501, 501, 0o755),
        ('private/var/mobile/Library/Logs/CrashReporter', 501, 501, 0o755),
        ('private/var/root', 0, 0, 0o700),
        ('private/var/run', 0, 0, 0o755),
        ('private/var/tmp', 0, 0, 0o1777),
        ('private/var/vm', 0, 0, 0o700),
        ('private/var/containers', 0, 0, 0o755),
        ('private/var/containers/Bundle', 33, 33, 0o755),
        ('private/var/containers/Data', 0, 0, 0o755),
        ('private/var/containers/Data/System', 0, 0, 0o755),
        ('private/var/containers/Shared', 0, 0, 0o755),
        ('private/var/containers/Shared/SystemGroup', 0, 0, 0o755),
        ('private/var/containers/Temp', 0, 0, 0o755),
        ('private/var/db/diagnostics', 272, 272, 0o751),
        ('private/var/db/uuidtext', 272, 272, 0o755),
        ('private/var/db/diagnostics/timesync', 272, 272, 0o755),
    ]:
        root.directory(relative, uid, gid, mode)
    for relative in ['private/var/db/diagnostics', 'private/var/db/uuidtext',
                     'private/var/db/diagnostics/timesync']:
        marker = root.path(relative + '/.migrated')
        root.record('marker', relative + '/.migrated', uid=272, gid=272, mode='0o644')
        if root.verify:
            if not marker.is_file():
                raise ValueError('Missing log migration marker')
            root.metadata(marker, 272, 272, 0o644)
        elif not root.dry_run:
            marker.touch(exist_ok=True)
            os.chown(marker, 272, 272)
            os.chmod(marker, 0o644)


def activation_ark(root, build, version):
    base = root.path('private/var/containers/Data/System')
    candidates = []
    if base.is_dir():
        for entry in base.iterdir():
            if entry.is_symlink() or not entry.is_dir():
                continue
            meta = entry / '.com.apple.mobile_container_manager.metadata.plist'
            if meta.is_file() and load(meta).get('MCMMetadataIdentifier') == 'com.apple.mobileactivationd':
                candidates.append(entry)
    if len(candidates) > 1:
        raise ValueError('Multiple mobileactivationd MCM containers; refusing ambiguity')
    if not candidates:
        return 'skipped: no existing mobileactivationd MCM container; no identity invented'
    directory = candidates[0].relative_to(root.root).as_posix() + '/Library/internal'
    root.directory(str(Path(directory).parent), 501, 501)
    root.directory(directory, 501, 501)
    relative = directory + '/data_ark.plist'
    path = root.path(relative)
    data = load(path) if path.exists() else {}
    data.update({
        '-ActivationState': 'Activated', '-BrickState': False,
        '-BuildVersion': build, '-ProductVersion': version,
        '-DataMigrationCompleted': True, '-PreSydroDataMigrationCompleted': True,
    })
    root.save(relative, data, 501, 501)
    return 'seeded existing container; native Hactivation gate still required'


def cryptex_links(root):
    linked = 0
    for cryptex in ['OS', 'App']:
        base = root.path(f'private/preboot/Cryptexes/{cryptex}')
        if not base.is_dir():
            raise ValueError(f'Missing copied {cryptex} cryptex')
        for directory, dirs, files in os.walk(base, followlinks=False):
            # os.walk lists symlinks to directories separately; mirror the link.
            link_dirs = [name for name in dirs if Path(directory, name).is_symlink()]
            dirs[:] = [name for name in dirs if name not in link_dirs]
            for name in files + link_dirs:
                source = Path(directory, name)
                relative = source.relative_to(base).as_posix()
                destination = root.path(relative)
                # Preserve every OS-root counterpart, including broken symlinks.
                lexical = root.root / relative
                if lexical.is_symlink() or destination.exists():
                    continue
                mode = source.lstat().st_mode
                if not (stat.S_ISREG(mode) or stat.S_ISLNK(mode)):
                    raise ValueError(f'Unsupported cryptex node: {relative}')
                root.record('cryptex-link', relative, cryptex=cryptex,
                            kind='symlink' if stat.S_ISLNK(mode) else 'hardlink')
                linked += 1
                if root.verify:
                    raise ValueError(f'Missing cryptex fallback: {relative}')
                if root.dry_run:
                    continue
                destination.parent.mkdir(parents=True, exist_ok=True)
                if stat.S_ISLNK(mode):
                    os.symlink(os.readlink(source), destination)
                    st = source.lstat()
                    os.chown(destination, st.st_uid, st.st_gid, follow_symlinks=False)
                else:
                    os.link(source, destination)
    return linked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mounted_root', type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--dry-run', action='store_true')
    mode.add_argument('--verify', action='store_true', help='Read-only metadata and preference verification')
    parser.add_argument('--no-cryptex-links', action='store_true',
                        help='Only for a root whose cryptex fallback links are already supplied')
    parser.add_argument('--report', type=Path, help='Write local preparation receipt JSON')
    args = parser.parse_args()
    if not (args.dry_run or args.verify) and os.geteuid() != 0:
        parser.error('Run as root on a writable image with volume ownership enabled')
    root = Root(args.mounted_root, args.dry_run, args.verify)
    version = load(root.path('System/Library/CoreServices/SystemVersion.plist'))
    build, product = version['ProductBuildVersion'], version['ProductVersion']
    if not isinstance(build, str) or not isinstance(product, str) or not build or not product:
        raise ValueError('Invalid SystemVersion build or product version')
    account_ids(root)
    directories(root)
    root.pref('com.apple.migration', {
        'LastSystemVersion': build,
        'DMUserDataDisposition': {'SyntheticDidUpgrade': False, 'buildVersion': build}})
    root.pref('com.apple.purplebuddy', {
        'SetupDone': True, 'SetupFinishedAllSteps': True, 'SetupVersion': 11,
        'daemonizeNeedsToRun': False})
    root.pref('com.apple.purplebuddy.notbackedup', {key: False for key in (
        'ShouldRunDiagnosticsMiniBuddy', 'ShouldRunHSA2MiniBuddy',
        'ShouldRunKeychainSyncMiniBuddy', 'ShouldRunMesaMiniBuddy',
        'ShouldRunPaymentMiniBuddy', 'ShouldRunStandardMiniBuddy', 'SilentICDPUpgrade')})
    root.pref('com.apple.springboard', {'MCForceShowCloudConfigurationUI': False})
    root.pref('com.apple.managedconfiguration.notbackedup', {
        'LockdownActivationIndicatesCloudConfigurationAvailable': False})
    ark = activation_ark(root, build, product)
    links = 0 if args.no_cryptex_links else cryptex_links(root)
    report = {
        'product_version': product, 'build_version': build, 'dry_run': args.dry_run,
        'verification_only': args.verify,
        'setup_preferences': 'A5-derived keys; iOS runtime acceptance unverified',
        'activation_ark': ark, 'cryptex_fallback_links': links, 'actions': root.actions,
        'activation_runtime_verified': False,
        'apple_executables_modified': False,
    }
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'actions'}, indent=2))


if __name__ == '__main__':
    main()
