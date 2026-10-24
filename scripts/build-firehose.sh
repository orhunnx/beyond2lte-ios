#!/bin/bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
base="${B2L_BUILD_ENV:-$repo_root/out/build-env-0063}"
source "$base/env.sh"
xcodebuild -project "$base/src/libdispatch/libdispatch.xcodeproj" -target libfirehose_kernel -configuration Release -sdk iphoneos -arch arm64 OBJROOT="$base/firehose-obj" SYMROOT="$base/firehose-sym" DSTROOT="$B2L_DEPSROOT" CODE_SIGNING_ALLOWED=NO OTHER_CFLAGS='$(inherited) -mcpu=generic -march=armv8-a+crypto' HEADER_SEARCH_PATHS="$B2L_DEPSROOT/usr/local/include/kernel $B2L_DEPSROOT/System/Library/Frameworks/Kernel.framework/PrivateHeaders $B2L_DEPSROOT/System/Library/Frameworks/Kernel.framework/Headers $base/src/libdispatch $B2L_DEPSROOT/usr/local/include/os $B2L_DEPSROOT/usr/local/include/firehose $KDKROOT/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders" install

cp "$B2L_DEPSROOT/usr/local/lib/kernel/liblibfirehose_kernel.a" "$B2L_DEPSROOT/usr/local/lib/kernel/libfirehose_kernel.a"
