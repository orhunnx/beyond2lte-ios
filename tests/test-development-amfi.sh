#!/usr/bin/env bash
# Synthetic fixtures only; never reads an IPSW or a real executable's metadata.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/b2l-amfi-tests.XXXXXX")"
trap 'rm -rf "$task_test_dir"' EXIT
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
clang_path="$(xcrun --find clang)"
clangxx_path="$(xcrun --find clang++)"
"$clang_path" -isysroot "$sdk_path" -std=c11 -Wall -Wextra -Werror \
 -fsanitize=address,undefined -I"$repo_root/xnu-overlay/bsd/kern" \
 "$repo_root/tests/test-metadata.c" "$repo_root/xnu-overlay/bsd/kern/b2l_amfi_metadata.c" \
 -o "$task_test_dir/metadata"
"$clang_path" -isysroot "$sdk_path" -std=c11 -Wall -Wextra -Werror \
 -fsanitize=address,undefined -I"$repo_root/xnu-overlay/bsd/kern" \
 "$repo_root/tests/test-identity.c" "$repo_root/xnu-overlay/bsd/kern/b2l_amfi_metadata.c" \
 -o "$task_test_dir/identity"
"$clang_path" -isysroot "$sdk_path" -std=c11 -Wall -Wextra -Werror \
 -fsanitize=address,undefined -I"$repo_root/xnu-overlay/bsd/kern" \
 "$repo_root/tests/test-der-metadata.c" "$repo_root/xnu-overlay/bsd/kern/b2l_amfi_metadata.c" \
 -o "$task_test_dir/der"
"$task_test_dir/der"
"$task_test_dir/identity"
"$task_test_dir/metadata"
"$clangxx_path" -isysroot "$sdk_path" -std=c++17 -Wall -Wextra -Werror \
 -fsanitize=address,undefined "$repo_root/tests/test-entitlements.cpp" \
 -o "$task_test_dir/entitlements"
"$task_test_dir/entitlements"
python3 "$repo_root/tests/test-lifecycle.py"
