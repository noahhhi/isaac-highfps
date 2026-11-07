#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
: "${XWIN_ROOT:?Set XWIN_ROOT to the xwin splat directory (x86 CRT and SDK)}"
: "${WINMM_SOURCE:?Set WINMM_SOURCE to a 32-bit system winmm.dll for export generation}"
compiler="${CXX:-clang-cl}"
python3 tools/gen_proxy.py "$WINMM_SOURCE"
mkdir -p build
"$compiler" --target=i686-pc-windows-msvc -fuse-ld=lld \
  /std:c++17 /EHsc /O2 /MT /LD /DHIGHFPS_BUILD_19715 /D_CRT_SECURE_NO_WARNINGS \
  "/imsvc$XWIN_ROOT/crt/include" "/imsvc$XWIN_ROOT/sdk/include/ucrt" \
  "/imsvc$XWIN_ROOT/sdk/include/shared" "/imsvc$XWIN_ROOT/sdk/include/um" \
  /Fo:build/ /Fe:build/winmm.dll src/dllmain.cpp src/winmm_thunks.cpp \
  /link /DEF:src/winmm.def "/LIBPATH:$XWIN_ROOT/crt/lib/x86" \
  "/LIBPATH:$XWIN_ROOT/sdk/lib/ucrt/x86" "/LIBPATH:$XWIN_ROOT/sdk/lib/um/x86"
