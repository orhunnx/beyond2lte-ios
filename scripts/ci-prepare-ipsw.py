#!/usr/bin/env python3
"""CI helper to inspect, extract, and split-compress iOS 26.6.2 (23G90) IPSW payloads."""
import argparse
import hashlib
import io
import json
import os
import plistlib
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile

IPSW_ME_URL = "https://api.ipsw.me/v4/device/iPhone17,2?type=ipsw"
DEFAULT_CHUNK_BYTES = 1950 * 1024 * 1024  # 1950 MiB (< 2 GiB FAT32-safe)


class RemoteFile(io.RawIOBase):
    """Seekable HTTP range-backed read stream with bounded retries."""

    def __init__(self, url, retries=5):
        self.url = url
        self.retries = retries
        self.pos = 0
        req = urllib.request.Request(url, method="HEAD")
        with urllib.request.urlopen(req, timeout=30) as resp:
            self.size = int(resp.headers["Content-Length"])

    def readable(self):
        return True

    def seekable(self):
        return True

    def seek(self, offset, whence=0):
        if whence == 0:
            self.pos = offset
        elif whence == 1:
            self.pos += offset
        elif whence == 2:
            self.pos = self.size + offset
        else:
            raise ValueError(f"Unsupported whence: {whence}")
        return self.pos

    def tell(self):
        return self.pos

    def read(self, n=-1):
        if n == -1:
            n = self.size - self.pos
        if n <= 0 or self.pos >= self.size:
            return b""
        end = min(self.size - 1, self.pos + n - 1)
        expected = end - self.pos + 1
        last_err = None
        for attempt in range(self.retries):
            try:
                req = urllib.request.Request(
                    self.url, headers={"Range": f"bytes={self.pos}-{end}"}
                )
                with urllib.request.urlopen(req, timeout=120) as resp:
                    data = resp.read()
                if not data or len(data) > expected:
                    raise IOError(f"Unexpected range length {len(data)} (expected <= {expected})")
                self.pos += len(data)
                return data
            except Exception as exc:
                last_err = exc
                if attempt + 1 < self.retries:
                    time.sleep(min(2 ** attempt, 10))
        raise RuntimeError(f"Range read bytes={self.pos}-{end} failed: {last_err}") from last_err


def resolve_ipsw_url(build_id="23G90", version="26.6.2"):
    """Resolve the iPhone17,2 IPSW URL for the target iOS build."""
    with urllib.request.urlopen(IPSW_ME_URL, timeout=30) as resp:
        data = json.load(resp)
    for fw in data.get("firmwares", []):
        if fw.get("buildid") == build_id or fw.get("version") == version:
            return fw["url"]
    raise RuntimeError(f"No firmware found for build={build_id} version={version}")


def read_manifest(ipsw_url):
    """Return OS, Cryptex1,SystemOS, and Cryptex1,AppOS paths from BuildManifest.plist."""
    required = ("OS", "Cryptex1,SystemOS", "Cryptex1,AppOS")
    with zipfile.ZipFile(RemoteFile(ipsw_url)) as zf:
        bm = plistlib.loads(zf.read("BuildManifest.plist"))
        ident = next(
            (i for i in bm["BuildIdentities"] if all(k in i.get("Manifest", {}) for k in required)),
            bm["BuildIdentities"][0],
        )
        manifest = ident["Manifest"]
        out = {
            "build": ident.get("Info", {}).get("BuildNumber", ""),
            "OS": manifest["OS"]["Info"]["Path"],
            "SystemOS": manifest["Cryptex1,SystemOS"]["Info"]["Path"],
            "AppOS": manifest["Cryptex1,AppOS"]["Info"]["Path"],
        }
        for key in ("OS", "SystemOS", "AppOS"):
            zf.getinfo(out[key])
        return out


def extract_entry(ipsw_url, entry, out_dir):
    """Stream one zip entry from the remote IPSW into out_dir."""
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, os.path.basename(entry))
    with zipfile.ZipFile(RemoteFile(ipsw_url)) as zf:
        with zf.open(entry) as src, open(out_path, "wb") as dst:
            shutil.copyfileobj(src, dst, length=8 * 1024 * 1024)
    return out_path


def compress_and_split(img_path, out_prefix, chunk_size=DEFAULT_CHUNK_BYTES):
    """Stream-compress img_path via pigz into fixed-size parts and write SHA256SUMS."""
    proc = subprocess.Popen(["pigz", "-c", img_path], stdout=subprocess.PIPE)
    part_idx, total, rem = 1, 0, chunk_size
    sums = []
    hasher = hashlib.sha256()
    current_path = f"{out_prefix}{part_idx}"
    fh = open(current_path, "wb")
    try:
        while True:
            data = proc.stdout.read(min(8 * 1024 * 1024, rem))
            if not data:
                break
            fh.write(data)
            hasher.update(data)
            total += len(data)
            rem -= len(data)
            if rem == 0:
                fh.close()
                sums.append((os.path.basename(current_path), os.path.getsize(current_path), hasher.hexdigest()))
                part_idx += 1
                rem = chunk_size
                hasher = hashlib.sha256()
                current_path = f"{out_prefix}{part_idx}"
                fh = open(current_path, "wb")
    finally:
        if not fh.closed:
            fh.close()

    if os.path.getsize(current_path) == 0:
        os.remove(current_path)
    else:
        sums.append((os.path.basename(current_path), os.path.getsize(current_path), hasher.hexdigest()))

    rc = proc.wait()
    if rc != 0:
        raise RuntimeError(f"pigz exited with status {rc}")

    sums_path = os.path.join(os.path.dirname(out_prefix), "SHA256SUMS")
    with open(sums_path, "w") as sf:
        for name, sz, digest in sums:
            sf.write(f"{digest}  {name}\n")
            print(f"{name}: {sz} bytes sha256={digest}")
    print(f"Total compressed size: {total} bytes across {len(sums)} parts")


def extract_launchd_cache(img_path, out_path):
    """Extract /System/Library/xpc/launchd.plist cleanly from an unmounted HFS+ image."""
    import importlib.util

    hfs_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pull-b2l-unified-logs.py")
    spec = importlib.util.spec_from_file_location("hfsreader", hfs_script)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)

    reader = mod.ImageReader(img_path)
    try:
        volume = mod.Volume(reader)
        target = "/System/Library/xpc/launchd.plist"
        source = None
        for entry, full, _, _ in volume.selected((target,)):
            if full == target:
                source = b"".join(
                    mod.Fork(volume, entry["fork_raw"], entry["id"]).resolve(volume.overflow).content()
                )
                break
        if source is None or not volume.unchanged():
            raise RuntimeError("Missing/changed /System/Library/xpc/launchd.plist in HFS image")
        parsed = plistlib.loads(source)
        if "LaunchDaemons" not in parsed:
            raise RuntimeError("Unexpected launchd.plist structure (missing LaunchDaemons)")
        os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
        with open(out_path, "wb") as fh:
            fh.write(source)
        print(f"Extracted {target}: {len(source)} bytes sha256={hashlib.sha256(source).hexdigest()}")
    finally:
        reader.close()


def verify_root(mounted_root):
    """Verify 23G90 build identity, dyld shared cache, dyld, and extensions.plist inside mounted_root."""
    import importlib.util

    prep_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "prepare-ios-root.py")
    spec = importlib.util.spec_from_file_location("prepare_ios_root", prep_path)
    prep = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prep)

    root = prep.Root(mounted_root, verify=True)
    sv_path = root.path("System/Library/CoreServices/SystemVersion.plist")
    with open(sv_path, "rb") as fh:
        sv = plistlib.load(fh)
    print(f"SystemVersion: {sv.get('ProductVersion')} ({sv.get('ProductBuildVersion')})")
    if sv.get("ProductBuildVersion") != "23G90":
        raise RuntimeError(f"Expected ProductBuildVersion 23G90, got {sv.get('ProductBuildVersion')!r}")

    required = [
        "usr/lib/dyld",
        "System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e",
        "private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e",
        "System/Library/xpc/extensions.plist",
    ]
    for rel in required:
        resolved = root.path(rel)
        if not resolved.is_file() or resolved.stat().st_size == 0:
            raise RuntimeError(f"Missing or empty required runtime file: {rel} -> {resolved}")
        print(f"OK {rel}: {resolved.stat().st_size} bytes")

    ext = root.path("System/Library/xpc/extensions.plist")
    ext_hash = hashlib.sha256(ext.read_bytes()).hexdigest()
    print(f"extensions.plist sha256={ext_hash} size={ext.stat().st_size}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_url = sub.add_parser("resolve-url")
    p_url.add_argument("--build", default="23G90")
    p_url.add_argument("--version", default="26.6.2")

    p_man = sub.add_parser("manifest")
    p_man.add_argument("ipsw_url")

    p_ext = sub.add_parser("extract")
    p_ext.add_argument("ipsw_url")
    p_ext.add_argument("entry")
    p_ext.add_argument("out_dir")

    p_cmp = sub.add_parser("compress-split")
    p_cmp.add_argument("img_path")
    p_cmp.add_argument("out_prefix")

    p_ver = sub.add_parser("verify-root")
    p_ver.add_argument("mounted_root")

    p_ld = sub.add_parser("extract-launchd")
    p_ld.add_argument("img_path")
    p_ld.add_argument("out_path")

    args = parser.parse_args()
    if args.cmd == "resolve-url":
        print(resolve_ipsw_url(args.build, args.version))
    elif args.cmd == "manifest":
        print(json.dumps(read_manifest(args.ipsw_url)))
    elif args.cmd == "extract":
        print(extract_entry(args.ipsw_url, args.entry, args.out_dir))
    elif args.cmd == "compress-split":
        compress_and_split(args.img_path, args.out_prefix)
    elif args.cmd == "verify-root":
        verify_root(args.mounted_root)
    elif args.cmd == "extract-launchd":
        extract_launchd_cache(args.img_path, args.out_path)


if __name__ == "__main__":
    main()
