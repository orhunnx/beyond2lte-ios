#!/usr/bin/env python3
"""Package external beyond2lte UEFI + embedded XNU loader + a BOOT trailer.

No phone I/O or flashing is performed. The Android ramdisk is empty; XNU remains
on disk until the firmware loader reads its bounded trailer through UFS.
"""
import argparse
import hashlib
import json
import lzma
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import uuid

FD_BASE = 0xC9000000
FD_SIZE = 0x400000
PAGE = 2048
MAGIC = b"B2LXNU02"
PAYLOAD_VERSION = 2
HEADER_SIZE = 320
SEED_SIZE = 256


def align(value, alignment=PAGE):
    return (value + alignment - 1) & -alignment


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_wifi_mac(path):
    """Read a private local file; never return its value in error text."""
    import re
    text = path.read_text().strip()
    if not re.fullmatch(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", text):
        raise ValueError("WiFi MAC file must contain exactly six colon-separated octets")
    mac = bytes.fromhex(text.replace(":", ""))
    if not any(mac) or mac[0] & 1:
        raise ValueError("WiFi MAC must be a nonzero unicast address")
    return mac


def wifi_metadata(mac=None):
    if mac is None:
        return bytes(32)
    if len(mac) != 6 or not any(mac) or mac[0] & 1:
        raise ValueError("WiFi metadata requires six nonzero unicast address octets")
    return b"B2LWIFI1" + mac + bytes(18)


def check_wifi_metadata(field):
    if field == bytes(32):
        return False
    if len(field) != 32 or field[:8] != b"B2LWIFI1" or any(field[14:]):
        raise ValueError("invalid optional BOOT WiFi metadata")
    wifi_metadata(field[8:14])
    return True


def make_payload_header(kernel_size, wifi_mac=None):
    """Supply the full XNU boot seed directly from the host operating system."""
    header = struct.pack("<8sIIQQ", MAGIC, PAYLOAD_VERSION, HEADER_SIZE, kernel_size, HEADER_SIZE)
    return header + os.urandom(SEED_SIZE) + wifi_metadata(wifi_mac)


def volume_contains_loader(data, loader):
    """Match the loader only within a bounded FV, including FV_IMAGE sections."""
    def match_fv(candidate):
        if len(candidate) < 56 or candidate[40:44] != b"_FVH":
            return False
        length = struct.unpack_from("<Q", candidate, 32)[0]
        header_length = struct.unpack_from("<H", candidate, 48)[0]
        if length > len(candidate) or header_length < 56 or header_length > length:
            return False
        return loader in candidate[:length]

    if match_fv(data):
        return True
    # GenFv wraps the FV in a section stream. Alignment RAW sections can precede
    # EFI_SECTION_FIRMWARE_VOLUME_IMAGE (0x17), so the FV need not start at zero.
    position = 0
    while position + 4 <= len(data):
        size = int.from_bytes(data[position:position + 3], "little")
        section_type = data[position + 3]
        header_size = 4
        if size == 0xffffff:
            if position + 8 > len(data):
                return False
            size = struct.unpack_from("<I", data, position + 4)[0]
            header_size = 8
        if size < header_size or size > len(data) - position:
            return False
        if section_type == 0x17 and match_fv(data[position + header_size:position + size]):
            return True
        position = align(position + size, 4)
    return False


def firmware_contains_loader(firmware, loader):
    """Inspect the real FD's guided LZMA section, not just an adjacent map file."""
    marker = uuid.UUID("EE4E5898-3914-4259-9D6E-DC7BD79403CF").bytes_le
    search = 0
    while (position := firmware.find(marker, search)) >= 0:
        search = position + len(marker)
        section = position - 4
        if section < 0 or section + 24 > len(firmware):
            continue
        size = int.from_bytes(firmware[section:section + 3], "little")
        data_offset = struct.unpack_from("<H", firmware, section + 20)[0]
        if firmware[section + 3] != 2 or data_offset < 24 or size <= data_offset or section + size > len(firmware):
            continue
        try:
            decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
            volume = decoder.decompress(firmware[section + data_offset:section + size], max_length=64 * 1024 * 1024)
        except lzma.LZMAError:
            continue
        if decoder.eof and volume_contains_loader(volume, loader):
            return True
    return False


def check_boot(path):
    """Validate container arithmetic independently of the pack command."""
    data = path.read_bytes()
    if len(data) < PAGE or data[:8] != b"ANDROID!":
        raise ValueError("missing Android BOOT header")
    kernel_size, _, ramdisk_size, _, second_size = struct.unpack_from("<5I", data, 8)
    page, version = struct.unpack_from("<2I", data, 36)
    if page != PAGE or version != 1 or ramdisk_size or second_size:
        raise ValueError("expected Android v1, 2048-byte pages and empty ramdisk/second")
    if struct.unpack_from("<I", data, 1644)[0] != 1648:
        raise ValueError("incorrect Android v1 header_size")
    trailer = PAGE + align(kernel_size)
    if trailer + HEADER_SIZE > len(data):
        raise ValueError("BOOT trailer is truncated")
    magic, payload_version, header_size, size, offset = struct.unpack_from("<8sIIQQ", data, trailer)
    if magic == b"B2LXNU01":
        raise ValueError("legacy BOOT trailer provides only 64 seed bytes; repackage v2 with 256 seed bytes")
    if (magic, payload_version, header_size, offset) != (MAGIC, PAYLOAD_VERSION, HEADER_SIZE, HEADER_SIZE):
        raise ValueError("invalid BOOT trailer header")
    if size < 4096 or size > 128 * 1024 * 1024 or trailer + offset + size != len(data):
        raise ValueError("invalid BOOT trailer size")
    wifi_present = check_wifi_metadata(data[trailer + 288:trailer + 320])
    macho = trailer + offset
    if struct.unpack_from("<III", data, macho) != (0xfeedfacf, 0x0100000c, 0):
        raise ValueError("XNU must be a thin, plain ARM64 Mach-O image")
    return {"android_kernel_bytes": kernel_size, "trailer_file_offset": trailer,
            "xnu_bytes": size, "boot_bytes": len(data), "ramdisk_bytes": 0,
            "payload_version": PAYLOAD_VERSION, "seed_bytes": SEED_SIZE,
            "wifi_mac_present": wifi_present}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mu", type=Path, help="Disposable external Mu-Silicium build tree")
    p.add_argument("--firmware", type=Path, help="4 MiB FD rebuilt with XnuLoader embedded")
    p.add_argument("--kernel", type=Path, help="Plain ARM64 mach.development.b2l")
    p.add_argument("--loader", type=Path, help="XnuLoader.efi embedded in that FD")
    p.add_argument("--output-dir", type=Path)
    p.add_argument("--wifi-mac-file", type=Path, help="Private local six-octet WiFi MAC; keep file and personalized image outside Git")
    p.add_argument("--max-image-bytes", type=lambda x: int(x, 0), help="Explicit offline packaging size ceiling")
    p.add_argument("--boot-partition-bytes", type=lambda x: int(x, 0), help="Confirmed target BOOT capacity, if available")
    p.add_argument("--clang", default=shutil.which("clang"))
    p.add_argument("--objcopy", default=shutil.which("llvm-objcopy") or "/opt/homebrew/opt/llvm/bin/llvm-objcopy")
    p.add_argument("--verify", type=Path, help="Only validate an existing packaged image")
    a = p.parse_args()
    if a.verify:
        print(json.dumps(check_boot(a.verify), indent=2))
        return
    for name in ("mu", "firmware", "kernel", "loader", "output_dir", "max_image_bytes"):
        if getattr(a, name) is None:
            p.error(f"--{name.replace('_', '-')} is required when packaging")
    if a.max_image_bytes <= 0:
        p.error("size ceiling must be positive")
    fd = a.firmware.read_bytes()
    kernel = a.kernel.read_bytes()
    if len(fd) != FD_SIZE:
        p.error("firmware FD must be exactly 4 MiB; stack/framebuffer layout cannot be enlarged")
    if len(kernel) < 4096 or struct.unpack_from("<III", kernel) != (0xfeedfacf, 0x0100000c, 0):
        p.error("kernel must be a thin plain ARM64 Mach-O image")
    if not a.loader.is_file() or a.loader.read_bytes()[:2] != b"MZ":
        p.error("--loader must be the built ARM64 EFI application")
    if not firmware_contains_loader(fd, a.loader.read_bytes()):
        p.error("the supplied EFI loader is not embedded in this FD's compressed firmware volume; rebuild the XNU firmware profile first")
    dtb_path = a.mu / "Resources/DTBs/beyond2lte.dtb"
    dtb = dtb_path.read_bytes()
    if len(dtb) < 40 or struct.unpack_from(">I", dtb)[0] != 0xd00dfeed:
        p.error("reference beyond2lte DTB is invalid")
    a.output_dir.mkdir(parents=True, exist_ok=True)
    intermediates = a.output_dir / "components"
    intermediates.mkdir(exist_ok=True)
    shim_elf = intermediates / "BootShim.elf"
    shim_bin = intermediates / "BootShim.bin"
    subprocess.run([a.clang, "--target=aarch64-linux-gnu", "-c", str(a.mu / "BootShim/BootShim.S"),
                    "-o", str(shim_elf), "-DREQUIRES_KERNEL_HEADER=1", f"-DFD_BASE={FD_BASE}",
                    f"-DFD_SIZE={FD_SIZE}"], check=True)
    subprocess.run([a.objcopy, "-O", "binary", str(shim_elf), str(shim_bin)], check=True)
    shim = shim_bin.read_bytes()
    if len(shim) % 16 or shim[56:60] != b"ARM\x64" or struct.unpack_from("<QQ", shim, 8) != (FD_BASE, FD_SIZE):
        p.error("unexpected BootShim layout")
    android_kernel = intermediates / "uefi-kernel.bin"
    android_kernel.write_bytes(shim + fd + dtb)
    empty_ramdisk = intermediates / "empty-ramdisk"
    empty_ramdisk.write_bytes(b"")
    image = a.output_dir / "boot.img"
    subprocess.run([sys.executable, str(a.mu / "Resources/Scripts/mkbootimg.py"),
                    "--kernel", str(android_kernel), "--ramdisk", str(empty_ramdisk),
                    "--header_version", "1", "--pagesize", str(PAGE), "--board", "beyond2lte",
                    "--output", str(image)], check=True)
    boot = image.read_bytes()
    expected_trailer = PAGE + align(len(shim) + len(fd) + len(dtb))
    if len(boot) != expected_trailer:
        p.error("unexpected Android BOOT component padding")
    wifi_mac = read_wifi_mac(a.wifi_mac_file) if a.wifi_mac_file else None
    header = make_payload_header(len(kernel), wifi_mac)
    final_size = len(boot) + len(header) + len(kernel)
    limit = min(a.max_image_bytes, a.boot_partition_bytes or a.max_image_bytes)
    if final_size > limit:
        image.unlink()
        p.error(f"BOOT image needs {final_size} bytes, above explicit ceiling {limit}")
    with image.open("ab") as out:
        out.write(header)
        out.write(kernel)
    layout = check_boot(image)
    manifest = {
        "format": "beyond2lte-xnu-boot-v2", "device": "beyond2lte", "model": "SM-G975F",
        "status": "offline-built; not device-boot-verified", "layout": layout,
        "firmware_base": hex(FD_BASE), "firmware_bytes": FD_SIZE,
        "confirmed_target_boot_partition_bytes": a.boot_partition_bytes,
        "offline_size_ceiling_bytes": a.max_image_bytes,
        "boot_sha256": sha256(image), "kernel_sha256": sha256(a.kernel),
        "firmware_sha256": sha256(a.firmware), "loader_sha256": sha256(a.loader),
        "embedded_loader_verified": True,
        "storage_writes_performed": False,
        "boot_path": "Samsung S-Boot -> BootShim -> external Mu UEFI -> embedded XnuLoader -> BOOT trailer Mach-O",
        "notes": ["Android ramdisk and second fields are empty; S-Boot does not load the XNU trailer.",
                  "Firmware UFS/BlockIo reads the trailer from the BOOT partition.",
                  "The host seed changes per packaged image, not per reboot; firmware RNG is preferred.",
                  "Verify actual device identity and BOOT capacity, and obtain approval before writing."]}
    (a.output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    archive = a.output_dir / "beyond2lte-xnu-boot.tar"
    with tarfile.open(archive, "w", format=tarfile.USTAR_FORMAT) as tar:
        for name in ("boot.img", "manifest.json"):
            tar.add(a.output_dir / name, arcname=name)
    print(json.dumps(manifest, indent=2))
    print(f"Created {archive}; no device write was performed")


if __name__ == "__main__":
    main()
