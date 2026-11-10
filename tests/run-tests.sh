#!/usr/bin/env bash
# Standalone tests only: never starts Isaac, reads game memory, or opens saves.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "$0")/.." && pwd)"
output_dir="$repo_dir/build/tests"
mkdir -p "$output_dir"
mode="${1:-core}"
case "$mode" in core|abi|shader|lua-fixture|all) ;; *) echo 'Usage: tests/run-tests.sh [core|abi|shader|lua-fixture|all]' >&2; exit 2;; esac
backend="${TEST_BACKEND:-auto}"
host_compiler="${CXX:-c++}"
windows_compiler="${CLANG_CL:-clang-cl}"
windows_runner="${TEST_WINDOWS_RUNNER:-auto}"
if [[ "$windows_runner" == auto ]]; then
  if [[ -n "${WSL_DISTRO_NAME:-}" || "$(uname -r)" == *microsoft* ]]; then windows_runner=interop
  else windows_runner=wine; fi
fi
case "$windows_runner" in interop|wine) ;; *) echo 'TEST_WINDOWS_RUNNER must be auto, interop or wine' >&2; exit 2;; esac
read -r -a host_flags <<< "${CXXFLAGS:-}"
if [[ "$backend" == auto ]]; then
  cat > "$output_dir/header-check.cpp" <<'CPP'
#include <vector>
int main(){std::vector<int> values(1);return values[0];}
CPP
  if "$host_compiler" -std=c++17 "${host_flags[@]}" "$output_dir/header-check.cpp" -o "$output_dir/header-check" > "$output_dir/header-check.log" 2>&1; then
    backend=native
  else
    backend=msvc
  fi
fi
case "$backend" in native|msvc) ;; *) echo 'TEST_BACKEND must be auto, native or msvc' >&2; exit 2;; esac

require_windows_build() {
  : "${XWIN_ROOT:?Set XWIN_ROOT to an x86 Windows CRT/SDK sysroot}"
  command -v "$windows_compiler" >/dev/null
}
build_windows() {
  require_windows_build
  local source="$1" name="$2"
  "$windows_compiler" --target=i686-pc-windows-msvc -fuse-ld=lld \
    /std:c++17 /EHsc /O2 /MT /D_CRT_SECURE_NO_WARNINGS \
    "/I$repo_dir/src" "/imsvc$XWIN_ROOT/crt/include" \
    "/imsvc$XWIN_ROOT/sdk/include/ucrt" "/imsvc$XWIN_ROOT/sdk/include/shared" \
    "/imsvc$XWIN_ROOT/sdk/include/um" "/Fo:$output_dir/$name.obj" \
    "/Fe:$output_dir/$name.exe" "$source" \
    /link /SUBSYSTEM:CONSOLE "/LIBPATH:$XWIN_ROOT/crt/lib/x86" \
    "/LIBPATH:$XWIN_ROOT/sdk/lib/ucrt/x86" "/LIBPATH:$XWIN_ROOT/sdk/lib/um/x86"
}
run_windows() {
  local exe="$1"; shift
  if [[ "$windows_runner" == interop ]]; then
    timeout "${TEST_TIMEOUT:-120}" "$exe" "$@"
    return
  fi
  # A separate prefix under ignored build output; never reuse a game prefix.
  local prefix="${TEST_WINE_PREFIX:-$output_dir/wine-prefix}"
  command -v wine >/dev/null
  WINEPREFIX="$prefix" WINEARCH=win64 WINEDEBUG=-all \
    timeout "${TEST_TIMEOUT:-120}" wine "$exe" "$@"
}
run_core() {
  local name source
  for name in render_timing_test camera_presentation_test clock_lifecycle_test native_render_clock_test native_ui_replay_test entity_draw_cache_probe_test; do
    source="$repo_dir/tests/$name.cpp"
    printf '\n[core] %s (%s)\n' "$name" "$backend"
    if [[ "$backend" == native ]]; then
      "$host_compiler" -std=c++17 -O2 -Wall -Wextra "${host_flags[@]}" \
        -I "$repo_dir/src" "$source" -o "$output_dir/$name"
      "$output_dir/$name"
    else
      build_windows "$source" "$name"
      run_windows "$output_dir/$name.exe"
    fi
  done
}
run_abi() {
  printf '\n[abi] entity_draw_cache_runtime_test\n'
  build_windows "$repo_dir/tests/entity_draw_cache_runtime_test.cpp" entity_draw_cache_runtime_test
  run_windows "$output_dir/entity_draw_cache_runtime_test.exe"
}
run_shader() {
  : "${SHADER_LUA_DLL:?Set SHADER_LUA_DLL to a compatible local Lua 5.3 runtime DLL; no DLL is bundled/copied}"
  [[ -f "$SHADER_LUA_DLL" ]] || { echo 'SHADER_LUA_DLL does not exist' >&2; return 2; }
  printf '\n[shader] shader_params_cache_test (external Lua runtime)\n'
  build_windows "$repo_dir/tests/shader_params_cache_test.cpp" shader_params_cache_test
  local prefix="${TEST_WINE_PREFIX:-$output_dir/wine-prefix}"
  local lua_windows_path
  if [[ "$windows_runner" == interop ]]; then lua_windows_path="$(wslpath -w "$SHADER_LUA_DLL")"
  else lua_windows_path="$(WINEPREFIX="$prefix" WINEARCH=win64 WINEDEBUG=-all winepath -w "$SHADER_LUA_DLL")"; fi
  run_windows "$output_dir/shader_params_cache_test.exe" "$lua_windows_path"
}
windows_path() {
  if [[ "$windows_runner" == interop ]]; then wslpath -w "$1"
  else WINEPREFIX="${TEST_WINE_PREFIX:-$output_dir/wine-prefix}" WINEARCH=win64 WINEDEBUG=-all winepath -w "$1"; fi
}
run_lua_fixture() {
  : "${SHADER_LUA_DLL:?Set SHADER_LUA_DLL to a compatible local Lua 5.3 runtime DLL}"
  printf '\n[lua-fixture] offline rewind state machine\n'
  build_windows "$repo_dir/tests/lua_fixture_test.cpp" lua_fixture_test
  run_windows "$output_dir/lua_fixture_test.exe" "$(windows_path "$SHADER_LUA_DLL")" \
    "$(windows_path "$repo_dir/tests/rewind_state_machine_test.lua")" \
    "$(windows_path "$repo_dir/tests/fixtures/rewind-fixture.lua")"
}
if [[ "$mode" == core || "$mode" == all ]]; then run_core; fi
if [[ "$mode" == abi || "$mode" == all ]]; then run_abi; fi
if [[ "$mode" == shader ]]; then run_shader; fi
if [[ "$mode" == lua-fixture ]]; then run_lua_fixture; fi
if [[ "$mode" == all ]]; then
  if [[ -n "${SHADER_LUA_DLL:-}" ]]; then run_shader; run_lua_fixture
  else printf '\n[SKIP] optional shader/Lua fixture tests: SHADER_LUA_DLL not supplied\n'; fi
fi
printf '\nSelected standalone tests passed. Build output: %s\n' "$output_dir"
