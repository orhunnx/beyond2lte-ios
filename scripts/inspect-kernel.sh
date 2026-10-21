#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/out/tests}"
[[ $# == 1 ]] || { echo "Usage: $0 kernel" >&2; exit 2; }
mkdir -p "$build_dir"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror \
  "$repo_root/tests/inspect_kernel.c" "$repo_root/uefi/XnuLoader/MachO.c" \
  -o "$build_dir/inspect_kernel"
"$build_dir/inspect_kernel" "$1"
