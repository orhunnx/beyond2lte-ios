#!/usr/bin/env python3
"""Regression coverage for nested GenFv section streams in the real FD check."""
import importlib.util
import lzma
from pathlib import Path
import struct
import unittest
import uuid

spec = importlib.util.spec_from_file_location("package_boot", Path(__file__).resolve().parents[1] / "scripts/package-boot.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


def section(kind, payload):
    length = 4 + len(payload)
    return length.to_bytes(3, "little") + bytes([kind]) + payload


def volume(loader):
    data = bytearray(256)
    struct.pack_into("<Q", data, 32, len(data))
    data[40:44] = b"_FVH"
    struct.pack_into("<H", data, 48, 56)
    data[128:128 + len(loader)] = loader
    return bytes(data)


def guided_lzma(stream):
    payload = lzma.compress(stream, format=lzma.FORMAT_ALONE)
    header = uuid.UUID("EE4E5898-3914-4259-9D6E-DC7BD79403CF").bytes_le + struct.pack("<HH", 24, 1)
    return section(2, header + payload)


class FirmwarePayloadTests(unittest.TestCase):
    loader = b"MZ-B2L-XNU-LOADER-REGRESSION"

    def test_real_genfv_shape_raw_then_fv_image(self):
        # Actual built stream: 04 00 00 19 (empty RAW), then FV_IMAGE header.
        stream = section(0x19, b"") + section(0x17, volume(self.loader))
        self.assertEqual(stream[:4], b"\x04\x00\x00\x19")
        self.assertTrue(pack.firmware_contains_loader(guided_lzma(stream), self.loader))

    def test_direct_fv_supported(self):
        self.assertTrue(pack.firmware_contains_loader(guided_lzma(volume(self.loader)), self.loader))

    def test_loader_outside_fv_rejected(self):
        stream = section(0x17, volume(b"")) + self.loader
        self.assertFalse(pack.firmware_contains_loader(guided_lzma(stream), self.loader))

    def test_truncated_fv_rejected(self):
        stream = section(0x17, volume(self.loader)[:-1])
        self.assertFalse(pack.firmware_contains_loader(guided_lzma(stream), self.loader))

    def test_truncated_section_rejected(self):
        stream = section(0x19, b"") + section(0x17, volume(self.loader))[:-1]
        self.assertFalse(pack.firmware_contains_loader(guided_lzma(stream), self.loader))

    def test_missing_loader_rejected(self):
        self.assertFalse(pack.firmware_contains_loader(guided_lzma(section(0x17, volume(b""))), self.loader))


if __name__ == "__main__":
    unittest.main()
