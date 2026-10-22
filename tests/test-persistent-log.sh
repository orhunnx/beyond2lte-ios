#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
arm="$root/xnu-overlay/osfmk/arm64"
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread -DB2L_LOG_TEXT_CAPACITY=256 -I"$arm" "$arm/b2l_log_core.c" "$root/tests/test-persistent-log.c" -o "$tmp/test"
"$tmp/test" "$tmp/region"
python3 "$root/scripts/b2l-log-region.py" decode "$tmp/region" --text "$tmp/text"
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -DB2L_UFS_HOST_TEST -I"$root/xnu-overlay/bsd/dev" "$root/tests/test-ufs-aux.c" "$root/xnu-overlay/bsd/dev/b2l_ufs_aux.c" "$root/xnu-overlay/bsd/dev/b2l_ufs_core.c" -o "$tmp/aux"
"$tmp/aux"
