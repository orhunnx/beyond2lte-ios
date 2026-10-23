#!/usr/bin/env python3
"""Read-only native unified log export from the B2L HFS+ volume in recovery.

Never mounts a volume, replays a journal, writes the phone, or reboots it.
A dirty HFS volume is exported as observed, with that limitation in the receipt.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import struct
import subprocess
import sys
import time

SERIAL = 'R58M33609TR'
DEVICE = '/dev/block/sda31'
CHUNK = 8 * 1024 * 1024
MAX_METADATA = 512 * 1024 * 1024

class ExportError(Exception):
    pass

def require(ok, message):
    if not ok:
        raise ExportError(message)

def u16(data, off=0):
    return struct.unpack_from('>H', data, off)[0]

def u32(data, off=0):
    return struct.unpack_from('>I', data, off)[0]

def u64(data, off=0):
    return struct.unpack_from('>Q', data, off)[0]

def digest(data):
    return hashlib.sha256(data).hexdigest()

class ImageReader:
    def __init__(self, path):
        self.file = Path(path).open('rb')
        self.size = os.fstat(self.file.fileno()).st_size
        self.identity = {'transport': 'local image', 'path': str(Path(path).resolve())}
    def close(self):
        self.file.close()
    def __del__(self):
        file = getattr(self, 'file', None)
        if file is not None:
            file.close()
    def read(self, offset, size):
        require(0 <= offset <= self.size and 0 <= size <= self.size - offset, 'read outside image')
        self.file.seek(offset)
        data = self.file.read(size)
        require(len(data) == size, 'short image read')
        return data

class AdbReader:
    def __init__(self, adb):
        self.adb = [adb, '-s', SERIAL]
        require(self.call('get-state').strip() == b'recovery', 'target must be in recovery')
        require(self.shell('getprop ro.product.device').strip() == b'beyond2lte', 'wrong product')
        require(self.shell('id -u').strip() == b'0', 'recovery must provide root')
        resolved = self.shell('readlink -f ' + DEVICE).decode().strip()
        require(resolved == DEVICE, 'unexpected USERDATA block device')
        mounts = self.shell('cat /proc/mounts').decode(errors='replace')
        for line in mounts.splitlines():
            if not line.split():
                continue
            source = line.split()[0]
            if source.startswith('/dev/block/'):
                source = self.shell('readlink -f ' + shlex.quote(source)).decode().strip()
            require(source != DEVICE, 'source volume is mounted; export requires a quiescent recovery volume')
        self.size = int(self.shell('blockdev --getsize64 ' + DEVICE).strip())
        require(1024 * 1024 <= self.size <= 2**50 and self.size % 4096 == 0, 'invalid block device size')
        self.identity = {'transport': 'ADB read only', 'serial': SERIAL,
                         'product': 'beyond2lte', 'state': 'recovery', 'device': DEVICE}
    def call(self, *args):
        return subprocess.check_output(self.adb + list(args), timeout=180)
    def shell(self, command):
        return self.call('shell', '-T', command)
    def read(self, offset, size):
        require(0 <= offset <= self.size and 0 <= size <= self.size - offset, 'read outside block device')
        if not size:
            return b''
        first = offset // 4096
        count = (offset % 4096 + size + 4095) // 4096
        data = self.call('exec-out', 'sh', '-c',
                         f'dd if={DEVICE} bs=4096 skip={first} count={count} 2>/dev/null')
        require(len(data) == count * 4096, 'short ADB block read')
        return data[offset % 4096:offset % 4096 + size]

class Fork:
    def __init__(self, volume, raw, ident):
        require(len(raw) == 80, 'invalid HFS fork size')
        self.volume, self.ident = volume, ident
        self.size, self.blocks = u64(raw), u32(raw, 12)
        require(self.blocks <= volume.blocks and self.size <= self.blocks * volume.bs, 'invalid fork length')
        self.extents = self.parse_extents(raw[16:80])
        require(self.mapped <= self.blocks, 'inline extents exceed fork allocation')
    @staticmethod
    def parse_extents(raw):
        require(len(raw) == 64, 'invalid extent record')
        result, ended = [], False
        for off in range(0, 64, 8):
            start, count = u32(raw, off), u32(raw, off + 4)
            if not count:
                require(start == 0, 'nonzero empty extent')
                ended = True
            else:
                require(not ended, 'nonempty extent after empty extent')
                result.append((start, count))
        return result
    @property
    def mapped(self):
        return sum(count for _, count in self.extents)
    def validate(self):
        require(self.mapped == self.blocks, f'incomplete fork extents for CNID {self.ident}')
        ranges = sorted((start, start + count) for start, count in self.extents)
        for i, (start, end) in enumerate(ranges):
            require(0 <= start < end <= self.volume.blocks, 'extent outside HFS volume')
            require(not i or ranges[i - 1][1] <= start, 'overlapping fork extents')
    def extend(self, extents):
        require(bool(extents), 'empty overflow extent record')
        require(self.mapped + sum(n for _, n in extents) <= self.blocks, 'overflow exceeds fork allocation')
        self.extents.extend(extents)
    def resolve(self, overflow):
        while self.mapped < self.blocks:
            key = (self.ident, 0, self.mapped)
            require(key in overflow, f'missing extent overflow record {key}')
            self.extend(overflow[key])
        self.validate()
        return self
    def read(self, offset, size):
        require(0 <= offset <= self.size and 0 <= size <= self.size - offset, 'read outside fork')
        result = bytearray()
        logical = 0
        for start, count in self.extents:
            require(start + count <= self.volume.blocks, 'extent outside volume')
            length = count * self.volume.bs
            if offset < logical + length and size:
                inside = max(offset - logical, 0)
                take = min(size, length - inside)
                block = self.volume.reader.read(start * self.volume.bs + inside, take)
                require(len(block) == take, 'short physical extent read')
                result.extend(block)
                offset += take
                size -= take
            logical += length
        require(size == 0, 'fork range not mapped (extent-tree bootstrap unavailable)')
        return bytes(result)
    def content(self):
        for off in range(0, self.size, CHUNK):
            yield self.read(off, min(CHUNK, self.size - off))
    def metadata(self):
        require(self.size <= MAX_METADATA, 'metadata fork exceeds export memory bound')
        return b''.join(self.content())

class BytesFork:
    def __init__(self, data):
        self.data, self.size = data, len(data)
    def read(self, offset, size):
        require(0 <= offset <= self.size and size <= self.size - offset, 'B-tree read outside fork')
        return self.data[offset:offset + size]

class BTree:
    def __init__(self, fork):
        self.fork = fork
        probe = fork.read(0, min(fork.size, 512))
        require(len(probe) >= 64, 'short B-tree header')
        self.ns = u16(probe, 32)
        require(512 <= self.ns <= 32768 and not self.ns & (self.ns - 1), 'invalid B-tree node size')
        header, records = self.node(0)
        require(header[8] == 1 and records and len(records[0]) >= 42, 'invalid B-tree header node')
        h = records[0]
        self.depth, self.root, self.leaf_records = u16(h), u32(h, 2), u32(h, 6)
        self.first, self.last, self.total = u32(h, 10), u32(h, 14), u32(h, 22)
        require(self.total and self.total * self.ns <= fork.size, 'B-tree node count exceeds fork')
        require(0 <= self.depth <= 32 and self.root < self.total and self.first < self.total and self.last < self.total,
                'invalid B-tree root or leaf header')
        require(bool(self.first) == bool(self.last) == bool(self.leaf_records), 'inconsistent empty B-tree')
    def node(self, index):
        if hasattr(self, 'total'):
            require(0 <= index < self.total, 'B-tree node index outside bounds')
        node = self.fork.read(index * self.ns, self.ns)
        require(len(node) == self.ns, 'short B-tree node')
        count = u16(node, 10)
        require(count < self.ns // 4, 'invalid B-tree record count')
        limit = self.ns - 2 * (count + 1)
        offsets = [u16(node, self.ns - 2 * (i + 1)) for i in range(count + 1)]
        require(all(14 <= x <= limit for x in offsets), 'B-tree record offset outside data region')
        require(all(a < b for a, b in zip(offsets, offsets[1:])), 'unordered B-tree record offsets')
        return node, [node[a:b] for a, b in zip(offsets, offsets[1:])]
    def leaves(self):
        current, previous, count, seen = self.first, 0, 0, set()
        while current:
            require(current not in seen and len(seen) < self.total, 'cyclic B-tree leaf chain')
            seen.add(current)
            node, records = self.node(current)
            require(node[8] == 255 and node[9] == 1 and u32(node, 4) == previous, 'invalid B-tree leaf/backlink')
            count += len(records)
            yield from records
            previous, current = current, u32(node)
            require(current < self.total, 'B-tree forward link outside bounds')
        require(previous == self.last and count == self.leaf_records, 'incomplete B-tree leaf chain')
    @staticmethod
    def extent_key(record):
        require(len(record) >= 12 and u16(record) == 10 and record[3] == 0, 'invalid overflow key')
        require(record[2] in (0, 255), 'invalid overflow fork type')
        return (u32(record, 4), record[2], u32(record, 8))
    def lookup_extent(self, key):
        """Bootstrap the overflow file itself without requiring unmapped nodes."""
        current, seen, height = self.root, set(), self.depth
        while current:
            require(current not in seen, 'cyclic overflow index')
            seen.add(current)
            node, records = self.node(current)
            require(node[9] == height, 'invalid overflow tree height')
            keys = [self.extent_key(r) for r in records]
            require(all(a < b for a, b in zip(keys, keys[1:])), 'unordered overflow index keys')
            if node[8] == 255:
                require(height == 1, 'invalid overflow leaf height')
                for k, r in zip(keys, records):
                    if k == key:
                        require(len(r) == 76, 'invalid overflow leaf record length')
                        return Fork.parse_extents(r[12:76])
                raise ExportError(f'extent-tree bootstrap key missing {key}')
            require(node[8] == 0 and height > 1, 'invalid overflow index node')
            candidates = [r for k, r in zip(keys, records) if k <= key]
            require(bool(candidates), 'overflow lookup before first index key')
            chosen = candidates[-1]
            require(len(chosen) == 16, 'invalid overflow index record length')
            current, height = u32(chosen, 12), height - 1
        raise ExportError('empty extent-tree bootstrap lookup')

class Volume:
    def __init__(self, reader):
        self.reader = reader
        self.header = reader.read(1024, 512)
        h = self.header
        require(len(h) == 512, 'short HFS volume header')
        require(h[:2] in (b'H+', b'HX') and u16(h, 2) in (4, 5), 'not an HFS+/HFSX volume')
        self.bs, self.blocks = u32(h, 40), u32(h, 44)
        require(512 <= self.bs <= 1048576 and not self.bs & (self.bs - 1), 'invalid HFS block size')
        require(self.blocks and self.blocks * self.bs <= reader.size, 'HFS geometry exceeds partition')
        ef = Fork(self, h[192:272], 3)
        if ef.mapped < ef.blocks:
            bootstrap = BTree(ef)
            while ef.mapped < ef.blocks:
                ef.extend(bootstrap.lookup_extent((3, 0, ef.mapped)))
        ef.validate()
        self.extents_data = ef.metadata()
        self.overflow = {}
        for record in BTree(BytesFork(self.extents_data)).leaves():
            key = BTree.extent_key(record)
            require(len(record) == 76 and key not in self.overflow, 'invalid/duplicate overflow record')
            self.overflow[key] = Fork.parse_extents(record[12:76])
        cf = Fork(self, h[272:352], 4).resolve(self.overflow)
        self.catalog_data = cf.metadata()
        self.catalog = BTree(BytesFork(self.catalog_data))
    def entries(self):
        for record in self.catalog.leaves():
            require(len(record) >= 10, 'short catalog record')
            length, chars = u16(record), u16(record, 6)
            require(length == 6 + chars * 2 and length + 4 <= len(record), 'invalid catalog key')
            parent = u32(record, 2)
            name = record[8:8 + chars * 2].decode('utf-16-be')
            data = record[2 + length:]
            kind = u16(data)
            if kind not in (1, 2):
                continue
            require(len(data) >= (88 if kind == 1 else 168), 'short catalog folder/file record')
            item = {'id': u32(data, 8), 'parent': parent, 'name': name, 'kind': kind}
            if kind == 2:
                item['fork_raw'] = data[88:168]
            yield item
    def selected(self, include_prefixes=()):
        folders = {}
        for e in self.entries():
            if e['kind'] == 1:
                require(e['id'] not in folders, 'duplicate folder CNID')
                folders[e['id']] = e
        for e in self.entries():
            if e['kind'] != 2:
                continue
            names, seen, parent = [e['name']], set(), e['parent']
            while parent != 2:
                require(parent not in seen and parent in folders, 'catalog folder cycle or missing parent')
                seen.add(parent)
                p = folders[parent]
                names.append(p['name'])
                parent = p['parent']
            full = '/' + '/'.join(reversed(names))
            selection = archive_path(full, include_prefixes)
            if selection:
                rel, essential = selection
                for component in names:
                    require(len(safe_relative(component)) == 1, 'unsafe HFS name component')
                safe_relative(rel)
                yield e, full, rel, essential
    def unchanged(self):
        return self.reader.read(1024, 512) == self.header

def safe_relative(name):
    require(isinstance(name, str) and name and not name.startswith('/') and '\\' not in name and '\0' not in name,
            'unsafe archive path')
    parts = name.split('/')
    require(all(p not in ('', '.', '..') and all(ord(c) >= 32 for c in p) for p in parts), 'unsafe archive component')
    require(str(PurePosixPath(name)) == name, 'noncanonical archive path')
    return parts

def archive_path(full, include_prefixes=()):
    """Select only native unified-log data, not arbitrary var/db contents."""
    diag = '/private/var/db/diagnostics/'
    uuid = '/private/var/db/uuidtext/'
    if full.startswith(diag):
        rel = full[len(diag):]
        parts = rel.split('/')
        if len(parts) == 2 and parts[0] in ('Persist', 'Special', 'Signpost', 'HighVolume') and parts[1].endswith('.tracev3'):
            return rel, True
        if rel == 'version.plist' or (len(parts) == 2 and parts[0] == 'timesync' and parts[1].endswith('.timesync')):
            return rel, True
        if len(parts) == 1 and re.fullmatch(r'(logd(?:_helper)?\.\d+\.log|logdata\.statistics\.\d+\.(?:jsonl|txt))', rel):
            return rel, False
    if full.startswith(uuid):
        rel = full[len(uuid):]
        if rel == '.migrated':
            return None
        if re.fullmatch(r'(?:[0-9a-fA-F]{2}/[0-9a-fA-F]{30}|dsc/[0-9a-fA-F]{32})', rel):
            return 'uuidtext/' + rel, True
    for prefix in include_prefixes:
        if full == prefix or full.startswith(prefix + '/'):
            return 'supplemental/' + full.lstrip('/'), False
    return None

def create_file(root, relative):
    parts = safe_relative(relative)
    parent = root
    for part in parts[:-1]:
        parent = parent / part
        parent.mkdir(exist_ok=True)
        require(not parent.is_symlink() and parent.resolve().is_relative_to(root), 'output directory escaped archive')
    path = parent / parts[-1]
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    return path, os.fdopen(fd, 'wb')

def export(reader, output, include_prefixes=()):
    output = Path(output).absolute()
    require(output.name.endswith('.logarchive'), 'output must end with .logarchive')
    require(output.parent.is_dir(), 'output parent directory must already exist')
    output.mkdir(mode=0o700, exist_ok=False)
    output = output.resolve()
    receipt = {'status': 'in progress', 'complete': False, 'source': reader.identity,
               'started_unix': time.time(), 'phone_writes': False, 'files': [], 'errors': []}
    receipt_path = output / 'b2l-export-receipt.json'
    def save():
        receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
    save()
    try:
        volume = Volume(reader)
        receipt['volume'] = {'block_size': volume.bs, 'total_blocks': volume.blocks,
            'header_sha256': digest(volume.header), 'catalog_sha256': digest(volume.catalog_data),
            'extents_sha256': digest(volume.extents_data), 'overflow_records': len(volume.overflow),
            'clean_unmount_flag': bool(u32(volume.header, 4) & 0x100),
            'journal_replayed': False}
        selected = list(volume.selected(include_prefixes))
        require(bool(selected), 'no unified-log files found')
        required_bytes = sum(u64(e['fork_raw']) for e, _, _, _ in selected)
        disk = os.statvfs(output)
        require(required_bytes + 16 * 1024 * 1024 <= disk.f_bavail * disk.f_frsize, 'insufficient host disk space')
        for directory in ('Persist', 'Special', 'Signpost', 'HighVolume', 'timesync', 'uuidtext'):
            (output / directory).mkdir(exist_ok=True)
        for e, full, relative, essential in selected:
            item = {'source_path': full, 'archive_path': relative, 'cnid': e['id'],
                    'essential': essential, 'size': u64(e['fork_raw']), 'complete': False}
            receipt['files'].append(item)
            target = None
            try:
                fork = Fork(volume, e['fork_raw'], e['id'])
                fork.resolve(volume.overflow)
                target, stream = create_file(output, relative)
                sha, count = hashlib.sha256(), 0
                with stream:
                    for block in fork.content():
                        stream.write(block)
                        count += len(block)
                        sha.update(block)
                require(count == fork.size and target.stat().st_size == fork.size, 'incomplete output file')
                item.update(complete=True, sha256=sha.hexdigest(), extents=len(fork.extents))
                print(f'{relative}: {count} bytes', flush=True)
            except (OSError, ExportError, subprocess.SubprocessError) as error:
                item['error'] = str(error)
                if target is not None:
                    target.unlink(missing_ok=True)
                if essential:
                    receipt['errors'].append(f'{relative}: {error}')
            save()
        require(volume.unchanged(), 'volume header changed during export; snapshot is not stable')
        receipt['header_unchanged'] = True
        receipt['complete'] = not receipt['errors'] and all(x['complete'] for x in receipt['files'] if x['essential'])
        receipt['all_selected_complete'] = all(x['complete'] for x in receipt['files'])
        receipt['status'] = ('complete observed snapshot' if receipt['all_selected_complete'] else
                             'core snapshot complete with supplemental omissions') if receipt['complete'] else 'incomplete'
        receipt['limitations'] = ['Read-only export does not replay the HFS journal or recover uncommitted records.',
            'Stable header and recovery state reduce concurrent-write risk; this is not an atomic storage snapshot.',
            'Native log decoding also requires matching uuidtext/dsc, timesync and OS tools; private fields may be redacted.']
    except BaseException as error:
        receipt['errors'].append(f'{type(error).__name__}: {error}')
        receipt['status'] = 'incomplete'
        if isinstance(error, (KeyboardInterrupt, SystemExit)):
            raise
    finally:
        receipt['finished_unix'] = time.time()
        save()
    return receipt

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='new host .logarchive directory; never overwritten')
    parser.add_argument('--adb', default='/opt/homebrew/bin/adb')
    parser.add_argument('--include-prefix', action='append', default=[], help='also export files below an absolute HFS path to supplemental/; repeatable')
    parser.add_argument('--include-crash-reports', action='store_true', help='include all mobile/root/system logs as supplemental files')
    parser.add_argument('--image', type=Path, help='local raw HFS partition image instead of live ADB (offline/fixture)')
    args = parser.parse_args()
    try:
        prefixes = list(args.include_prefix)
        if args.include_crash_reports:
            prefixes += ['/private/var/mobile/Library/Logs', '/private/var/root/Library/Logs',
                         '/private/var/log', '/Library/Logs', '/private/var/db/diagnostics']
        for prefix in prefixes:
            require(prefix.startswith('/') and len(prefix) > 1, 'include prefix must be absolute')
            safe_relative(prefix[1:])
        reader = ImageReader(args.image) if args.image else AdbReader(args.adb)
        receipt = export(reader, args.output, prefixes)
        print(json.dumps({'archive': str(args.output), 'complete': receipt['complete'], 'errors': receipt['errors']}))
        return 0 if receipt['complete'] else 2
    except (OSError, ValueError, ExportError, subprocess.SubprocessError) as error:
        print(f'export failed: {error}', file=sys.stderr)
        return 2

if __name__ == '__main__':
    sys.exit(main())
