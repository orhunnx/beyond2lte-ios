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
    for path in (dsc_path, fdf_path, bindings_path, manager_path, manager_inf_path, a.loader):
        if not path.is_file():
            p.error(f"missing input: {path}")
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
    print(f"Prepared external 4 MiB firmware profile at {root}")
    print(f"Embedded loader {guid}; post-console launch precedes BootNext/BootOrder and stops if the loader returns")


if __name__ == "__main__":
    main()
