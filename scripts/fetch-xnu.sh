#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
xnu_path="${1:-$repo_root/xnu}"
xnu_sha=ac9718fb1af618d5ce8678d0dc6e8a58f252216f

if [[ ! -d "$xnu_path/.git" ]]; then
  git clone --filter=blob:none --no-checkout https://github.com/apple-oss-distributions/xnu.git "$xnu_path"
fi

git -C "$xnu_path" fetch --depth 1 origin "$xnu_sha"
git -C "$xnu_path" checkout --detach "$xnu_sha"
actual="$(git -C "$xnu_path" rev-parse HEAD)"
[[ "$actual" == "$xnu_sha" ]] || { echo "Unexpected XNU commit: $actual" >&2; exit 1; }
echo "XNU source: $xnu_path @ $actual"
