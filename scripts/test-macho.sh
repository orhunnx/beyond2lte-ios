#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/out/tests}"
mkdir -p "$build_dir"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$repo_root/tests/macho_test.c" "$repo_root/uefi/XnuLoader/MachO.c" \
  -o "$build_dir/macho_test"
"$build_dir/macho_test"
