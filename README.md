# isaac-highfps — Repentance+ 1.9.7.15.J374

This fork of [hunchcldev/isaac-highfps](https://github.com/hunchcldev/isaac-highfps) separates Isaac's display output from its native update clocks. It targets the **32-bit Repentance+ 1.9.7.15.J374 executable** and is being tested on Steam Deck under Proton.

The main update stays at 60 Hz, full world updates stay at 30 Hz, and presentation follows the configured display or frame cap. Native entity and room-scroll previews provide intermediate positions. Audited native render-side animation updates and supported Lua drawing run on the fixed timeline, with their results reused for additional presentations.

**0.13.0-j374.4 — menu page presentation.** Source `4c552f0` interpolates the menu viewport only while displaying a frame, then restores the native coordinates before another update. Main-menu ↔ character-page slides now use intermediate positions at 80/90 Hz without advancing input or animation clocks faster. The original discrete selection arrow is preserved. See the [menu evidence](docs/validation/menu-presentation.json). The earlier camera correction (`1444261`) and fixed-clock changes retain their separately hashed reports below. The tested cases and remaining gaps are listed below. This is not a claim that every mod, animation or graphics path is compatible. The upstream 1.9.7.17/J460 DLL cannot be installed directly on J374; bypassing its version check does not make its addresses or calling conventions compatible.

## Install on Steam Deck

1. Exit the game. Back up any existing `winmm.dll`, `isaac-highfps.ini`, game options and saves.
2. Copy the **J374 build** of `winmm.dll` beside `isaac-ng.exe`.
3. Add this Steam launch option, preserving any other options you use:

   ```text
   WINEDLLOVERRIDES="winmm=n,b" %command%
   ```

4. Start with the defaults below. Restart the game after changing the INI.

```ini
[highfps]
Enabled=1
Interpolate=1
MaxFps=0
VSync=1
IdleFps=0
LuaVanillaCadence=1
LuaOverlay=1
Log=1
Diag=0
```

`MaxFps=0` follows the available presentation pacing; it does not force 90 or 100 FPS. `VSync=1` requests actual OpenGL swap synchronization. A display mode limited to 80 Hz can therefore use the same configuration. To request a 100 FPS cap, set `MaxFps=100`; presenting 100 distinct frames also requires a suitable display mode and enough rendering performance.

`IdleFps=0` keeps the normal display pacing in menus and while paused. There is no default forced 60 FPS idle cap.

Variable presentation intervals are supported by the scheduler. VRR still depends on the display, driver, compositor and game session enabling it. **Physical VRR and runtime refresh-change validation are pending.** An attempted runtime compositor-limit change did not change measured presentation cadence, so it is not counted as a passing variable-rate test. The offline variable-interval scheduler tests and separate 80/100 Hz runs cover different scopes.

To disable the mod, set `Enabled=0` and restart. To remove it, exit the game, remove this fork's DLL and restore any previous DLL/configuration and launch option from your backup. The mod does not patch the executable on disk.

## Configuration

| Setting | Default | Meaning |
| --- | --- | --- |
| `Enabled` | `1` | Enable the native component. |
| `Interpolate` | `1` | Preview intermediate native entity/camera positions for presentation. |
| `MaxFps` | `0` | Follow presentation pacing, or request a positive FPS cap. |
| `VSync` | `1` | `1`: request swap synchronization; `0`: disable it; `-1`: follow the engine's interval. |
| `IdleFps` | `0` | No separate idle cap; a positive value requests one. |
| `LuaVanillaCadence` | `1` | Enable the fixed native-render/callback pipeline. |
| `LuaOverlay` | `1` | Legacy configuration name that enables supported draw capture/replay in this pipeline. |
| `Log` | `1` | Write `%TEMP%\isaac-highfps.log`, with a game-directory fallback. |
| `Diag` | `0` | Detailed timing/cache diagnostics; enable for controlled tests or bug reports. |

Keep both Lua options enabled for the tested pipeline. In this fork, `LuaOverlay=1` no longer describes the old per-callback FBO overlay approach: the active pipeline captures supported native draw commands and replays them. Turning either option off leaves the tested compatibility path and is not a recommended fix for a rendering fault.

## Mod drawing and compatibility

A fixed tick completes native **Update → Render → GC** before the next tick. Supported main and entity render callbacks execute in that native render, so state changes made by callbacks are not repeated for every display refresh. Additional presentations replay supported drawing without executing those Lua callbacks again.

**Update-owned render-to-texture is a separate category.** Some effects render and invoke Lua while the engine is updating, then are removed. Those calls must execute natively even if the previous display pass was pure; they must not be suppressed as extra presentations. Blood Splat (effect variant 7) exposed this boundary. The dedicated direct-spawn regression restored all 30 native render callbacks across ten instances; the previous build produced only 20. Baked pixels and natural damage producers need separate evidence. [Report](docs/validation/blood-callback-comparison.json).

The replay keeps the drawing coordinates sampled at the canonical 60 Hz render. It does **not** guess how to translate arbitrary mod graphics to an interpolated entity position. A native sprite can move smoothly between ticks while a mod-drawn label or marker attached to it still changes position at 60 Hz.

Capture is bounded and checks draw layout, resource ownership and render state. Custom shaders, direct GL operations and other unsupported drawing can prevent a scope from being replayed on extra presentations. Shader-parameter callback caching is a separate path; broad custom-shader compatibility remains under validation. Report missing or flickering mod graphics with the log and mod list rather than assuming all render callbacks are safe to resample.

The Lua globals `HIGH_FPS_NATIVE`, `HIGH_FPS_VERSION` and `HIGH_FPS_RATE` remain available. `HIGH_FPS_RATE` reports measured output-loop cadence; counting `MC_POST_RENDER` is not an output-FPS measurement in the fixed pipeline.

## Validation status

Tests use a separate game copy, Proton prefix and saves. Native 60 Hz on **the same J374 executable** is the baseline. Reports identify the DLL, fixture and executable hashes so results can be tied to the revision that was tested.

| Check | Observed result | Scope |
| --- | --- | --- |
| Main-menu ↔ character-page slides, final 80/90 Hz | Identical adjacent captured-frame pairs in declared movement windows: old90 109/316 (34.49%), new90 5/316 (1.58%), new80 3/280 (1.07%). Each new case's 488 canonical viewport samples matched native60 to log precision; restore and endpoint checks passed. | Four round trips per case. Capture/input timestamps have startup uncertainty; this is not an exact input-latency measurement or coverage of every menu. [Report](docs/validation/menu-presentation.json). |
| Large-room camera, final 80/90 Hz | Old 90 Hz camera: 22 out-of-bounds samples and 20 reverse steps against a consistently directed native camera. New 80/90 Hz cases: zero for both checks, with interpolation staying within each pair of native samples. All 2,175 normalized canonical records matched native60. | One fixed-seed Greed tall room, long travel/stops/reversal; numerical presentation checks and canonical-state checks have separate scopes. [Camera](docs/validation/camera-presentation.json), [state](docs/validation/greed-canonical.json). |
| Final R5 callback/draw smoke | All 483 recorded events matched native60; 286 paired player/tear position samples had zero error. Measured output was 100.010 FPS. One inspected capture showed MAIN/PLAYER/TEAR labels and M/P/T markers. | Same R5 DLL as the 80 Hz test; one inspected frame is not every-frame pixel coverage. [Report](docs/validation/release-r5-evidence.json). |
| Final R5 adaptive 80 Hz | Measured output 79.994 FPS, active game regression 30.003 Hz; measured callback/update counts and listed player/timer/fire/bomb states matched. The 33-shot game-tick sequence matched. | Natural tear births differed; early birth-relative displacement was compared separately. [Report](docs/validation/release-r5-evidence.json). |
| Callback order at other output rates | Earlier 90 and 45 Hz cases matched all 483 recorded events and callback state/order in the controlled fixture. | Reports retain each tested DLL hash. [Report](docs/validation/canonical-overlay-comparison.json). |
| Output versus update cadence | 100 Hz fixture: median present interval 9.999 ms, Manager regression 59.992 Hz, active game rate 30.004 Hz. | Isolated Proton/Gamescope presentation trace, not physical scanout. [Report](docs/validation/render-types-comparison.json). |
| Rendering below 60 FPS | 45 Hz fixture: median present interval 22.222 ms, Manager regression 60.004 Hz, active game rate 30.000 Hz. | Exercises multiple complete fixed ticks between some presentations. [Report](docs/validation/render-types-comparison.json). |
| Main and ten entity callback categories | All eleven categories were observed at 60, 100 and 45 Hz. | Coverage is not universal equivalence: short-lived tear/projectile totals differed; the Blood Splat Update-owned render path is covered separately below. [Report](docs/validation/render-types-comparison.json). |
| Update-owned Blood Splat rendering | Ten direct spawns: 540 normalized event records and ten terminal summaries matched native60; 30 render calls restored, including ten Update-owned calls. | Controlled direct-spawn path and callback state/order; not natural NPC-damage production or baked pixels. [Report](docs/validation/blood-callback-comparison.json). |
| Room transitions | WALK, FADE and TELEPORT had matching phase sequences and aligned Manager-tick spans; maximum paired alpha-curve timing differences were 4.24, 6.49 and 10.13 ms. | One native 60 Hz/output 100 Hz pair per transition style; not all animation types or repeatability. [Report](docs/validation/transition-matrix-comparison.json). |
| Screen-shader parameter callbacks | All 2,240 recorded parameter events matched after excluding wall time, with matching per-game-tick order. | Parameter cadence/table isolation only; GPU pixels and Sprite/Image custom shaders remain pending. [Report](docs/validation/shader-comparison.json). |
| Normal/Hard/Greed menu fades | All 16 paired fade curves matched their interior values over 18 Manager ticks; maximum relative curve timing difference was 10.474 ms. | Real difficulty changes in character selection. Greedier and other menu paths are not covered. [Report](docs/validation/menu-curve-comparison.json). |
| Ordinary floor transition | Stage 1→2 trapdoor: all 125 event snapshots matched after time/origin normalization; request through ten resumed world updates took 11.784/11.787 s. | One ordinary floor route, not every cinematic or alternate stage. [Report](docs/validation/floor-comparison.json). |
| Moving pause/resume | About three seconds paused with nonzero retained velocity: position, game frame and TimeCounter stayed fixed. Each two-second resume window advanced 60 world/120 UI ticks. | Within-run invariants; the two run seeds differ. [Report](docs/validation/pause-moving-comparison.json). |
| Glowing Hour Glass rewind | 104 normalized records matched; one use restored the room/game clock, then resumed ten world updates with matching counters. | Actual RestoreState/backwards-clock path; no live Lua VM replacement was observed. [Report](docs/validation/rewind-comparison.json). |
| Remaining coverage | Physical/runtime VRR, broader menu/floor/custom-shader paths and persistent dice/emote behavior. | No universal mod compatibility claim. |
| Physical VRR | Pending. | Requires suitable display hardware and a VRR-enabled presentation stack. |

No single FPS counter proves correct gameplay speed or smooth output. The validation separates simulation state/timers, callback order, native presentation timing and visible pixels. See the [port notes](docs/port-1.9.7.15.md) and [clock design](docs/clock-design.md) for details and remaining tests.

## Build

Use the J374-specific Linux/WSL build with **clang-cl targeting the x86 MSVC ABI**, `lld-link`, Python 3, and an x86 Windows CRT/SDK sysroot:

```sh
XWIN_ROOT=/path/to/x86-msvc-sysroot \
WINMM_SOURCE=/path/to/32-bit/winmm.dll \
bash build-wsl.sh
```

The script defines `HIGHFPS_BUILD_19715`, uses `/MT` for the static CRT, and writes `build/winmm.dll`. `WINMM_SOURCE` is parsed locally to generate forwarding exports; do not distribute or commit that system DLL. GNU-ABI MinGW is not a substitute for this build's x86 SEH/MSVC ABI requirements. The unmodified upstream `build.bat` path is not the documented J374 build.

Startup cache installation waits for the initializer thread to exit, and required-hook readiness is published only after successful patching. If the required full pipeline is unavailable, the fork falls back to 60 FPS with native Lua execution. One earlier test exceeded a 45-second startup budget; its cause was not established and retry succeeded. See the [port notes](docs/port-1.9.7.15.md) for that limit.

Build details and rollback notes are in [docs/port-1.9.7.15.md](docs/port-1.9.7.15.md).

Standalone checks are documented in [tests/README.md](tests/README.md). `tests/run-tests.sh core` runs the production-clock, UI and mock draw-cache tests; `abi` adds the game-independent Windows x86 tests. The optional shader test uses a supplied local Lua runtime. None of these tests starts the game.

## Credits and license

MIT; see [LICENSE](LICENSE). Based on the original native high-FPS work by [hunchcldev](https://github.com/hunchcldev/isaac-highfps). REPENTOGON function signatures were used as reverse-engineering references; a signature match was checked against J374 instructions and calling conventions before use. Game executables, memory dumps, saves and system DLLs are not part of this repository.
