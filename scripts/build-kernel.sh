#!/usr/bin/env bash
# Build the standalone kernel target. Apple kext KPI bundles are not a B2L target.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
xnu_path="${XNU_PATH:-$repo_root/xnu}"
build_root="${BUILD_DIR:-$repo_root/out/xnu}"
: "${KDKROOT:?Set KDKROOT to the merged KDK and KDK_SDK payload root}"
: "${B2L_DEPSROOT:?Set B2L_DEPSROOT to the isolated dependency installation root}"
export B2L_DEPSROOT
[[ "$(git -C "$xnu_path" rev-parse HEAD)" == ac9718fb1af618d5ce8678d0dc6e8a58f252216f ]] || {
  echo 'Unexpected XNU source revision.' >&2; exit 1;
}
[[ -f "$xnu_path/pexpert/pexpert/arm64/B2L.h" ]] || {
  echo 'Apply the B2L patch and overlay before building.' >&2; exit 1;
}
python3 "$repo_root/scripts/stage-libpthread.py" "$xnu_path"
python3 "$repo_root/scripts/stage-bearssl.py" "$xnu_path"
cd "$xnu_path"
exec make SDKROOT=macosx KDKROOT="$KDKROOT" \
  TARGET_CONFIGS='DEVELOPMENT ARM64 B2L' \
  PLATFORM=iPhoneOS PLATFORM_FOR_CONFIG=iPhoneOS SDKVERSION=26.5 BTI_BUILD=0 \
  RC_DARWIN_KERNEL_VERSION=25.5.0 BUILD_LTO=0 BUILD_WERROR=0 \
  MEMORY_SIZE="${BUILD_MEMORY_BYTES:-17179869184}" \
  SYSCTL_HW_PHYSICALCPU="${BUILD_JOBS:-4}" SYSCTL_HW_LOGICALCPU="${BUILD_JOBS:-4}" \
  KERNEL_BUILDS_IN_PARALLEL=1 MAKEJOBS="-j${BUILD_JOBS:-4}" \
  OBJROOT="$build_root/obj" SYMROOT="$build_root/sym" DSTROOT="$build_root/dst" \
  build "$@"
