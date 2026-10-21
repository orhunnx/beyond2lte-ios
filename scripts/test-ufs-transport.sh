#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/out/tests}"
mkdir -p "$build_dir"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$repo_root/xnu-overlay/bsd/dev" "$repo_root/tests/ufs_protocol_test.c" \
  -o "$build_dir/ufs_protocol_test"
"$build_dir/ufs_protocol_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$repo_root/tests/boot_device_path_test.c" -o "$build_dir/boot_device_path_test"
"$build_dir/boot_device_path_test"
