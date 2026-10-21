#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/out/tests}"
mkdir -p "$build_dir"
"$repo_root/scripts/test-macho.sh"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$repo_root/tests/device_tree_test.c" "$repo_root/uefi/XnuLoader/DeviceTree.c" \
  -o "$build_dir/device_tree_test"
"$build_dir/device_tree_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$repo_root/tests/arena_test.c" "$repo_root/uefi/XnuLoader/Arena.c" \
  -o "$build_dir/arena_test"
"$build_dir/arena_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$repo_root/tests/progress_test.c" "$repo_root/uefi/XnuLoader/Progress.c" \
  -o "$build_dir/progress_test"
"$build_dir/progress_test"
