#!/usr/bin/env bash
# Synthetic fixtures only; never reads an IPSW or a real executable's metadata.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/b2l-amfi-tests.XXXXXX")"
trap 'rm -rf "$task_test_dir"' EXIT
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
clang_path="$(xcrun --find clang)"
clangxx_path="$(xcrun --find clang++)"
for fixture in metadata identity der-metadata cdhash; do
 "$clang_path" -isysroot "$sdk_path" -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$repo_root/xnu-overlay/bsd/kern" -I"$repo_root/common/cache" \
  "$repo_root/tests/test-$fixture.c" "$repo_root/xnu-overlay/bsd/kern/b2l_amfi_metadata.c" "$repo_root/common/cache/CacheSha.c" \
  -o "$task_test_dir/$fixture"
 "$task_test_dir/$fixture"
done
"$clangxx_path" -isysroot "$sdk_path" -std=c++17 -Wall -Wextra -Werror \
 -fsanitize=address,undefined "$repo_root/tests/test-entitlements.cpp" \
 -o "$task_test_dir/entitlements"
"$task_test_dir/entitlements"
python3 "$repo_root/tests/test-lifecycle.py"
