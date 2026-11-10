# Standalone tests

These tests do not start Isaac, read process memory or open game saves. They compile against the production headers in `src/`.

```sh
bash tests/run-tests.sh core
```

`core` covers scheduler/lifecycle debt, camera and menu viewport presentation, menu scalar timing, native UI snapshot/replay ownership, and mock draw-command capture/replay. It uses a normal native C++17 compiler when available. If native standard-library headers are unavailable, automatic selection falls back to the x86 MSVC build and requires `XWIN_ROOT` plus WSL Windows interop or Wine.

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

`menu_presentation_test.cpp` checks the production menu viewport helper: independent 60 Hz interpolation, first-sample and resize/lifetime resets, canonical-state preservation, exact scoped restoration, low-output-rate catch-up samples, invalid values, and preserving unexpected native writes. Page ID is excluded from identity so navigation retains slide history. This test does not change the native discrete selection cursor or replace live page-transition captures.

The `.5` draw-cache regressions extend two existing test files:

- `entity_draw_cache_probe_test.cpp` checks deferred caller-written vertex attributes across shared-image layout changes, ordered replay, callback-final resource state, failed-submit restoration and rejection of an unsupported final format.
- `entity_draw_cache_runtime_test.cpp` invokes the actual NativeBackend against controlled J374-shaped data with GL-query stubs. It checks all four supported native Sprite shader layouts, extra-uniform rejection, program/descriptor generation changes and the format-setter ABI. Texture tests distinguish unrelated deletion from captured-quad/native-target/FBO dependencies; cover zero/nonexistent names, primary/palette name reuse, invalid arguments and foreign-thread invalidation; and ensure both deletes pass through a lazy resolver while preserving its original target. The initialization follow-up adds executable call-site stubs for the three-argument parameter and nine-argument upload hooks, current-unit texture identity, actual same-value parameters, base-level native upload arguments, dependency/FBO rejection, unsupported callers/arguments and lazy-target preservation. It does not claim arbitrary GPU sampler-state replay.

These tests do not load proprietary game data or establish GPU pixels. The [cache compatibility report](../docs/validation/cache-compatibility-unit.json) records their production-source hashes and the separate real Regret first-load diagnostic result. GoodTrip/Regret image and interaction evidence is described in the [port notes](../docs/port-1.9.7.15.md#5-ui-draw-cache-compatibility); counters alone are insufficient to accept drawing. Unknown native GL paths still fail closed rather than running extra Lua callbacks.

`input_result_cache_test.cpp` is also part of `abi`. It installs the production input hooks into executable x86 stubs and checks both four-argument RET16 calling conventions, native execution followed by pure reuse, false override versus nil, untouched outputs on nil/miss, exact float bits, hook/entity/null-key separation and the latest completed result after recursive calls. It verifies that repeated outside-render updates still execute, that tick/scene/dispatcher/VM-close changes invalidate samples, and that partial installation restores the prior close chain. Capacity is checked at 4096 keys. These are query-result tests, not a claim that input callbacks run only 60 times per second or that a physical controller has passed. Runtime queries missing a sample must remain visible in diagnostics. See [input-cache-unit.json](../docs/validation/input-cache-unit.json).

Do not commit generated executables, object files, DLL inputs, prefixes, proprietary game files or saves. Unit success does not establish live-game timing, pixel output, physical VRR or arbitrary mod compatibility; those have separate validation reports under `docs/validation/`.

Live-game fixtures are documented in [fixtures/README.md](fixtures/README.md). They are never deployed by this runner.

Live tests must use a separate game/profile and silent output: set MusicVolume=0, SFXVolume=0 and OnlinePlayerVolume=0 before every test launch, including after profile copies. Keep MusicEnabled and animation/voice-mode logic intact, and leave the normal game's volume alone. Menu capture analysis is read-only: `python tests/analyze_main_menu.py BASE_CASE TRIAL_CASE... --output REPORT.json`. The recorded page sequence is main menu → character page → main menu, four times with settled input intervals; it never confirms from character selection into a run.

`carousel_presentation_test.cpp` checks native-derived XY/scale history and exact restoration at 45/60/80/90/100/144 output rates, wrap/reversal, low-output catch-up samples, first sample and owner/roster/page/size invalidation, stale writes, bounds/alignment/alias rejection and invalid-time values. It does not call game functions. The runtime samples once per canonical render and restores the temporary display arrays before native updates.

Carousel recording analysis is offline and requires Pillow: `python tests/analyze_carousel.py NATIVE_CASE OLD_CASE FINAL90_CASE FINAL80_CASE --output report.json`. Each case contains `input-events.json`, `menu-clock.json`, `capture/frame-NNNN.jpg` and `capture/gst.log`, plus `native.log` for enabled builds. An independently guarded harness supplies those recordings; the analyzer never opens a process or generates input. It compares binary32 canonical pose sequences and all logged interpolation endpoints, and retains full image run histograms plus 33.333/50/75 ms short-run sensitivities. These conditional pixel statistics exclude long settled image runs and are not unconditional stutter or input-latency measurements.
