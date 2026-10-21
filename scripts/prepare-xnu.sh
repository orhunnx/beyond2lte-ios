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
  "$repo_root/patches/0006-b2l-arm64e-compat.patch"
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
echo "Prepared B2L source: $xnu_path"
