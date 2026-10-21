#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
arm="$root/xnu-overlay/iokit/Kernel/arm"
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$arm" "$arm/b2l_aks_core.c" "$root/tests/test-aks-core.c" -o "$tmp/test"
"$tmp/test"
python3 "$root/tests/test-aks-adapter.py"
python3 "$root/tests/test-aks-client.py"
