#!/usr/bin/env bash
# Test original compatibility sources; all generated outputs stay temporary.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
xnu="${1:?Pass the prepared pinned XNU source directory}"
source_dir="$repo/xnu-overlay/osfmk/arm64"
# Mach-O syntax differs from ELF: native vectors catch dropped address adds.
if [[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]]; then
  python3 "$repo/tests/test-pacga-darwin.py"
fi
work="$(mktemp -d /private/tmp/b2l-arm64e-tests.XXXXXX)"
trap 'rm -rf "$work"' EXIT INT TERM
cc="${CLANG:-$(xcrun --find clang)}"
llvm_bin="${LLVM_BIN:-/opt/homebrew/opt/llvm/bin}"
lld="${LD_LLD:-$(command -v ld.lld)}"
mkdir "$work/core" "$work/pacga"
cp "$repo/tests/arm64e/core/"* "$work/core/"
cp "$source_dir/b2l_compat.c" "$source_dir/b2l_compat.h" "$work/core/"
cp "$source_dir/b2l_compat_fast.s" "$work/core/b2l_compat_fast.S"
LLVM_MC="$llvm_bin/llvm-mc" python3 "$work/core/generate-fixtures.py"
flags=(-isysroot "$(xcrun --sdk macosx --show-sdk-path)" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer)
"$cc" "${flags[@]}" "$work/core/b2l_compat.c" "$work/core/test_compat.c" -o "$work/core/reference"
"$work/core/reference"
"$cc" "${flags[@]}" -DB2L_COMPAT_USE_ASM "$work/core/b2l_compat.c" "$work/core/b2l_compat_fast.S" "$work/core/test_compat.c" -o "$work/core/assembly"
"$work/core/assembly"
python3 "$repo/tests/test-pacga-runtime.py" "$xnu"
python3 "$repo/tests/test_arm64e_grade.py" "$xnu"
python3 "$repo/tests/arm64e/pager/run.py" "$xnu" --negative-shadow-regression
cp "$repo/tests/arm64e/pacga/"* "$work/pacga/"
cp "$source_dir/b2l_pacga_qarma5.c" "$source_dir/b2l_pacga_qarma5.h" "$work/pacga/"
cp "$source_dir/b2l_pacga_qarma5_arm64.s" "$work/pacga/b2l_pacga_qarma5_arm64.S"
"$cc" "${flags[@]}" "$work/pacga/test-pacga.c" -o "$work/pacga/host"
"$work/pacga/host"
"$cc" -target aarch64-none-elf -ffreestanding -mgeneral-regs-only -c "$work/pacga/b2l_pacga_qarma5_arm64.S" -o "$work/pacga/asm.o"
"$cc" -target aarch64-none-elf -O2 -DB2L_TEST_ASM -ffreestanding -fno-builtin -fno-stack-protector -ffixed-x18 -mgeneral-regs-only -Wall -Wextra -Werror -c "$work/pacga/test-pacga.c" -o "$work/pacga/test.o"
"$cc" -target aarch64-none-elf -c "$work/pacga/test-start.S" -o "$work/pacga/start.o"
"$lld" -e _start -Ttext=0x40080000 "$work/pacga/start.o" "$work/pacga/test.o" "$work/pacga/asm.o" -o "$work/pacga/test.elf"
qemu-system-aarch64 -machine virt -cpu cortex-a53 -m 128M -nographic -semihosting-config enable=on,target=native -kernel "$work/pacga/test.elf"
echo 'ARM64e core, native assembly, QARMA5, wrapper, grade and mixed-page gates passed'
