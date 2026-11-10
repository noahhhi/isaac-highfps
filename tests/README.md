# Standalone tests

These tests do not start Isaac, read process memory or open game saves. They compile against the production headers in `src/`.

```sh
bash tests/run-tests.sh core
```

`core` covers scheduler/lifecycle debt, camera presentation, menu scalar timing, native UI snapshot/replay ownership, and mock draw-command capture/replay. It uses a normal native C++17 compiler when available. If native standard-library headers are unavailable, automatic selection falls back to the x86 MSVC build and requires `XWIN_ROOT` plus WSL Windows interop or Wine.

```sh
XWIN_ROOT=/path/to/x86-msvc-sysroot TEST_BACKEND=msvc \
  bash tests/run-tests.sh all
```

`abi` runs the game-independent Windows x86 hook/runtime tests, including generated stdcall/fastcall code, XMM preservation, image-reference leases, canonical/pure callback behavior and forwarding outside the Manager-render scope. It requires clang-cl, lld-link, the x86 Windows CRT/SDK and WSL Windows interop or Wine. On WSL the runner directly executes the standalone PE through interop. The Wine backend uses its own prefix under ignored `build/tests/`; a game prefix is not required.

`shader` is optional and additionally needs a compatible local Lua 5.3 DLL:

```sh
XWIN_ROOT=/path/to/x86-msvc-sysroot \
SHADER_LUA_DLL=/path/to/lua53.dll \
  bash tests/run-tests.sh shader
```

The runtime is loaded from that path; it is not copied into the repository or bundled. A separately built open-source Lua runtime can be used. The test checks shader-name/ordinal results, Lua registry ownership and close/reopen handling; it does not test a GPU shader or game pixels.

`lua-fixture` uses that same supplied runtime to execute the offline fake-API rewind harness. It opens no game or save and tests fixture phase persistence.

`all` runs core and ABI tests, and runs shader plus offline Lua fixture tests when `SHADER_LUA_DLL` is supplied. Otherwise it prints an explicit optional skip. `TEST_BACKEND=native|msvc`, `CXX`, `CXXFLAGS`, `CLANG_CL`, `TEST_WINDOWS_RUNNER=auto|interop|wine`, `TEST_WINE_PREFIX` and `TEST_TIMEOUT` can override local tool choices. Use a dedicated test prefix if overriding its location.

The existing `render_timing_test.cpp` is included in the core run without changing its source. It adds fixed-rate/variable-interval scheduler and motion-model checks; the lifecycle test also checks explicit exclusion versus retained ordinary debt.

`camera_presentation_test.cpp` checks the production camera helper independently of player prediction: monotone native-camera easing at 45/60/80/90/100/144 FPS, continuous stops/reversals/clamps, camera motion while world updates are frozen, stationary pause, room/epoch discontinuities, variable 48–144 Hz sample intervals and invalid-time guards. It checks sampled camera mathematics; it does not replace live large-room captures or physical VRR validation.

Do not commit generated executables, object files, DLL inputs, prefixes, proprietary game files or saves. Unit success does not establish live-game timing, pixel output, physical VRR or arbitrary mod compatibility; those have separate validation reports under `docs/validation/`.

Live-game fixtures are documented in [fixtures/README.md](fixtures/README.md). They are never deployed by this runner.
