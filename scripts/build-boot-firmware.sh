#!/usr/bin/env bash
# Rebuild an external, disposable beyond2lte firmware tree with the XNU app.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${WORKSPACE:?Set WORKSPACE to the disposable prepared Mu-Silicium tree}"
"$repo_root/scripts/build-efi.sh"
loader="$WORKSPACE/Build/B2LXnu/RELEASE_CLANGPDB/AARCH64/XnuLoader.efi"
"${PYTHON_COMMAND:-python3}" "$repo_root/scripts/prepare-boot-firmware.py" \
  --mu "$WORKSPACE" --loader "$loader" \
  --loader-guid 92742D9E-1A4F-42F8-9DC9-53895297B2F4
export PACKAGES_PATH="$WORKSPACE/Platforms/Samsung:$WORKSPACE/Common/Mu:$WORKSPACE/Common/Mu_OEM_Sample:$WORKSPACE/Silicon/Samsung:$WORKSPACE/Silicon/Silicium${PACKAGES_PATH:+:$PACKAGES_PATH}"
# DSC build-time ACPI declarations remain external. Their firmware-volume
# inclusion is disabled by the XNU profile; XNU uses an Apple device tree.
export PACKAGES_PATH="$PACKAGES_PATH:$WORKSPACE/Silicium-ACPI:$WORKSPACE/Silicium-ACPI/Platforms/Samsung:$WORKSPACE/Silicium-ACPI/SoCs/Samsung"
export DSC_PATH=beyond2ltePkg/beyond2lte.dsc
exec "$repo_root/scripts/build-efi.sh" \
  -D FD_BASE=0xC9000000 -D FD_SIZE=0x400000 -D FD_BLOCKS=0x400 \
  -D ENABLE_SECUREBOOT=0 -D DEVICE_MODEL=0 "$@"
