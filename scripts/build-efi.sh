#!/usr/bin/env bash
# Build standalone applications; the firmware platform is an external dependency.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${WORKSPACE:?Set WORKSPACE to the prepared Mu/EDK II workspace}"
export WORKSPACE
export EDK_TOOLS_PATH="${EDK_TOOLS_PATH:-$WORKSPACE/Mu_Basecore/BaseTools}"
export EDK_TOOLS_BIN="${EDK_TOOLS_BIN:-$EDK_TOOLS_PATH/Source/C/bin}"
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
: "${CLANG_BIN:?Set CLANG_BIN to a directory containing clang, llvm-lib, llvm-rc and lld-link}"
export CLANG_BIN="${CLANG_BIN%/}/"
export PACKAGES_PATH="$WORKSPACE/Mu_Basecore${PACKAGES_PATH:+:$PACKAGES_PATH}"
export PATH="$EDK_TOOLS_PATH/BinWrappers/PosixLike:$EDK_TOOLS_BIN:$CLANG_BIN:$PATH"
for tool in clang llvm-lib llvm-rc lld-link; do
  [[ -x "$CLANG_BIN$tool" ]] || { echo "Missing tool: $CLANG_BIN$tool" >&2; exit 1; }
done
[[ -x "$EDK_TOOLS_BIN/GenFw" ]] || { echo 'Build native BaseTools first.' >&2; exit 1; }
mkdir -p "$WORKSPACE/B2LXnuPkg"
rsync -a "$repo_root/uefi/" "$WORKSPACE/B2LXnuPkg/"
rsync -a "$repo_root/common/cache/" "$WORKSPACE/B2LXnuPkg/XnuLoader/"
rsync -a "$repo_root/common/smp/" "$WORKSPACE/B2LXnuPkg/XnuLoader/"
cd "$WORKSPACE"
exec "$EDK_TOOLS_PATH/BinWrappers/PosixLike/build" \
  -a AARCH64 -t CLANGPDB -b RELEASE -n "${BUILD_JOBS:-4}" \
  -p "${DSC_PATH:-B2LXnuPkg/B2LXnuPkg.dsc}" "$@"
