#!/usr/bin/env python3
"""Test deterministic firmware launch preparation without a phone or full Mu tree."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


PREPARE = Path(__file__).resolve().parents[1] / "scripts/prepare-boot-firmware.py"
GUID = "92742D9E-1A4F-42F8-9DC9-53895297B2F4"
MANAGER = "Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/BootManager.c"
INF = "Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/BootManagerLib.inf"
BINDINGS = "Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc"


class PreparationTests(unittest.TestCase):
    def fixture(self, root):
        (root / "Mu_Basecore").mkdir()
        files = {
            "Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc": "[PcdsFixedAtBuild]\n  gPcBdsPkgTokenSpaceGuid.PcdShellFile|{ 0 }\n",
            "Platforms/Samsung/beyond2ltePkg/beyond2lte.fdf": "[FV.FvMain]\n[FV.FVMAIN_COMPACT]\n",
            BINDINGS: "PlatformBootManagerLib|SiliciumPkg/Library/BootManagerLib/BootManagerLib.inf\n",
            MANAGER: """VOID PlatformBootManagerBeforeConsole () {
  MsBootOptionsLibRegisterDefaultBootOptions ();
}
VOID PlatformBootManagerAfterConsole () {
  OriginalAfterConsole ();
}
""",
            INF: "[LibraryClasses]\n  PcdLib\n[Pcd]\n  gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut\n",
            "Loader.efi": "MZ-test-loader",
        }
        for name, text in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)

    def prepare(self, root):
        return subprocess.run([sys.executable, str(PREPARE), "--mu", str(root),
                               "--loader", str(root / "Loader.efi"), "--loader-guid", GUID],
                              capture_output=True, text=True)

    def test_deterministic_launch_and_idempotence(self):
        with tempfile.TemporaryDirectory(prefix="b2l-firmware-test-") as directory:
            root = Path(directory)
            self.fixture(root)
            result = self.prepare(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            first = (root / MANAGER).read_text()
            self.assertNotIn("MsBootOptionsLibRegisterDefaultBootOptions ();", first)
            self.assertIn("GetSectionFromAnyFv (&LoaderGuid, EFI_SECTION_PE32", first)
            self.assertLess(first.index("EfiBootManagerConnectAll ();"), first.index("gBS->LoadImage"))
            self.assertLess(first.index("gBS->StartImage"), first.index("CpuDeadLoop ();"))
            self.assertNotIn("SetVariable", first)
            self.assertNotIn("EfiBootManagerBoot (", first)
            self.assertEqual(self.prepare(root).returncode, 0)
            self.assertEqual(first, (root / MANAGER).read_text())
            self.assertEqual((root / INF).read_text().count("  DxeServicesLib\n"), 1)

    def test_changed_actual_library_binding_rejected(self):
        with tempfile.TemporaryDirectory(prefix="b2l-firmware-test-") as directory:
            root = Path(directory)
            self.fixture(root)
            before = (root / MANAGER).read_text()
            (root / BINDINGS).write_text("PlatformBootManagerLib|Other/Library.inf\n")
            result = self.prepare(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("binding changed", result.stderr)
            self.assertEqual(before, (root / MANAGER).read_text())

    def test_missing_pcd_rejected_without_source_mutation(self):
        with tempfile.TemporaryDirectory(prefix="b2l-firmware-test-") as directory:
            root = Path(directory)
            self.fixture(root)
            before = (root / MANAGER).read_text()
            (root / "Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc").write_text("[Components]\n")
            result = self.prepare(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("PcdShellFile", result.stderr)
            self.assertEqual(before, (root / MANAGER).read_text())


if __name__ == "__main__":
    unittest.main()
