#!/usr/bin/env bash
# Apply the board patch only to the pinned upstream revision. Never reset a checkout.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
xnu_path="${1:-${XNU_PATH:-$repo_root/xnu}}"
expected=ac9718fb1af618d5ce8678d0dc6e8a58f252216f
[[ "$(git -C "$xnu_path" rev-parse HEAD)" == "$expected" ]] || {
  echo "XNU must be at $expected" >&2; exit 1;
}
python3 "$repo_root/scripts/apply-board-patches.py" "$xnu_path" \
  "$repo_root/patches/0001-b2l-machine-and-local-dependencies.patch" \
  "$repo_root/patches/0002-b2l-builtin-hfs-root.patch" \
  "$repo_root/patches/0003-b2l-hfs-md5.patch" \
  "$repo_root/patches/0004-b2l-unavailable-amfi-entitlements.patch" \
  "$repo_root/patches/0005-b2l-userdata-root.patch" \
  "$repo_root/patches/0006-b2l-arm64e-compat.patch" \
  "$repo_root/patches/0007-b2l-disable-signature-enforcement.patch" \
  "$repo_root/patches/0008-b2l-userspace-progress-trace.patch" \
  "$repo_root/patches/0009-b2l-first-compat-trap-stages.patch" \
  "$repo_root/patches/0010-b2l-scalar-instruction-fetch.patch" \
  "$repo_root/patches/0011-b2l-native-pan-copyio.patch" \
  "$repo_root/patches/0012-b2l-unsigned-shared-cache.patch" \
  "$repo_root/patches/0013-b2l-unsigned-executable-boothash.patch" \
  "$repo_root/patches/0014-b2l-arm64e-dyld-page-fixups.patch" \
  "$repo_root/patches/0015-b2l-sandbox-container-query.patch" \
  "$repo_root/patches/0016-b2l-registered-sandbox-policy.patch" \
  "$repo_root/patches/0017-b2l-development-amfi-metadata.patch" \
  "$repo_root/patches/0018-b2l-prelockdown-policy-registration.patch" \
  "$repo_root/patches/0019-b2l-unsigned-code-identity.patch" \
  "$repo_root/patches/0020-b2l-volatile-development-keystore.patch" \
  "$repo_root/patches/0021-b2l-persistent-system-log.patch" \
  "$repo_root/patches/0022-b2l-two-core-startup.patch" \
  "$repo_root/patches/0023-b2l-automatic-native-cache.patch" \
  "$repo_root/patches/0024-b2l-bounded-crash-backtrace.patch" \
  "$repo_root/patches/0025-b2l-platform-status-and-watchdog.patch" \
  "$repo_root/patches/0026-b2l-persona-manifest-bootstrap.patch" \
  "$repo_root/patches/0027-b2l-der-entitlement-query.patch" \
  "$repo_root/patches/0028-b2l-smp-startup-diagnostics.patch" \
  "$repo_root/patches/0029-b2l-ordered-cpu-registration.patch" \
  "$repo_root/patches/0030-b2l-cpu-startup-context.patch" \
  "$repo_root/patches/0031-b2l-first-secondary-panic.patch" \
  "$repo_root/patches/0032-b2l-reject-secondary-affinity-alias.patch"
# New files have separate provenance and are kept as an overlay.
while IFS= read -r -d '' source_file; do
  relative="${source_file#"$repo_root/xnu-overlay/"}"
  destination="$xnu_path/$relative"
  if [[ -f "$destination" ]] && ! cmp -s "$source_file" "$destination"; then
    echo "Refusing to replace a different local overlay file: $destination" >&2
    exit 1
  fi
done < <(find "$repo_root/xnu-overlay" -type f -print0)
rsync -a "$repo_root/xnu-overlay/" "$xnu_path/"
for source_file in "$repo_root/common/cache/"*; do
  destination="$xnu_path/bsd/kern/$(basename "$source_file")"
  if [[ -f "$destination" ]] && ! cmp -s "$source_file" "$destination"; then
    echo "Refusing different generated cache source: $destination" >&2; exit 1
  fi
  cp "$source_file" "$destination"
done
echo "Prepared B2L source: $xnu_path"

for source_file in "$repo_root/common/smp/"*; do
  destination="$xnu_path/iokit/Kernel/arm/$(basename "$source_file")"
  if [[ -f "$destination" ]] && ! cmp -s "$source_file" "$destination"; then
    echo "Refusing different generated SMP header: $destination" >&2; exit 1
  fi
  cp "$source_file" "$destination"
done
