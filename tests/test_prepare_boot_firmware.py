#!/usr/bin/env python3
"""Test deterministic firmware launch preparation without a phone or full Mu tree."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


PREPARE = Path(__file__).resolve().parents[1] / "scripts/prepare-boot-firmware.py"
GUID = "92742D9E-1A4F-42F8-9DC9-53895297B2F4"
MANAGER = "Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/BootManager.c"
INF = "Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/BootManagerLib.inf"
BINDINGS = "Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc"
GIC = "Mu_Basecore/ArmPkg/Drivers/ArmGicDxe/GicV2/ArmGicV2Dxe.c"


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
            GIC: "VOID ArmGicV2DisableInterruptInterface (UINTN GicInterruptInterfaceBase) { OriginalDisable (); }\n"
                 "VOID GicV2ExitBootServicesEvent (EFI_EVENT Event, VOID *Context) { OriginalDrain (); }\n",
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

    def test_gic_shutdown_preserves_raw_token_and_is_bounded(self):
        with tempfile.TemporaryDirectory(prefix="b2l-firmware-test-") as directory:
            root = Path(directory)
            self.fixture(root)
            result = self.prepare(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            text = (root / GIC).read_text()
            self.assertIn("Index < 4096", text)
            self.assertIn("IntId = RawIar & 0x3FFU;", text)
            self.assertIn("IntId >= 1020", text)
            self.assertIn("ARM_GIC_ICCEIOR, RawIar", text)
            self.assertIn("CpuControl &= ~(1U << 9)", text)
            self.assertNotIn("mGicInterruptInterfaceBase + 0x1000", text)
            self.assertIn("GicInterruptInterfaceBase + ARM_GIC_ICCICR, ~1U", text)
            self.assertIn("ArmGicV2DisableInterruptInterface (mGicInterruptInterfaceBase)", text)
            self.assertNotIn("GicV2EndOfInterrupt (", text)
            self.assertEqual(self.prepare(root).returncode, 0)
            self.assertEqual(text, (root / GIC).read_text())

    @unittest.skipUnless(shutil.which("cc"), "host C compiler is required")
    def test_generated_gic_callback_with_mock_registers(self):
        with tempfile.TemporaryDirectory(prefix="b2l-gic-mmio-test-") as directory:
            root = Path(directory)
            self.fixture(root)
            result = self.prepare(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            harness = r"""
#include <assert.h>
#include <stdint.h>
typedef void VOID;
typedef void *EFI_EVENT;
typedef uintptr_t UINTN;
typedef uint32_t UINT32;
typedef int BOOLEAN;
#define ARM_GIC_ICCICR 0
#define ARM_GIC_ICCPMR 4
#define ARM_GIC_ICCEIOR 16
#define ARM_GIC_ICDICER 0x180
#define ARM_GIC_ICDDCR 0
static UINTN mGicInterruptInterfaceBase = 0x1000, mGicDistributorBase = 0x3000;
static UINTN mGicNumInterrupts = 160;
static int gHardwareInterruptV2Protocol;
static UINT32 control, distributor, raw, eoi, dir, last_eoi, reads;
static UINT32 MmioRead32(UINTN address) {
  assert(address == 0x1000); return control;
}
static void MmioWrite32(UINTN address, UINT32 value) {
  if (address == 0x1010) { eoi++; last_eoi = value; }
  else if (address == 0x2000) { assert(0 && "GICC_DIR is outside the mapped CPU interface"); }
  else if (address == 0x1000) control = value;
  else assert(address == 0x1004 || address == 0x3180);
}
static void MmioAnd32(UINTN address, UINT32 value) {
  if (address == 0x1000) control &= value;
  else { assert(address == 0x3000); distributor &= value; }
}
static void GicV2DisableInterruptSource(void *protocol, UINTN source) {
  assert(protocol == &gHardwareInterruptV2Protocol && source < mGicNumInterrupts);
}
static UINTN ArmGicV2AcknowledgeInterrupt(UINTN base) {
  assert(base == 0x1000); reads++; return raw;
}
static void ArmDataSynchronizationBarrier(void) {}
""" + (root / GIC).read_text() + r"""
int main(void) {
  // Inherited split EOI is cleared; stuck raw SGI uses only the mapped EOIR.
  control = 0x2ab; distributor = 0x43; raw = 0xc01;
  GicV2ExitBootServicesEvent(0, 0);
  assert(reads == 4096 && eoi == 4096 && dir == 0);
  assert(last_eoi == raw);
  assert(control == 0xaa && distributor == 0x42);
  // Combined EOI must never write DIR, even when the acknowledge source repeats.
  control = 0x801; distributor = 0x43; raw = 27;
  reads = eoi = dir = 0;
  GicV2ExitBootServicesEvent(0, 0);
  assert(reads == 4096 && eoi == 4096 && dir == 0 && control == 0x800);
  // A special INTID terminates immediately even with unrelated high raw bits.
  control = 1; raw = 0xffff03ff; reads = eoi = dir = 0;
  GicV2ExitBootServicesEvent(0, 0);
  assert(reads == 1 && eoi == 0 && dir == 0);
  return 0;
}
"""
            source = root / "gic_mock_test.c"
            binary = root / "gic_mock_test"
            source.write_text(harness)
            subprocess.run([shutil.which("cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True)

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
