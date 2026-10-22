#!/usr/bin/env python3
"""Add the built XNU loader to a disposable external Mu firmware work tree.

The reference UEFI remains an external dependency. No ACPI/Windows sources are
copied into this repository. This script does not access a phone or flash disk.
"""
import argparse
from pathlib import Path
import re
import shutil
import uuid


def replace_function(source, name, body):
    start = source.index(name + " (")
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[:brace] + "{\n" + body + "\n}" + source[end:]


def xnu_memory_profile(source):
    """Relax only the Windows budget guard, preserving the real EL2 top reserve."""
    old = '  {"Upper bank guard", 0x980000000ULL, 0x080000000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},'
    new = '  {"B2L EL2 early memory", 0x9FD800000ULL, 0x02800000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},'
    if source.count(old) == 1 and new not in source:
        return source.replace(old, '// B2L XNU RAM profile: live 8GiB board DT reserves the final40MiB.\n'
                             '// RamManagerDxe discovers only actual DDR and excludes this reservation.\n' + new)
    if source.count(new) == 1 and old not in source:
        return source
    raise ValueError("expected original last-bank guard or exact XNU EL2 reservation is missing")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mu", type=Path, required=True, help="Disposable Mu-Silicium build tree")
    p.add_argument("--loader", type=Path, required=True)
    p.add_argument("--loader-guid", required=True)
    a = p.parse_args()
    root = a.mu.resolve()
    guid = uuid.UUID(a.loader_guid)
    # Protect the user's named reference checkout from accidental build edits.
    if root.name == "UEFI_beyond2lte" or not (root / "Mu_Basecore").is_dir():
        p.error("--mu must be a disposable Mu-Silicium build tree, not the reference repository")
    platform = root / "Platforms/Samsung/beyond2ltePkg"
    dsc_path = platform / "beyond2lte.dsc"
    fdf_path = platform / "beyond2lte.fdf"
    library_root = root / "Silicon/Silicium/SiliciumPkg"
    bindings_path = library_root / "SiliciumPkg.dsc.inc"
    manager_path = library_root / "Library/BootManagerLib/BootManager.c"
    manager_inf_path = library_root / "Library/BootManagerLib/BootManagerLib.inf"
    gic_path = root / "Mu_Basecore/ArmPkg/Drivers/ArmGicDxe/GicV2/ArmGicV2Dxe.c"
    memory_path = platform / "Library/MemoryMapLib/MemoryMapLib.c"
    for path in (memory_path, dsc_path, fdf_path, bindings_path, manager_path, manager_inf_path, gic_path, a.loader):
        if not path.is_file():
            p.error(f"missing input: {path}")
    if re.search(r"MemoryMapLib\s*\|\s*beyond2ltePkg/Library/MemoryMapLib/MemoryMapLib\.inf", dsc_path.read_text()) is None:
        p.error("external MemoryMapLib binding changed")
    try:
        memory = xnu_memory_profile(memory_path.read_text())
    except ValueError as error:
        p.error(str(error))
    bindings = bindings_path.read_text()
    if re.search(r"PlatformBootManagerLib\s*\|\s*SiliciumPkg/Library/BootManagerLib/BootManagerLib\.inf", bindings) is None:
        p.error("external firmware PlatformBootManagerLib binding changed; refusing an ineffective boot policy patch")
    manager = manager_path.read_text()
    before = "  MsBootOptionsLibRegisterDefaultBootOptions ();"
    before_disabled = "  // B2L XNU: do not register or rewrite persistent boot options."
    if before not in manager and before_disabled not in manager:
        p.error("expected PlatformBootManagerBeforeConsole registration call is missing")
    manager = manager.replace(before, before_disabled)
    for include in ("<PiDxe.h>", "<Library/BaseLib.h>", "<Library/DxeServicesLib.h>", "<Library/UefiLib.h>"):
        if f"#include {include}" not in manager:
            manager = f"#include {include}\n" + manager
    fields = guid.fields
    guid_initializer = "{ 0x%08X, 0x%04X, 0x%04X, { %s } }" % (
        fields[0], fields[1], fields[2], ", ".join(f"0x{b:02X}" for b in guid.bytes[8:]))
    try:
        manager = replace_function(manager, "PlatformBootManagerAfterConsole", f"""  // B2L XNU: execute before BDS consumes cached BootNext or existing BootOrder.
  // No boot option variables are created, removed or reordered by this path.
  EFI_STATUS Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *GopProtocol = NULL;
  EFI_GUID LoaderGuid = {guid_initializer};
  EFI_HANDLE LoaderImage = NULL;
  VOID *LoaderBuffer = NULL;
  UINTN LoaderSize = 0;

  PostGopSetup (&GopProtocol);
  EfiBootManagerConnectAll ();
  Print (L"B2L: launching embedded XNU loader before BootNext/BootOrder\\r\\n");
  Status = GetSectionFromAnyFv (&LoaderGuid, EFI_SECTION_PE32, 0, &LoaderBuffer, &LoaderSize);
  if (!EFI_ERROR (Status)) {{
    Status = gBS->LoadImage (FALSE, gImageHandle, NULL, LoaderBuffer, LoaderSize, &LoaderImage);
    FreePool (LoaderBuffer);
    if (!EFI_ERROR (Status)) {{
      Status = gBS->StartImage (LoaderImage, NULL, NULL);
    }}
  }}
  // Returning would expose old Windows boot entries. Keep the failure visible.
  Print (L"B2L: XNU loader returned %r; automatic fallback stopped\\r\\n", Status);
  CpuDeadLoop ();""")
    except (ValueError, IndexError):
        p.error("expected PlatformBootManagerAfterConsole implementation is missing or malformed")
    manager_inf = manager_inf_path.read_text()
    for library in ("BaseLib", "DxeServicesLib", "UefiLib", "UefiBootManagerLib"):
        if re.search(rf"^\s*{library}\s*$", manager_inf, re.MULTILINE) is None:
            manager_inf = manager_inf.replace("[LibraryClasses]", f"[LibraryClasses]\n  {library}", 1)
    if "[LibraryClasses]" not in manager_inf:
        p.error("external BootManagerLib INF has no library class section")
    try:
        gic = replace_function(gic_path.read_text(), "ArmGicV2DisableInterruptInterface", """  // B2L XNU profile: preserve unrelated CPU-interface control bits.
  MmioAnd32 (GicInterruptInterfaceBase + ARM_GIC_ICCICR, ~1U);
  MmioWrite32 (GicInterruptInterfaceBase + ARM_GIC_ICCPMR, 0);""")
        gic = replace_function(gic, "GicV2ExitBootServicesEvent", """  // B2L XNU profile: bound shutdown even if a source cannot be quiesced.
  // CoreExitBootServices already stopped the firmware timer. Mask both
  // non-secure architected timer PPIs before draining the CPU interface.
  UINTN Index;
  UINT32 RawIar;
  UINT32 IntId;
  UINT32 CpuControl;
  (VOID)Event;
  (VOID)Context;

  CpuControl = MmioRead32 (mGicInterruptInterfaceBase + ARM_GIC_ICCICR);
  // B2L maps only the first 4 KiB CPU-interface window. GICC_DIR at +0x1000
  // is outside that mapping. Select combined non-secure EOI before draining,
  // matching the board's XNU GIC setup, and preserve every unrelated bit.
  CpuControl &= ~(1U << 9); // Clear EOImodeNS in the non-secure GICC_CTLR view.
  MmioWrite32 (mGicInterruptInterfaceBase + ARM_GIC_ICCICR, CpuControl);
  ArmDataSynchronizationBarrier ();
  MmioWrite32 (mGicDistributorBase + ARM_GIC_ICDICER, (1U << 27) | (1U << 30));
  for (Index = 0; Index < mGicNumInterrupts; Index++) {
    GicV2DisableInterruptSource (&gHardwareInterruptV2Protocol, Index);
  }
  ArmDataSynchronizationBarrier ();

  for (Index = 0; Index < 4096; Index++) {
    RawIar = (UINT32)ArmGicV2AcknowledgeInterrupt (mGicInterruptInterfaceBase);
    IntId = RawIar & 0x3FFU;
    if (IntId >= 1020 || IntId >= mGicNumInterrupts) {
      break;
    }
    // The protocol wrapper checks a bare INTID and rejects SGI CPU source
    // bits. Hardware EOIR must instead receive the complete original token.
    MmioWrite32 (mGicInterruptInterfaceBase + ARM_GIC_ICCEIOR, RawIar);
  }
  ArmDataSynchronizationBarrier ();
  // The board-profile helper clears Enable without changing other bits.
  ArmGicV2DisableInterruptInterface (mGicInterruptInterfaceBase);
  MmioAnd32 (mGicDistributorBase + ARM_GIC_ICDDCR, ~1U);
  ArmDataSynchronizationBarrier ();""")
    except (ValueError, IndexError):
        p.error("expected GicV2ExitBootServicesEvent implementation is missing or malformed")
    binary_inf = f"""# Generated external binary module; do not commit the binary.
[Defines]
  INF_VERSION = 0x0001001B
  BASE_NAME = B2LXnuEmbedded
  FILE_GUID = {guid}
  MODULE_TYPE = UEFI_APPLICATION
  VERSION_STRING = 0.1
[Binaries.AARCH64]
  PE32|XnuLoader.efi|*
"""
    dsc = dsc_path.read_text()
    pcd = "gPcBdsPkgTokenSpaceGuid.PcdShellFile|{ " + ", ".join(f"0x{b:02X}" for b in guid.bytes_le) + " }"
    dsc, n = re.subn(r"gPcBdsPkgTokenSpaceGuid\.PcdShellFile\|[^\r\n]*", pcd, dsc)
    if n != 1:
        p.error(f"expected exactly one PcdShellFile, found {n}")
    component = "beyond2ltePkg/B2LXnuEmbedded/B2LXnuEmbedded.inf"
    if component not in dsc:
        dsc += f"\n[Components]\n  {component}\n"
    fdf = fdf_path.read_text()
    if component not in fdf:
        if fdf.count("[FV.FVMAIN_COMPACT]") != 1:
            p.error("expected one compact FV declaration")
        fdf = fdf.replace("[FV.FVMAIN_COMPACT]", f"  INF {component}\n\n[FV.FVMAIN_COMPACT]")
    # XNU first boot consumes Apple DeviceTree built by its EFI loader.
    # ACPI remains outside this project and is not loaded by this firmware profile.
    for line in ("  INF MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf",
                 "  INF MdeModulePkg/Universal/Acpi/AcpiPlatformDxe/AcpiPlatformDxe.inf",
                 "  !include Include/ACPI.inc"):
        fdf = fdf.replace(line, "  # B2L XNU profile omits ACPI")
    output = platform / "B2LXnuEmbedded"
    output.mkdir(exist_ok=True)
    shutil.copyfile(a.loader, output / "XnuLoader.efi")
    (output / "B2LXnuEmbedded.inf").write_text(binary_inf)
    dsc_path.write_text(dsc)
    fdf_path.write_text(fdf)
    manager_path.write_text(manager)
    manager_inf_path.write_text(manager_inf)
    gic_path.write_text(gic)
    memory_path.write_text(memory)
    print(f"Prepared external 4 MiB firmware profile at {root}")
    print(f"Embedded loader {guid}; post-console launch precedes BootNext/BootOrder and stops if the loader returns")


if __name__ == "__main__":
    main()
