#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d /private/tmp/b2l-native-cache.XXXXXX)"
trap 'rm -rf "$tmp"' EXIT
cc="${CC:-clang}"
flags=(-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$repo/common/cache")
"$cc" "${flags[@]}" "$repo/tests/native_cache_test.c" "$repo/common/cache/b2l_cache_core.c" "$repo/common/cache/CacheSha.c" -o "$tmp/core"
"$tmp/core"
if [[ $# == 1 ]]; then
 "$cc" "${flags[@]}" "$repo/tests/native_cache_hfs_test.c" "$repo/common/cache/b2l_cache_core.c" "$repo/common/cache/CacheHfs.c" "$repo/common/cache/CacheSha.c" -o "$tmp/hfs"
 "$tmp/hfs" "$1"
fi
