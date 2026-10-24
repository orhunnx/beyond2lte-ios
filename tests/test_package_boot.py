#!/usr/bin/env python3
"""Regression coverage for nested GenFv section streams in the real FD check."""
import importlib.util
import lzma
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
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


class BootTrailerTests(unittest.TestCase):
    def kernel(self):
        image = bytearray(4096)
        struct.pack_into("<III", image, 0, 0xfeedfacf, 0x0100000c, 0)
        return bytes(image)

    def android_prefix(self):
        prefix = bytearray(pack.PAGE + pack.align(4096))
        prefix[:8] = b"ANDROID!"
        struct.pack_into("<I", prefix, 8, 4096)
        struct.pack_into("<II", prefix, 36, pack.PAGE, 1)
        struct.pack_into("<I", prefix, 1644, 1648)
        return prefix

    def verify(self, data):
        with tempfile.NamedTemporaryFile() as image:
            image.write(data)
            image.flush()
            return pack.check_boot(Path(image.name))

    def test_seed_uses_all_256_host_bytes_once(self):
        seed = bytes(range(256))
        with patch.object(pack.os, "urandom", return_value=seed) as random:
            header = pack.make_payload_header(4096)
        random.assert_called_once_with(256)
        self.assertEqual(len(header), 320)
        self.assertEqual(header[32:288], seed)
        self.assertEqual(header[288:320], bytes(32))
        result = self.verify(self.android_prefix() + header + self.kernel())
        self.assertEqual(result["payload_version"], 2)
        self.assertEqual(result["seed_bytes"], 256)

    def test_private_wifi_metadata_is_validated_and_not_disclosed(self):
        mac = bytes([2, 0, 0, 0, 0, 1])  # synthetic local fixture
        header = pack.make_payload_header(4096, mac)
        self.assertEqual(header[288:302], b"B2LWIFI1" + mac)
        result = self.verify(self.android_prefix() + header + self.kernel())
        self.assertTrue(result["wifi_mac_present"])
        self.assertNotIn("02:00:00:00:00:01", str(result))
        for offset in (288, 302, 319):
            bad = bytearray(header)
            bad[offset] ^= 1
            with self.assertRaises(ValueError):
                self.verify(self.android_prefix() + bad + self.kernel())
        for invalid in (bytes(6), bytes([1,0,0,0,0,1]), bytes(5), bytes(7)):
            with self.assertRaises(ValueError):
                pack.make_payload_header(4096, invalid)
        with tempfile.TemporaryDirectory() as directory:
            private = Path(directory) / "mac.txt"
            private.write_text("02:00:00:00:00:01\n")
            self.assertEqual(pack.read_wifi_mac(private), mac)
            private.write_text("untrusted-file-content")
            with self.assertRaisesRegex(ValueError, "exactly six") as error:
                pack.read_wifi_mac(private)
            self.assertNotIn("untrusted-file-content", str(error.exception))

    def test_legacy_64_byte_seed_requires_repackaging(self):
        header = struct.pack("<8sIIQQ", b"B2LXNU01", 1, 128, 4096, 128) + bytes(range(64)) + bytes(32)
        with self.assertRaisesRegex(ValueError, "legacy.*64.*256"):
            self.verify(self.android_prefix() + header + self.kernel())

    def test_truncated_v2_seed_rejected(self):
        with self.assertRaisesRegex(ValueError, "truncated"):
            self.verify(self.android_prefix() + pack.make_payload_header(4096)[:200])

    def test_wrong_v2_offset_rejected(self):
        header = bytearray(pack.make_payload_header(4096))
        struct.pack_into("<Q", header, 24, 128)
        with self.assertRaisesRegex(ValueError, "invalid BOOT trailer header"):
            self.verify(self.android_prefix() + header + self.kernel())

    def test_declared_kernel_length_overflow_rejected(self):
        header = bytearray(pack.make_payload_header(4096))
        struct.pack_into("<Q", header, 16, 2**64 - 1)
        with self.assertRaisesRegex(ValueError, "invalid BOOT trailer size"):
            self.verify(self.android_prefix() + header + self.kernel())


if __name__ == "__main__":
    unittest.main()
